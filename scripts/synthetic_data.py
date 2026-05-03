"""
RipenSense Synthetic Data Generator
====================================
Generates two datasets:
  - ripensense_7day_train.csv   : 7-day training dataset with varied scenarios
  - ripensense_1day_demo.csv    : 1-day demo run with a clear narrative arc

Time resolution : 1 minute per row
Shock physics   : simulated at 1-second resolution, aggregated per minute
GPS             : simulated truck route (Newark DC -> Holland Tunnel -> Manhattan stops)
Units           : temp=C, humidity=%RH, ethylene=ppb, accel=G, vpd=kPa
"""

import numpy as np
import pandas as pd
import math
from dataclasses import dataclass, field
from typing import List, Tuple

RNG = np.random.default_rng(42)

# ---------------------------------------------------------------------------
# Physical constants and thresholds (from meeting notes)
# ---------------------------------------------------------------------------
ETHYLENE_THRESHOLD_PPB      = 100.0   # 0.1 ppm = 100 ppb, point-of-no-return
ETHYLENE_TRIGGER_HOURS      = 24      # sustained exposure needed to guarantee ripening
TEMP_OPTIMAL                = 13.5    # C, midpoint of 13-14C goldilocks zone
TEMP_REF                    = 13.0    # C, Q10 reference temperature
TEMP_MIN_SAFE               = 12.0    # C, below causes chilling injury
TEMP_MAX_SAFE               = 18.0    # C, above causes cooked fruit
HUMIDITY_OPTIMAL            = 92.5    # %RH, midpoint of 90-95%
HUMIDITY_MIN_SAFE           = 85.0    # %RH
SHOCK_THRESHOLD_G           = 10.0    # G, below this no bruising
SHOCK_RI_INCREMENT          = 3.0     # RI points added per shock event above threshold
SENSOR_THERMAL_TAU_MIN      = 7.0     # minutes, SHT31 thermal time constant in still air
ETHYLENE_DIFFUSION_LAG_MIN  = 20.0    # minutes, lag before shock-induced ethylene appears
ETHYLENE_RISE_TAU_MIN       = 30.0    # minutes, time constant for ethylene rise after shock

# ---------------------------------------------------------------------------
# Ripening stage definitions
# Stage: (name, color, q10_low, q10_high, ri_min, ri_max)
# ---------------------------------------------------------------------------
STAGES = [
    (1, "Hard green",            2.0, 2.5,   0,  14),
    (2, "Green, trace yellow",   3.5, 4.0,  14,  28),
    (3, "More green than yellow",3.0, 3.0,  28,  42),
    (4, "More yellow than green",2.5, 2.5,  42,  56),
    (5, "Yellow, green tips",    2.0, 2.0,  56,  70),
    (6, "Full yellow (RIPE)",    1.8, 1.8,  70,  85),
    (7, "Yellow, brown spots",   1.5, 1.5,  85, 100),
]

def get_stage(ri: float) -> Tuple[int, float]:
    """Return (stage_number, q10) for a given RI value."""
    for stage_num, _, q10_lo, q10_hi, ri_min, ri_max in STAGES:
        if ri_min <= ri < ri_max:
            q10 = (q10_lo + q10_hi) / 2.0
            return stage_num, q10
    return 7, 1.5  # overripe

# ---------------------------------------------------------------------------
# VPD calculation (Tetens equation, meeting notes eq. 5-6)
# ---------------------------------------------------------------------------
def vp_sat(temp_c: float) -> float:
    """Saturation vapor pressure in kPa using Tetens equation."""
    return 0.61078 * math.exp(17.27 * temp_c / (temp_c + 237.3))

def vpd(temp_c: float, rh: float) -> float:
    """Vapor pressure deficit in kPa."""
    return vp_sat(temp_c) * (1.0 - rh / 100.0)

