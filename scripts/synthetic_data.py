"""
RipenSense Synthetic Data Generator v2
========================================
Outputs:
  ripensense_train_raw.csv     : all 50 runs concatenated, full minute-by-minute rows
  ripensense_ei_flat.csv       : flat feature table for Edge Impulse regression ingestion
                                 (one row per 30-min window, 270 features + anomaly_score)
                                 No metadata columns, no time-series structure hints.
                                 Upload as: "Each column contains a reading, each row is a sample"
  ripensense_1day_demo.csv     : 1-day demo narrative run

Key improvements over v1:
  - 50 randomized runs across 8 scenario types (~72k raw rows, ~14k EI windows)
  - Randomized event timing, magnitude, and initial RI per run
  - "Near miss" scenario class (approaches threshold, recovers)
  - Stage-aware Q10 that updates as RI accumulates
  - Flat EI export: no run_id/scenario_type, no timestamp, just features + label
  - Excess-above-optimal RI formula (calibrated: normal 24h -> RI~9, bad 24h -> RI~100)
"""

import numpy as np
import pandas as pd
import math
from dataclasses import dataclass
from typing import List, Tuple

RNG = np.random.default_rng(42)

# ---------------------------------------------------------------------------
# Physical constants (from RipenSense meeting notes)
# ---------------------------------------------------------------------------
ETHYLENE_THRESHOLD_PPB  = 100.0
TEMP_REF                = 13.0
TEMP_OPTIMAL            = 13.5
HUMIDITY_OPTIMAL        = 92.5
SHOCK_THRESHOLD_G       = 10.0
SHOCK_RI_INCREMENT      = 3.0
SENSOR_THERMAL_TAU_MIN  = 7.0
ETHYLENE_DIFFUSION_LAG  = 20.0   # minutes before shock ethylene appears
ETHYLENE_RISE_TAU       = 30.0   # minutes, exponential rise time constant

STAGES = [
    (1, 2.0,  2.5,   0,  14),
    (2, 3.5,  4.0,  14,  28),
    (3, 3.0,  3.0,  28,  42),
    (4, 2.5,  2.5,  42,  56),
    (5, 2.0,  2.0,  56,  70),
    (6, 1.8,  1.8,  70,  85),
    (7, 1.5,  1.5,  85, 100),
]

def get_stage(ri: float) -> Tuple[int, float]:
    for snum, q10_lo, q10_hi, ri_min, ri_max in STAGES:
        if ri_min <= ri < ri_max:
            return snum, (q10_lo + q10_hi) / 2.0
    return 7, 1.5

def vp_sat(t: float) -> float:
    return 0.61078 * math.exp(17.27 * t / (t + 237.3))

def vpd(t: float, rh: float) -> float:
    return vp_sat(t) * (1.0 - rh / 100.0)

# Calibrated weights
W_E = 0.04
W_T = 0.08
W_H = 0.01
_VPD_OPTIMAL = vpd(TEMP_OPTIMAL, HUMIDITY_OPTIMAL)

def ri_delta(temp_c: float, rh: float, ethylene_ppb: float, q10: float) -> float:
    """RI increment per minute using excess-above-optimal formulation."""
    e_term = W_E * max(0.0, ethylene_ppb / ETHYLENE_THRESHOLD_PPB)
    t_term = W_T * max(0.0, (q10 ** ((temp_c - TEMP_REF) / 10.0)) - 1.0)
    h_term = W_H * max(0.0, vpd(temp_c, rh) - _VPD_OPTIMAL)
    return e_term + t_term + h_term

# ---------------------------------------------------------------------------
# GPS route: Newark DC -> Holland Tunnel -> Manhattan stops
# ---------------------------------------------------------------------------
ROUTE_WAYPOINTS = [
    (40.7282, -74.1726),
    (40.7282, -74.1200),
    (40.7282, -74.0776),
    (40.7282, -74.0197),
    (40.7260, -74.0060),
    (40.7209, -74.0052),
    (40.7157, -74.0090),
    (40.7074, -74.0113),
]

