import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.cm as cm

# --- Parameters ---
T_ref = 13.0  # Goldilocks temperature
Q10_stages = [2.0, 3.5, 3.0, 2.5, 2.0, 1.8, 1.5]  # Q10 per ripening stage
RI_max = 100
ethylene_threshold = 0.1  # ppm
shock_threshold = 10  # G
time_step = 1  # hours
total_hours = 168  # simulate 7 days
n_bananas = 10

# Weights
WE = 0.5  # ethylene
WT = 0.3  # temp/Q10
WH = 0.1  # VPD/humidity
WS = 0.1  # shock

# Tetens formula for saturation vapor pressure (kPa)
def VPsat(T_C):
    return 0.61078 * np.exp(17.27 * T_C / (T_C + 237.3))

# VPD computation
def VPD(T_C, RH_percent):
    return VPsat(T_C) * (1 - RH_percent / 100)

# Generate temperature profile
def gen_temp_profile(failure=False):
    if not failure:
        return np.full(total_hours, 13.0) + np.random.normal(0, 0.2, total_hours)
    else:
        # temperature gradually rises to 20C over 4 hours at some point
        profile = np.full(total_hours, 13.0)
        start = np.random.randint(24, total_hours - 24)
        for i in range(4):
            profile[start + i] = 13 + (20 - 13) * (i + 1)/4
        profile += np.random.normal(0, 0.5, total_hours)
        return profile

# Generate humidity profile
def gen_humidity_profile():
    return 95 - np.random.normal(0, 2, total_hours)  # 90-95% RH

# Generate ethylene profile based on ripeness index and shocks
def gen_ethylene_profile(RI, shocks):
    ethylene = np.zeros(total_hours)
    for t in range(1, total_hours):
        # Ethylene spikes when RI > threshold or shocks
        ethylene[t] = ethylene[t-1]
        if RI[t-1] > 20 or shocks[t-1] > 0:
            ethylene[t] += 0.01 + 0.05 * (RI[t-1]/RI_max)  # simplistic growth
        # Cap ethylene
        ethylene[t] = min(ethylene[t], 10.0)
    return ethylene

# Generate shock profile
def gen_shock_profile():
    shocks = np.zeros(total_hours)
    # Randomly insert shocks
    n_shocks = np.random.randint(0, 5)
    for _ in range(n_shocks):
        t = np.random.randint(0, total_hours)
        shocks[t] = shock_threshold + np.random.uniform(0, 5)
    return shocks

# Simulate ripeness index
def compute_RI(temp, humidity, ethylene, shocks):
    RI = np.zeros(total_hours)
    for t in range(1, total_hours):
        # Temperature factor using Q10 approximation
        temp_factor = WT * Q10_stages[min(int(RI[t-1]/(RI_max/7)), 6)] ** ((temp[t-1]-T_ref)/10)
        # VPD/humidity effect
        vpd_factor = WH * VPD(temp[t-1], humidity[t-1])
        # Ethylene contribution
        eth_factor = WE * max(0, ethylene[t-1] - ethylene_threshold)
        # Shock contribution
        shock_factor = WS * shocks[t-1]
        # Increment RI
        RI[t] = RI[t-1] + temp_factor + vpd_factor + eth_factor + shock_factor
        RI[t] = min(RI[t], RI_max)
    return RI

# Generate dataset
all_data = []
for b in range(n_bananas):
    temp = gen_temp_profile(failure=np.random.rand()<0.2)
    humidity = gen_humidity_profile()
    shocks = gen_shock_profile()
    RI = compute_RI(temp, humidity, np.zeros(total_hours), shocks)
    ethylene = gen_ethylene_profile(RI, shocks)
    
    for t in range(total_hours):
        all_data.append({
            'banana_id': b,
            'hour': t,
            'temp_C': temp[t],
            'humidity_percent': humidity[t],
            'shock_G': shocks[t],
            'ethylene_ppm': ethylene[t],
            'RI': RI[t]
        })

df = pd.DataFrame(all_data)
df.to_csv('synthetic_banana_data.csv', index=False)
print("Synthetic dataset generated with shape:", df.shape)

# --- Visualisations ---
hours = np.arange(total_hours)
colors = cm.tab10(np.linspace(0, 1, n_bananas))

fig, axes = plt.subplots(2, 2, figsize=(14, 10))
fig.suptitle('Banana Ripening Simulation — 7 Days', fontsize=14, fontweight='bold')

# 1. Temperature profiles
ax = axes[0, 0]
for b in range(n_bananas):
    d = df[df['banana_id'] == b]
    ax.plot(d['hour'], d['temp_C'], color=colors[b], alpha=0.7, linewidth=1, label=f'Banana {b}')
ax.axhline(T_ref, color='black', linestyle='--', linewidth=1, label=f'T_ref={T_ref}°C')
ax.set_title('Temperature Profiles')
ax.set_xlabel('Hour')
ax.set_ylabel('Temperature (°C)')
ax.legend(fontsize=6, ncol=2)

# 2. Ripeness Index over time
ax = axes[0, 1]
for b in range(n_bananas):
    d = df[df['banana_id'] == b]
    ax.plot(d['hour'], d['RI'], color=colors[b], alpha=0.7, linewidth=1, label=f'Banana {b}')
ax.set_title('Ripeness Index (RI) Over Time')
ax.set_xlabel('Hour')
ax.set_ylabel('RI (0–100)')
ax.legend(fontsize=6, ncol=2)

# 3. Ethylene levels
ax = axes[1, 0]
for b in range(n_bananas):
    d = df[df['banana_id'] == b]
    ax.plot(d['hour'], d['ethylene_ppm'], color=colors[b], alpha=0.7, linewidth=1, label=f'Banana {b}')
ax.axhline(ethylene_threshold, color='red', linestyle='--', linewidth=1, label=f'Threshold ({ethylene_threshold} ppm)')
ax.set_title('Ethylene Concentration')
ax.set_xlabel('Hour')
ax.set_ylabel('Ethylene (ppm)')
ax.legend(fontsize=6, ncol=2)

# 4. Final RI distribution + shock events
ax = axes[1, 1]
final_RI = [df[df['banana_id'] == b]['RI'].iloc[-1] for b in range(n_bananas)]
shock_counts = [int((df[df['banana_id'] == b]['shock_G'] > 0).sum()) for b in range(n_bananas)]
bars = ax.bar(range(n_bananas), final_RI, color=colors, alpha=0.8, label='Final RI')
ax2 = ax.twinx()
ax2.scatter(range(n_bananas), shock_counts, color='red', zorder=5, label='Shock events')
ax2.set_ylabel('# Shock Events', color='red')
ax2.tick_params(axis='y', labelcolor='red')
ax.set_title('Final Ripeness Index & Shocks per Banana')
ax.set_xlabel('Banana ID')
ax.set_ylabel('Final RI')
ax.set_xticks(range(n_bananas))
lines1, labels1 = ax.get_legend_handles_labels()
lines2, labels2 = ax2.get_legend_handles_labels()
ax.legend(lines1 + lines2, labels1 + labels2, fontsize=8)

plt.tight_layout()
plt.savefig('banana_simulation.png', dpi=150)
plt.show()
print("Plot saved to banana_simulation.png")