# ---------------------------------------------------------------------------
# RI integration (meeting notes eq. 1-4)
# ---------------------------------------------------------------------------
# Weights calibrated so that:
#   - Normal conditions (13.5C, 92%RH, ~3ppb ethylene) -> RI ~9 after 24h
#   - Sustained failure (20C, 80%RH, 200ppb ethylene)  -> RI ~100 after 24h
W_E = 0.04    # ethylene weight (highest)
W_T = 0.08    # temperature weight
W_H = 0.01    # humidity/VPD weight (lowest)

# Baseline VPD at optimal conditions (used to compute excess only)
_VPD_OPTIMAL = vpd(TEMP_OPTIMAL, HUMIDITY_OPTIMAL)

def ri_delta(temp_c: float, rh: float, ethylene_ppb: float,
             q10: float, dt_min: float = 1.0) -> float:
    """
    Compute RI increment for one time step (per minute).

    Temperature and VPD terms use excess-above-optimal so that
    holding conditions at the goldilocks zone contributes near-zero RI,
    and only deviations accumulate damage.
    """
    # Ethylene term: proportional to concentration above zero
    eth_rate = max(0.0, ethylene_ppb / ETHYLENE_THRESHOLD_PPB)
    e_term = W_E * eth_rate

    # Temperature term: Q10 excess above baseline metabolic rate at Tref
    q10_excess = max(0.0, (q10 ** ((temp_c - TEMP_REF) / 10.0)) - 1.0)
    t_term = W_T * q10_excess

    # VPD term: excess moisture stress above optimal baseline
    vpd_val = vpd(temp_c, rh)
    vpd_excess = max(0.0, vpd_val - _VPD_OPTIMAL)
    h_term = W_H * vpd_excess

    return e_term + t_term + h_term  # already per-minute

# ---------------------------------------------------------------------------
# GPS route simulation
# Newark DC -> Holland Tunnel -> Manhattan (several stops)
# ---------------------------------------------------------------------------
ROUTE_WAYPOINTS = [
    # (lat, lon, label)
    (40.7282, -74.1726, "Newark DC"),
    (40.7282, -74.1200, "Newark surface roads"),
    (40.7282, -74.0776, "Approaching tunnel"),
    (40.7282, -74.0197, "Holland Tunnel exit"),
    (40.7260, -74.0060, "Tribeca stop"),
    (40.7209, -74.0052, "Lower Manhattan stop"),
    (40.7157, -74.0090, "Financial District stop"),
    (40.7074, -74.0113, "Battery Park delivery"),
]