def interpolate_route(n: int) -> Tuple[np.ndarray, np.ndarray]:
    wpts = ROUTE_WAYPOINTS
    stop = max(1, n // (len(wpts) * 4))
    travel = max(1, (n - stop * len(wpts)) // (len(wpts) - 1))
    coords = []
    for i in range(len(wpts) - 1):
        la0, lo0 = wpts[i]; la1, lo1 = wpts[i+1]
        for t in range(travel):
            f = t / max(travel - 1, 1)
            coords.append((la0 + f*(la1-la0) + RNG.normal(0, 0.0001),
                           lo0 + f*(lo1-lo0) + RNG.normal(0, 0.0001)))
        for _ in range(stop):
            coords.append((la1 + RNG.normal(0, 0.00005),
                           lo1 + RNG.normal(0, 0.00005)))
    while len(coords) < n:
        coords.append(coords[-1])
    coords = coords[:n]
    return np.array([c[0] for c in coords]), np.array([c[1] for c in coords])

# ---------------------------------------------------------------------------
# Shock aggregator
# ---------------------------------------------------------------------------
def aggregate_shocks(n: int, events: list) -> Tuple[np.ndarray, np.ndarray]:
    max_g  = np.ones(n)
    counts = np.zeros(n, dtype=int)
    for minute, peak_g in events:
        if minute >= n:
            continue
        max_g[minute] = max(max_g[minute], peak_g)
        if peak_g > SHOCK_THRESHOLD_G:
            counts[minute] += 1
        for j in range(1, 4):
            if minute + j < n:
                max_g[minute+j] = max(max_g[minute+j], 1.0 + peak_g * (0.3**j))
    max_g += RNG.uniform(0.05, 0.3, n)
    return max_g, counts

# ---------------------------------------------------------------------------
# Scenario event
# ---------------------------------------------------------------------------
@dataclass
class Event:
    kind:         str
    start_min:    int
    duration_min: int
    magnitude:    float

# ---------------------------------------------------------------------------
# Core simulator
# ---------------------------------------------------------------------------
def simulate(n_min: int, initial_ri: float, events: List[Event],
             base_temp: float = 13.5, base_humidity: float = 92.0,
             base_ethylene: float = 2.0, tod_offset_hr: float = 6.0) -> pd.DataFrame:

    true_temp = np.full(n_min, base_temp, dtype=float)
    true_hum  = np.full(n_min, base_humidity, dtype=float)
    true_eth  = np.full(n_min, base_ethylene, dtype=float)
    shock_events = []

    for ev in events:
        s = ev.start_min
        e = min(s + ev.duration_min, n_min)

        if ev.kind == "temp_drift":
            ramp = np.linspace(0, ev.magnitude, e - s)
            true_temp[s:e] += ramp
            for i in range(min(120, n_min - e)):
                true_temp[e+i] = base_temp + ev.magnitude * math.exp(-i / max(ev.duration_min*0.5, 1))

        elif ev.kind == "temp_spike":
            true_temp[s:e] += ev.magnitude
            for i in range(min(90, n_min - e)):
                true_temp[e+i] = base_temp + ev.magnitude * math.exp(-i / 20.0)

        elif ev.kind == "humidity_drop":
            drop = np.linspace(0, ev.magnitude, e - s)
            true_hum[s:e] = np.clip(true_hum[s:e] - drop, 50, 100)
            for i in range(min(120, n_min - e)):
                true_hum[e+i] = base_humidity - ev.magnitude * math.exp(-i / 30.0)

        elif ev.kind == "shock":
            n_imp = int(RNG.integers(1, 4))
            for _ in range(n_imp):
                t = s + int(RNG.integers(0, max(1, ev.duration_min)))
                g = ev.magnitude * RNG.uniform(0.7, 1.0)
                shock_events.append((min(t, n_min-1), float(g)))
            # Ethylene rise after diffusion lag
            for i in range(n_min - s):
                lag = ETHYLENE_DIFFUSION_LAG
                if i > lag:
                    ppb_spike = (ev.magnitude / 15.0) * 80.0
                    rise  = ppb_spike * (1 - math.exp(-(i - lag) / ETHYLENE_RISE_TAU))
                    decay = math.exp(-max(0, i - lag - ETHYLENE_RISE_TAU) / 120.0)
                    true_eth[s+i] += rise * decay

        elif ev.kind == "ethylene_contamination":
            for i in range(n_min - s):
                if i < ev.duration_min:
                    true_eth[s+i] += ev.magnitude
                else:
                    true_eth[s+i] += ev.magnitude * math.exp(-(i - ev.duration_min) / 60.0)

        elif ev.kind == "near_miss_temp":
            # Temp approaches danger zone then recovers before real damage
            peak_min = ev.duration_min // 2
            for i in range(e - s):
                frac = math.sin(math.pi * i / max(e - s - 1, 1))
                true_temp[s+i] += ev.magnitude * frac

        elif ev.kind == "near_miss_ethylene":
            # Ethylene approaches threshold, ventilation clears it
            for i in range(e - s):
                frac = math.sin(math.pi * i / max(e - s - 1, 1))
                true_eth[s+i] += ev.magnitude * frac

    # Diurnal variation (~0.5C swing)
    hours = (tod_offset_hr + np.arange(n_min) / 60.0) % 24.0
    true_temp += 0.5 * np.sin(2 * math.pi * (hours - 6) / 24.0)

    true_temp = np.clip(true_temp, -5, 40)
    true_hum  = np.clip(true_hum, 0, 100)
    true_eth  = np.clip(true_eth, 0, 5000)

    # Sensor lag (SHT31 thermal time constant)
    alpha_t = 1.0 - math.exp(-1.0 / SENSOR_THERMAL_TAU_MIN)
    alpha_h = 1.0 - math.exp(-1.0 / (SENSOR_THERMAL_TAU_MIN * 0.5))
    s_temp = np.zeros(n_min); s_temp[0] = true_temp[0]
    s_hum  = np.zeros(n_min); s_hum[0]  = true_hum[0]
    for i in range(1, n_min):
        s_temp[i] = s_temp[i-1] + alpha_t * (true_temp[i] - s_temp[i-1])
        s_hum[i]  = s_hum[i-1]  + alpha_h * (true_hum[i]  - s_hum[i-1])

    s_temp += RNG.normal(0, 0.15, n_min)
    s_hum  += RNG.normal(0, 0.5,  n_min)
    s_eth   = np.clip(true_eth + RNG.normal(0, 2.0, n_min), 0, None)
    probe   = s_temp + RNG.normal(0.3, 0.2, n_min)

    max_g, shock_cnt = aggregate_shocks(n_min, shock_events)

    # RI integration
    ri        = np.zeros(n_min); ri[0] = initial_ri
    stage_arr = np.zeros(n_min, dtype=int)
    stage_arr[0] = get_stage(initial_ri)[0]

    for i in range(1, n_min):
        snum, q10 = get_stage(ri[i-1])
        stage_arr[i] = snum
        delta = ri_delta(s_temp[i], s_hum[i], s_eth[i], q10)
        delta += shock_cnt[i] * SHOCK_RI_INCREMENT
        if s_temp[i] < 11.0:
            delta += 0.02 * (11.0 - s_temp[i])
        ri[i] = np.clip(ri[i-1] + delta, 0, 100)

    anomaly = np.clip(ri / 100.0, 0, 1)
    vpd_arr = np.array([vpd(s_temp[i], s_hum[i]) for i in range(n_min)])
    lats, lons = interpolate_route(n_min)

    start = pd.Timestamp("2025-05-01 06:00:00", tz="America/New_York")
    timestamps = [start + pd.Timedelta(minutes=int(i)) for i in range(n_min)]
    ts_ms = [int(i * 60 * 1000) for i in range(n_min)]  # ms from start, for EI

    return pd.DataFrame({
        "timestamp":            timestamps,
        "timestamp_ms":         ts_ms,
        "minutes_elapsed":      np.arange(n_min),
        "latitude":             np.round(lats, 6),
        "longitude":            np.round(lons, 6),
        "temp_c":               np.round(s_temp, 2),
        "humidity_rh":          np.round(s_hum, 2),
        "ethylene_ppb":         np.round(s_eth, 2),
        "probe_temp_c":         np.round(probe, 2),
        "max_g_last_60s":       np.round(max_g, 3),
        "shock_count_last_60s": shock_cnt,
        "vpd_kpa":              np.round(vpd_arr, 4),
        "stage":                stage_arr,
        "ri_cumulative":        np.round(ri, 3),
        "anomaly_score":        np.round(anomaly, 4),
    })

# ---------------------------------------------------------------------------
# Randomized scenario builders
# ---------------------------------------------------------------------------
def r(lo, hi): return float(RNG.uniform(lo, hi))
def ri(lo, hi): return int(RNG.integers(lo, hi))

def scenario_normal(n=1440) -> pd.DataFrame:
    events = []
    # Occasional minor road bumps, never above threshold
    for _ in range(RNG.integers(1, 4)):
        events.append(Event("shock", ri(0, n-10), ri(1,4), r(4, 9)))
    return simulate(n, r(1, 6), events,
                    base_temp=r(13.0, 14.0), base_humidity=r(91, 94))

def scenario_near_miss(n=1440) -> pd.DataFrame:
    """Conditions approach danger but recover. Anomaly stays < 0.3."""
    events = [
        Event("near_miss_temp",     ri(60, 300),  ri(60,120), r(4, 6)),
        Event("near_miss_ethylene", ri(200, 500), ri(60,120), r(60, 90)),
        Event("shock",              ri(100, 400), ri(1,3),    r(5, 9)),
    ]
    return simulate(n, r(1, 5), events,
                    base_temp=r(13.0, 14.5), base_humidity=r(90, 94))

def scenario_temp_drift(n=1440) -> pd.DataFrame:
    start = ri(60, 300)
    events = [
        Event("temp_drift", start, ri(120, 300), r(5, 9)),
        Event("shock", ri(0, start), ri(1,3), r(4, 10)),
    ]
    return simulate(n, r(1, 8), events, base_temp=r(13, 14))

def scenario_temp_spike(n=1440) -> pd.DataFrame:
    events = [
        Event("temp_spike", ri(100, 600), ri(30, 90), r(6, 11)),
        Event("shock",      ri(50, 300),  ri(1, 3),   r(8, 15)),
    ]
    return simulate(n, r(2, 10), events)

def scenario_rough_handling(n=1440) -> pd.DataFrame:
    events = []
    n_shocks = int(RNG.integers(3, 7))
    for _ in range(n_shocks):
        events.append(Event("shock", ri(10, n-10), ri(2, 8), r(12, 24)))
    return simulate(n, r(1, 6), events)

def scenario_humidity_drop(n=1440) -> pd.DataFrame:
    start = ri(60, 400)
    events = [
        Event("humidity_drop", start, ri(90, 240), r(8, 15)),
        Event("shock", ri(0, start), ri(1,3), r(4, 8)),
    ]
    return simulate(n, r(1, 8), events, base_humidity=r(90, 94))

def scenario_ethylene_contamination(n=1440) -> pd.DataFrame:
    start = ri(60, 400)
    events = [
        Event("ethylene_contamination", start, ri(60, 180), r(120, 250)),
        Event("shock", ri(0, n-10), ri(1,3), r(4, 9)),
    ]
    return simulate(n, r(1, 6), events)

def scenario_compound_failure(n=1440) -> pd.DataFrame:
    t_start = ri(60, 200)
    s_start = ri(60, 300)
    events = [
        Event("temp_drift",    t_start,       ri(120, 240), r(5, 9)),
        Event("shock",         s_start,        ri(3, 8),     r(14, 24)),
        Event("shock",         ri(300, 700),   ri(2, 6),     r(15, 22)),
        Event("humidity_drop", ri(200, 500),   ri(90, 180),  r(8, 14)),
        Event("shock",         ri(500, 900),   ri(2, 5),     r(16, 25)),
    ]
    return simulate(n, r(3, 15), events,
                    base_temp=r(13.5, 15.0), base_humidity=r(89, 93))

# ---------------------------------------------------------------------------
# Training dataset: 50 runs
# ---------------------------------------------------------------------------
SCENARIO_PLAN = [
    ("normal",                  8,  scenario_normal),
    ("near_miss",               7,  scenario_near_miss),
    ("temp_drift",              7,  scenario_temp_drift),
    ("temp_spike",              6,  scenario_temp_spike),
    ("rough_handling",          7,  scenario_rough_handling),
    ("humidity_drop",           5,  scenario_humidity_drop),
    ("ethylene_contamination",  5,  scenario_ethylene_contamination),
    ("compound_failure",        5,  scenario_compound_failure),
]
# Total: 8+7+7+6+7+5+5+5 = 50 runs

def make_training_dataset() -> pd.DataFrame:
    dfs = []
    run_id = 0
    for label, count, builder in SCENARIO_PLAN:
        for _ in range(count):
            df = builder()
            df.insert(0, "run_id",        run_id)
            df.insert(1, "scenario_type", label)
            dfs.append(df)
            run_id += 1
    combined = pd.concat(dfs, ignore_index=True)
    print(f"Training dataset: {len(combined):,} rows across {run_id} runs")
    print(f"  anomaly mean={combined.anomaly_score.mean():.3f}  "
          f"max={combined.anomaly_score.max():.3f}  "
          f"pct>0.5={( combined.anomaly_score > 0.5).mean()*100:.1f}%  "
          f"pct>0.25={(combined.anomaly_score > 0.25).mean()*100:.1f}%")
    return combined

# ---------------------------------------------------------------------------
# Edge Impulse windowed export
# Feature columns only (no GPS, no timestamps, no labels except anomaly_score)
# Window: 30 rows (30 minutes), stride: 5
# EI format: each window is one sample row with flattened features + label
# ---------------------------------------------------------------------------
FEATURE_COLS = [
    "temp_c", "humidity_rh", "ethylene_ppb", "probe_temp_c",
    "max_g_last_60s", "shock_count_last_60s", "vpd_kpa",
    "stage", "ri_cumulative",
]
WINDOW_SIZE = 30
STRIDE      = 5

def make_ei_dataset(train_df: pd.DataFrame) -> pd.DataFrame:
    """
    Slide a 30-minute window over each run and produce a flat feature table.

    Output format (Edge Impulse regression, flat sample):
      - One row per window
      - 270 feature columns (9 sensors x 30 timesteps), named feat_t0..feat_t29
      - Final column: anomaly_score (label)
      - NO run_id, scenario_type, or timestamps (EI treats those as features otherwise)

    Upload to EI as: "Each column contains a reading, each row contains a full sample"
    """
    records = []
    for run_id, group in train_df.groupby("run_id"):
        group = group.reset_index(drop=True)
        feats  = group[FEATURE_COLS].values   # (1440, 9)
        labels = group["anomaly_score"].values
        n = len(group)

        for start in range(0, n - WINDOW_SIZE + 1, STRIDE):
            window = feats[start : start + WINDOW_SIZE]   # (30, 9)
            label  = labels[start + WINDOW_SIZE - 1]
            flat   = window.T.flatten()                   # (270,) sensor-major order
            records.append(flat.tolist() + [label])

    # Column names: temp_c_t0..t29, humidity_rh_t0..t29, ..., anomaly_score
    feat_names = [f"{col}_t{t}" for col in FEATURE_COLS for t in range(WINDOW_SIZE)]
    cols = feat_names + ["anomaly_score"]
    df = pd.DataFrame(records, columns=cols)

    print(f"Edge Impulse flat dataset: {len(df):,} windows  "
          f"({WINDOW_SIZE}-min window, stride {STRIDE})")
    print(f"  Feature columns: {len(feat_names)}  (9 features x 30 timesteps)")
    print(f"  Label range: {df.anomaly_score.min():.3f} - {df.anomaly_score.max():.3f}")
    print(f"  Label mean:  {df.anomaly_score.mean():.3f}  std: {df.anomaly_score.std():.3f}")
    return df

# ---------------------------------------------------------------------------
# Demo run
# ---------------------------------------------------------------------------
def make_demo_run() -> pd.DataFrame:
    n = 24 * 60
    events = [
        Event("shock",                  180,  8,   22.0),
        Event("temp_spike",             300,  90,   9.0),
        Event("shock",                  480,  5,   17.0),
        Event("ethylene_contamination", 540,  120, 180.0),
        Event("humidity_drop",          660,  120,  12.0),
        Event("shock",                  900,  5,   19.0),
    ]
    df = simulate(n, 2.0, events, base_temp=14.0, base_humidity=91.0)
    df.insert(0, "run_id",        0)
    df.insert(1, "scenario_type", "demo")
    return df

# ---------------------------------------------------------------------------
# VISUALIZATION (Matplotlib diagnostics)
# ---------------------------------------------------------------------------
import matplotlib.pyplot as plt

def plot_single_run(df: pd.DataFrame, title: str = "Single Run Overview"):
    fig, axs = plt.subplots(4, 1, figsize=(14, 10), sharex=True)

    axs[0].plot(df["temp_c"], label="Sensor Temp")
    axs[0].plot(df["probe_temp_c"], label="Probe Temp", alpha=0.6)
    axs[0].set_ylabel("Temp (C)")
    axs[0].legend()

    axs[1].plot(df["humidity_rh"], label="Humidity", color="tab:blue")
    axs[1].set_ylabel("RH (%)")
    axs[1].legend()

    axs[2].plot(df["ethylene_ppb"], label="Ethylene", color="tab:orange")
    axs[2].set_ylabel("ppb")
    axs[2].legend()

    axs[3].plot(df["ri_cumulative"], label="RI", color="tab:red")
    axs[3].plot(df["anomaly_score"] * 100, label="Anomaly (scaled)", alpha=0.6)
    axs[3].set_ylabel("RI / Anomaly")
    axs[3].set_xlabel("Minute")
    axs[3].legend()

    plt.suptitle(title)
    plt.tight_layout()
    plt.show()


def plot_scenario_comparison(train_df: pd.DataFrame):
    summary = train_df.groupby("scenario_type")["anomaly_score"].mean().sort_values()

    plt.figure(figsize=(10, 4))
    plt.bar(summary.index, summary.values)
    plt.xticks(rotation=45, ha="right")
    plt.ylabel("Mean anomaly score")
    plt.title("Scenario Risk Comparison")
    plt.tight_layout()
    plt.show()


def plot_ri_distribution(train_df: pd.DataFrame):
    plt.figure(figsize=(8, 4))
    plt.hist(train_df["ri_cumulative"], bins=50)
    plt.xlabel("RI")
    plt.ylabel("Frequency")
    plt.title("RI Distribution Across All Runs")
    plt.tight_layout()
    plt.show()


def plot_ethylene_vs_ri(train_df: pd.DataFrame):
    plt.figure(figsize=(6, 5))
    plt.scatter(train_df["ethylene_ppb"], train_df["ri_cumulative"], s=1, alpha=0.2)
    plt.xlabel("Ethylene (ppb)")
    plt.ylabel("RI")
    plt.title("Ethylene vs Ripeness Index")
    plt.tight_layout()
    plt.show()

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    print("RipenSense Data Generator v2\n")

    print("=== Training dataset (50 runs) ===")
    train = make_training_dataset()
    train_path = "./outputs/ripensense_train_raw.csv"
    train.to_csv(train_path, index=False)
    print(f"  Saved: {train_path}\n")

    print("=== Edge Impulse flat dataset ===")
    ei = make_ei_dataset(train)
    ei_path = "./outputs/ripensense_ei_flat.csv"
    ei.to_csv(ei_path, index=False)
    print(f"  Saved: {ei_path}\n")

    print("=== Demo run (1 day) ===")
    demo = make_demo_run()
    demo_path = "./outputs/ripensense_1day_demo.csv"
    demo.to_csv(demo_path, index=False)
    print(f"  Demo rows: {len(demo):,}")
    print(f"  RI final: {demo.ri_cumulative.iloc[-1]:.1f}/100")
    print(f"  Anomaly peak: {demo.anomaly_score.max():.3f}  final: {demo.anomaly_score.iloc[-1]:.3f}")
    print(f"  Final stage: {demo.stage.iloc[-1]}")
    print(f"  Saved: {demo_path}\n")

    # Scenario breakdown
    print("=== Scenario breakdown (training) ===")
    summary = train.groupby("scenario_type").agg(
        runs       =("run_id",        "nunique"),
        rows       =("run_id",        "count"),
        anomaly_mean=("anomaly_score", "mean"),
        anomaly_max =("anomaly_score", "max"),
    ).round(3)
    print(summary.to_string())

    demo_plot = make_demo_run()
    plot_single_run(demo_plot, "Demo Run: Sensor + RI Dynamics")

    plot_scenario_comparison(train)
    plot_ri_distribution(train)
    plot_ethylene_vs_ri(train)