def interpolate_route(total_minutes: int) -> List[Tuple[float, float]]:
    """
    Interpolate GPS coordinates along the route over total_minutes.
    Includes realistic pauses at delivery stops.
    """
    n_waypoints = len(ROUTE_WAYPOINTS)
    # Assign time budgets: travel + pause at each stop
    stop_duration = max(1, total_minutes // (n_waypoints * 4))  # pause per stop
    travel_budget = total_minutes - stop_duration * n_waypoints
    segment_duration = max(1, travel_budget // (n_waypoints - 1))

    coords = []
    for i in range(n_waypoints - 1):
        lat0, lon0, _ = ROUTE_WAYPOINTS[i]
        lat1, lon1, _ = ROUTE_WAYPOINTS[i + 1]
        # Travel segment
        for t in range(segment_duration):
            frac = t / max(segment_duration - 1, 1)
            # Add slight jitter for road noise
            jitter_lat = RNG.normal(0, 0.0001)
            jitter_lon = RNG.normal(0, 0.0001)
            coords.append((lat0 + frac * (lat1 - lat0) + jitter_lat,
                           lon0 + frac * (lon1 - lon0) + jitter_lon))
        # Stop pause
        for _ in range(stop_duration):
            coords.append((lat1 + RNG.normal(0, 0.00005),
                           lon1 + RNG.normal(0, 0.00005)))

    # Pad or trim to exact length
    while len(coords) < total_minutes:
        coords.append(coords[-1])
    return coords[:total_minutes]

# ---------------------------------------------------------------------------
# Shock simulator (1-second resolution, aggregated to per-minute)
# ---------------------------------------------------------------------------
def simulate_shocks_per_minute(n_minutes: int,
                                shock_events: List[Tuple[int, float]]) -> Tuple[np.ndarray, np.ndarray]:
    """
    shock_events: list of (minute, peak_g) tuples
    Returns (max_g_per_minute, shock_count_per_minute)
    """
    max_g   = np.ones(n_minutes) * 1.0   # baseline ~1G (gravity)
    counts  = np.zeros(n_minutes, dtype=int)

    for minute, peak_g in shock_events:
        if minute >= n_minutes:
            continue
        # Shock is a brief impulse; show up in its minute
        max_g[minute] = max(max_g[minute], peak_g)
        if peak_g > SHOCK_THRESHOLD_G:
            counts[minute] += 1
            # Vibration decay for a few minutes after
            for j in range(1, 4):
                if minute + j < n_minutes:
                    decay = peak_g * (0.3 ** j)
                    max_g[minute + j] = max(max_g[minute + j], decay + 1.0)

    # Add road vibration baseline noise
    max_g += RNG.uniform(0.05, 0.3, n_minutes)
    return max_g, counts

# ---------------------------------------------------------------------------
# Core scenario simulator
# ---------------------------------------------------------------------------
@dataclass
class ScenarioEvent:
    """A discrete anomalous event during a shipment."""
    kind: str           # "temp_drift", "temp_spike", "humidity_drop", "shock", "ethylene_contamination"
    start_min: int
    duration_min: int
    magnitude: float    # interpretation depends on kind

def simulate_scenario(
    n_minutes: int,
    initial_ri: float,
    events: List[ScenarioEvent],
    base_temp: float = 13.5,
    base_humidity: float = 92.0,
    base_ethylene_ppb: float = 2.0,    # near-zero baseline
    time_of_day_offset_hr: float = 6.0 # hour of day at t=0
) -> pd.DataFrame:
    """
    Simulate a full shipment scenario minute by minute.
    Returns a DataFrame with all features and labels.
    """
    minutes = np.arange(n_minutes)

    # --- True underlying signals (what's actually happening physically) ---
    true_temp     = np.full(n_minutes, base_temp)
    true_humidity = np.full(n_minutes, base_humidity)
    true_ethylene = np.full(n_minutes, base_ethylene_ppb)

    # Track shock events separately for aggregation
    shock_event_list = []

    # Apply events to true signals
    for ev in events:
        s = ev.start_min
        e = min(s + ev.duration_min, n_minutes)

        if ev.kind == "temp_drift":
            # Gradual linear drift up to magnitude C over duration
            ramp = np.linspace(0, ev.magnitude, e - s)
            true_temp[s:e] += ramp
            # After event ends, gradual recovery (exponential)
            if e < n_minutes:
                recovery_tau = ev.duration_min * 0.5
                for i in range(n_minutes - e):
                    true_temp[e + i] = base_temp + ev.magnitude * math.exp(-i / max(recovery_tau, 1))

        elif ev.kind == "temp_spike":
            # Sudden spike, held, then recovery
            true_temp[s:e] += ev.magnitude
            if e < n_minutes:
                for i in range(min(60, n_minutes - e)):
                    true_temp[e + i] = base_temp + ev.magnitude * math.exp(-i / 20.0)

        elif ev.kind == "humidity_drop":
            drop = np.linspace(0, ev.magnitude, e - s)
            true_humidity[s:e] -= drop
            true_humidity[s:e] = np.clip(true_humidity[s:e], 50.0, 100.0)
            if e < n_minutes:
                for i in range(min(120, n_minutes - e)):
                    true_humidity[e + i] = base_humidity - ev.magnitude * math.exp(-i / 30.0)

        elif ev.kind == "shock":
            # magnitude = peak G force; generate 1-3 shock impulses around start_min
            n_impulses = RNG.integers(1, 4)
            for k in range(n_impulses):
                t = s + RNG.integers(0, max(1, ev.duration_min))
                g = ev.magnitude * RNG.uniform(0.7, 1.0)
                shock_event_list.append((min(t, n_minutes - 1), g))

            # Ethylene rises after diffusion lag, exponential with time constant
            for i in range(n_minutes - s):
                t_since = i  # minutes since shock
                lag = ETHYLENE_DIFFUSION_LAG_MIN
                if t_since > lag:
                    rise = ev.magnitude * 0.5 * (1 - math.exp(-(t_since - lag) / ETHYLENE_RISE_TAU_MIN))
                    # Convert G magnitude to ppb spike: 15G shock -> ~80ppb spike
                    ppb_spike = rise * (80.0 / 15.0)
                    # Decay after 2-4 hours unless climacteric threshold crossed
                    decay_tau = 120.0
                    decay = math.exp(-max(0, t_since - lag - ETHYLENE_RISE_TAU_MIN) / decay_tau)
                    true_ethylene[s + i] += ppb_spike * decay

        elif ev.kind == "ethylene_contamination":
            # External ethylene source (neighboring pallet), step up then slow decay
            for i in range(n_minutes - s):
                if i < ev.duration_min:
                    true_ethylene[s + i] += ev.magnitude
                else:
                    t_after = i - ev.duration_min
                    true_ethylene[s + i] += ev.magnitude * math.exp(-t_after / 60.0)

    # Add diurnal temperature variation (~1C swing over 24h, peaks at noon)
    for i in range(n_minutes):
        hour = (time_of_day_offset_hr + i / 60.0) % 24.0
        true_temp[i] += 0.5 * math.sin(2 * math.pi * (hour - 6) / 24.0)

    # Clip to physical bounds
    true_temp     = np.clip(true_temp,     -5.0, 40.0)
    true_humidity = np.clip(true_humidity,  0.0, 100.0)
    true_ethylene = np.clip(true_ethylene,  0.0, 5000.0)

    # --- Sensor readings (apply thermal lag and diffusion lag) ---
    # SHT31 thermal lag: exponential filter with tau = SENSOR_THERMAL_TAU_MIN
    alpha_temp = 1.0 - math.exp(-1.0 / SENSOR_THERMAL_TAU_MIN)
    alpha_hum  = 1.0 - math.exp(-1.0 / (SENSOR_THERMAL_TAU_MIN * 0.5))

    sensor_temp     = np.zeros(n_minutes)
    sensor_humidity = np.zeros(n_minutes)
    sensor_temp[0]     = true_temp[0]
    sensor_humidity[0] = true_humidity[0]

    for i in range(1, n_minutes):
        sensor_temp[i]     = sensor_temp[i-1]     + alpha_temp * (true_temp[i]     - sensor_temp[i-1])
        sensor_humidity[i] = sensor_humidity[i-1] + alpha_hum  * (true_humidity[i] - sensor_humidity[i-1])

    # Add sensor noise
    sensor_temp     += RNG.normal(0, 0.15, n_minutes)
    sensor_humidity += RNG.normal(0, 0.5,  n_minutes)
    sensor_ethylene  = true_ethylene + RNG.normal(0, 2.0, n_minutes)
    sensor_ethylene  = np.clip(sensor_ethylene, 0, None)

    # DS18B20 probe temp: slightly different from ambient (inside packaging)
    probe_temp = sensor_temp + RNG.normal(0.3, 0.2, n_minutes)

    # --- Shock aggregation ---
    max_g, shock_count = simulate_shocks_per_minute(n_minutes, shock_event_list)

    # --- RI and anomaly score integration ---
    ri         = np.zeros(n_minutes)
    stage_arr  = np.zeros(n_minutes, dtype=int)
    anomaly    = np.zeros(n_minutes)
    ri[0]      = initial_ri

    cumulative_ethylene_exposure = 0.0  # ppb-hours above threshold

    for i in range(1, n_minutes):
        stage_num, q10 = get_stage(ri[i-1])
        stage_arr[i] = stage_num

        delta = ri_delta(sensor_temp[i], sensor_humidity[i],
                         sensor_ethylene[i], q10, dt_min=1.0)

        # Shock contribution: discrete add per shock event
        delta += shock_count[i] * SHOCK_RI_INCREMENT

        # Chilling injury: temp below 11C adds RI (damage even from cold)
        if sensor_temp[i] < 11.0:
            delta += 0.02 * (11.0 - sensor_temp[i])

        ri[i] = np.clip(ri[i-1] + delta, 0.0, 100.0)

        # Track cumulative ethylene exposure above threshold
        if sensor_ethylene[i] > ETHYLENE_THRESHOLD_PPB:
            cumulative_ethylene_exposure += (sensor_ethylene[i] - ETHYLENE_THRESHOLD_PPB) / 60.0  # ppb-hours

        anomaly[i] = np.clip(ri[i] / 100.0, 0.0, 1.0)

    stage_arr[0] = get_stage(initial_ri)[0]

    # --- GPS ---
    coords = interpolate_route(n_minutes)
    lats = [c[0] for c in coords]
    lons = [c[1] for c in coords]

    # --- Timestamps ---
    start_ts = pd.Timestamp("2025-05-01 06:00:00", tz="America/New_York")
    timestamps = [start_ts + pd.Timedelta(minutes=int(i)) for i in range(n_minutes)]

    # --- VPD column ---
    vpd_arr = np.array([vpd(sensor_temp[i], sensor_humidity[i]) for i in range(n_minutes)])

    df = pd.DataFrame({
        "timestamp":            timestamps,
        "minutes_elapsed":      minutes,
        "latitude":             lats,
        "longitude":            lons,
        # Sensor readings
        "temp_c":               np.round(sensor_temp,     2),
        "humidity_rh":          np.round(sensor_humidity, 2),
        "ethylene_ppb":         np.round(sensor_ethylene, 2),
        "probe_temp_c":         np.round(probe_temp,      2),
        "max_g_last_60s":       np.round(max_g,           3),
        "shock_count_last_60s": shock_count,
        # Derived features
        "vpd_kpa":              np.round(vpd_arr,         4),
        "stage":                stage_arr,
        # Labels
        "ri_cumulative":        np.round(ri,              3),
        "anomaly_score":        np.round(anomaly,         4),
    })

    return df

# ---------------------------------------------------------------------------
# Scenario library
# ---------------------------------------------------------------------------
def make_normal_run(n_minutes: int, initial_ri: float = 2.0) -> pd.DataFrame:
    """Clean run: no anomalies, minor road vibration only."""
    events = [
        ScenarioEvent("shock", start_min=45,  duration_min=2,  magnitude=6.0),   # minor bump
        ScenarioEvent("shock", start_min=180, duration_min=2,  magnitude=7.0),
    ]
    return simulate_scenario(n_minutes, initial_ri, events)

def make_temp_drift_run(n_minutes: int, initial_ri: float = 2.0) -> pd.DataFrame:
    """Refrigeration unit slowly fails, temp drifts to ~20C over 4 hours."""
    events = [
        ScenarioEvent("temp_drift", start_min=120, duration_min=240, magnitude=7.0),
        ScenarioEvent("shock",      start_min=60,  duration_min=2,   magnitude=8.0),
    ]
    return simulate_scenario(n_minutes, initial_ri, events)

def make_temp_spike_run(n_minutes: int, initial_ri: float = 2.0) -> pd.DataFrame:
    """Door left open briefly: sudden spike to ~22C for 45 minutes."""
    events = [
        ScenarioEvent("temp_spike", start_min=200, duration_min=45, magnitude=9.0),
        ScenarioEvent("shock",      start_min=90,  duration_min=3,  magnitude=12.0),  # above threshold
    ]
    return simulate_scenario(n_minutes, initial_ri, events)

def make_rough_handling_run(n_minutes: int, initial_ri: float = 2.0) -> pd.DataFrame:
    """Multiple severe shock events, ethylene rises from bruising."""
    events = [
        ScenarioEvent("shock", start_min=30,  duration_min=5,  magnitude=18.0),
        ScenarioEvent("shock", start_min=120, duration_min=3,  magnitude=15.0),
        ScenarioEvent("shock", start_min=240, duration_min=5,  magnitude=20.0),
        ScenarioEvent("shock", start_min=350, duration_min=2,  magnitude=14.0),
    ]
    return simulate_scenario(n_minutes, initial_ri, events)

def make_humidity_drop_run(n_minutes: int, initial_ri: float = 2.0) -> pd.DataFrame:
    """Packaging failure: humidity drops below 85% for several hours."""
    events = [
        ScenarioEvent("humidity_drop", start_min=100, duration_min=180, magnitude=12.0),
        ScenarioEvent("shock",         start_min=50,  duration_min=2,   magnitude=7.0),
    ]
    return simulate_scenario(n_minutes, initial_ri, events)

def make_ethylene_contamination_run(n_minutes: int, initial_ri: float = 2.0) -> pd.DataFrame:
    """Neighboring pallet leaking ethylene into shared storage."""
    events = [
        ScenarioEvent("ethylene_contamination", start_min=60,  duration_min=120, magnitude=150.0),
        ScenarioEvent("shock",                  start_min=200, duration_min=2,   magnitude=9.0),
    ]
    return simulate_scenario(n_minutes, initial_ri, events)

def make_compound_failure_run(n_minutes: int, initial_ri: float = 2.0) -> pd.DataFrame:
    """Everything goes wrong: temp drift + rough handling + humidity drop."""
    events = [
        ScenarioEvent("temp_drift",    start_min=60,  duration_min=180, magnitude=6.0),
        ScenarioEvent("shock",         start_min=90,  duration_min=5,   magnitude=16.0),
        ScenarioEvent("shock",         start_min=200, duration_min=3,   magnitude=18.0),
        ScenarioEvent("humidity_drop", start_min=150, duration_min=120, magnitude=10.0),
        ScenarioEvent("shock",         start_min=300, duration_min=2,   magnitude=22.0),
    ]
    return simulate_scenario(n_minutes, initial_ri, events)

def make_demo_run() -> pd.DataFrame:
    """
    1-day demo narrative:
      - 0-2h:   clean start, normal conditions
      - ~3h:    severe shock (rough loading dock), ethylene clock starts
      - ~5h:    temp spikes to ~22C for 90 min (door left open), recovers
      - ~8h:    second shock during transfer
      - ~9h:    ethylene contamination from neighboring pallet (warehouse stop)
      - ~11h:   humidity drops from packaging damage
      - 14h+:   all effects compound, anomaly score peaks ~0.75
      - 20-24h: conditions normalize but damage is done, score holds high
    """
    n_minutes = 24 * 60
    events = [
        ScenarioEvent("shock",                  start_min=180,  duration_min=8,   magnitude=22.0),
        ScenarioEvent("temp_spike",             start_min=300,  duration_min=90,  magnitude=9.0),
        ScenarioEvent("shock",                  start_min=480,  duration_min=5,   magnitude=17.0),
        ScenarioEvent("ethylene_contamination", start_min=540,  duration_min=120, magnitude=180.0),
        ScenarioEvent("humidity_drop",          start_min=660,  duration_min=120, magnitude=12.0),
        ScenarioEvent("shock",                  start_min=900,  duration_min=5,   magnitude=19.0),
    ]
    return simulate_scenario(n_minutes, initial_ri=2.0, events=events,
                             base_temp=14.0, base_humidity=91.0)

# ---------------------------------------------------------------------------
# 7-day training dataset: mix of scenarios
# ---------------------------------------------------------------------------
def make_training_dataset() -> pd.DataFrame:
    """
    Generate a balanced training dataset over 7 days.
    Each scenario run is 24 hours (1440 minutes).
    Scenario mix: ~30% normal, ~70% various failure modes.
    """
    run_min = 24 * 60  # 1 day per scenario run
    scenarios = []

    # Normal runs (3 runs)
    for _ in range(3):
        ri0 = RNG.uniform(1.0, 5.0)
        scenarios.append(make_normal_run(run_min, ri0))

    # Temp drift (2 runs)
    for _ in range(2):
        ri0 = RNG.uniform(1.0, 8.0)
        scenarios.append(make_temp_drift_run(run_min, ri0))

    # Temp spike (2 runs)
    for _ in range(2):
        ri0 = RNG.uniform(2.0, 10.0)
        scenarios.append(make_temp_spike_run(run_min, ri0))

    # Rough handling (2 runs)
    for _ in range(2):
        ri0 = RNG.uniform(1.0, 6.0)
        scenarios.append(make_rough_handling_run(run_min, ri0))

    # Humidity drop (1 run)
    scenarios.append(make_humidity_drop_run(run_min, RNG.uniform(2.0, 8.0)))

    # Ethylene contamination (1 run)
    scenarios.append(make_ethylene_contamination_run(run_min, RNG.uniform(1.0, 5.0)))

    # Compound failure (2 runs -- worst case, highest anomaly scores)
    for _ in range(2):
        ri0 = RNG.uniform(3.0, 12.0)
        scenarios.append(make_compound_failure_run(run_min, ri0))

    # Tag each row with run_id and scenario type
    labels = [
        "normal", "normal", "normal",
        "temp_drift", "temp_drift",
        "temp_spike", "temp_spike",
        "rough_handling", "rough_handling",
        "humidity_drop",
        "ethylene_contamination",
        "compound_failure", "compound_failure",
    ]

    dfs = []
    for i, (df, label) in enumerate(zip(scenarios, labels)):
        df = df.copy()
        df.insert(0, "run_id",        i)
        df.insert(1, "scenario_type", label)
        dfs.append(df)

    combined = pd.concat(dfs, ignore_index=True)
    print(f"Training dataset: {len(combined):,} rows, {len(scenarios)} runs")
    print(f"  Anomaly score distribution:")
    print(f"    mean={combined.anomaly_score.mean():.3f}  "
          f"max={combined.anomaly_score.max():.3f}  "
          f"pct>0.5: {(combined.anomaly_score > 0.5).mean()*100:.1f}%")
    return combined

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    print("Generating RipenSense synthetic datasets...\n")

    # Training dataset (7 days of varied scenarios)
    train_df = make_training_dataset()
    train_path = "/mnt/user-data/outputs/ripensense_7day_train.csv"
    train_df.to_csv(train_path, index=False)
    print(f"  Saved: {train_path}\n")

    # Demo dataset (1 day, clear narrative arc)
    print("Generating 1-day demo run...")
    demo_df = make_demo_run()
    demo_df.insert(0, "run_id", 0)
    demo_df.insert(1, "scenario_type", "demo")
    demo_path = "/mnt/user-data/outputs/ripensense_1day_demo.csv"
    demo_df.to_csv(demo_path, index=False)
    print(f"  Demo dataset: {len(demo_df):,} rows")
    print(f"  Anomaly score: min={demo_df.anomaly_score.min():.3f}  "
          f"max={demo_df.anomaly_score.max():.3f}  "
          f"final={demo_df.anomaly_score.iloc[-1]:.3f}")
    print(f"  Final RI: {demo_df.ri_cumulative.iloc[-1]:.1f}/100")
    print(f"  Saved: {demo_path}\n")

    print("Done.")