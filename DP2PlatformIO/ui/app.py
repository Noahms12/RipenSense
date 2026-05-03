import streamlit as st
import pandas as pd
import numpy as np
import plotly.express as px
import plotly.graph_objects as go
from plotly.subplots import make_subplots
from datetime import datetime, timedelta

st.set_page_config(page_title="Banana Tracker Dashboard", layout="wide")

# --- 1. Generate Asynchronous Data ---
@st.cache_data
def generate_async_data():
    """Simulates high-frequency shock data and low-frequency environmental data."""
    now = datetime.now()
    
    # 1. LOW FREQUENCY DATA (Every 10 minutes - 50 points)
    slow_times = [now - timedelta(minutes=10 * i) for i in range(50)]
    slow_times.reverse()
    
    df_slow = pd.DataFrame({
        'Timestamp': slow_times,
        'Latitude': [40.7128 + (i * 0.001) for i in range(50)],
        'Longitude': [-74.0060 + (i * 0.0015) for i in range(50)],
        'Temp_C': [20.0 + np.random.normal(0, 0.5) for _ in range(40)] + [25, 28, 30, 32, 34, 35, 35, 35, 35, 35],
        'Gas_ppm': [10 + np.random.normal(0, 2) for _ in range(40)] + [40, 90, 150, 250, 400, 500, 550, 580, 600, 600],
        'AI_Risk': [0.05 for _ in range(40)] + [0.2, 0.4, 0.6, 0.8, 0.9, 0.95, 0.98, 0.99, 1.0, 1.0]
    })

    # 2. HIGH FREQUENCY DATA (Every 1 minute - 500 points)
    # This simulates the MPU6050 taking many more readings between the GPS/Temp updates
    fast_times = [now - timedelta(minutes=1 * i) for i in range(500)]
    fast_times.reverse()
    
    # Baseline vibration is 1G. We inject a sudden 45G drop at point 480.
    shocks = [abs(np.random.normal(1.0, 0.2)) for _ in range(500)]
    shocks[480] = 45.0 
    shocks[481] = 12.0 # bounce
    shocks[482] = 4.0  # settle
    
    df_fast = pd.DataFrame({
        'Timestamp': fast_times,
        'Shock_G': shocks
    })
    
    return df_slow, df_fast

df_slow, df_fast = generate_async_data()
latest = df_slow.iloc[-1]
latest_shock = df_fast['Shock_G'].max() # Get max recent shock for the metric

# --- 2. Build the UI ---
st.title("Banana Spoilage Tracker")

col1, col2, col3, col4 = st.columns(4)
col1.metric("AI Spoilage Risk", f"{latest['AI_Risk']:.2f}")
col2.metric("Current Temp", f"{latest['Temp_C']:.1f} °C")
col3.metric("Gas Level", f"{int(latest['Gas_ppm'])} ppm")
col4.metric("Max Recent Shock", f"{latest_shock:.1f} G", delta="IMPACT DETECTED" if latest_shock > 30 else "Normal", delta_color="inverse")

st.markdown("---")

# Map uses the slow data (GPS coordinates)
st.subheader("📍 Journey Map")
fig_map = px.scatter_mapbox(
    df_slow, 
    lat="Latitude", 
    lon="Longitude", 
    color="AI_Risk",
    range_color=[0, 1], 
    zoom=11,
    color_continuous_scale=px.colors.sequential.Reds, 
    hover_name="Timestamp",
    hover_data={
        "Latitude": False, 
        "Longitude": False, 
        "AI_Risk": ":.2f",
        "Temp_C": ":.1f",
        "Gas_ppm": True
    }
)
fig_map.update_layout(mapbox_style="open-street-map", margin={"r":0,"t":0,"l":0,"b":0})
st.plotly_chart(fig_map, use_container_width=True)

st.markdown("---")

# --- 3. The Shared X-Axis Master Chart ---
st.subheader("📈 Multi-Rate Sensor Telemetry")
st.markdown("Zoom in on the shock spike. Notice how the temperature and gas charts zoom to the exact same timeframe, despite having far fewer data points.")

# Create a figure with 3 stacked rows, sharing the time axis
fig_master = make_subplots(
    rows=3, cols=1, 
    shared_xaxes=True,
    vertical_spacing=0.05,
    subplot_titles=("AI Spoilage Risk", "Environmental (Temp & Gas)", "MPU6050 Impact Forces (High Freq)")
)

# Row 1: AI Risk (Slow Data)
fig_master.add_trace(go.Scatter(x=df_slow['Timestamp'], y=df_slow['AI_Risk'], name="AI Risk", line=dict(color="red", width=3)), row=1, col=1)

# Row 2: Temp & Gas (Slow Data) - Plotted on the same graph for density
fig_master.add_trace(go.Scatter(x=df_slow['Timestamp'], y=df_slow['Temp_C'], name="Temp (C)", line=dict(color="#d62728")), row=2, col=1)
fig_master.add_trace(go.Scatter(x=df_slow['Timestamp'], y=df_slow['Gas_ppm'], name="Gas (ppm)", yaxis="y2", line=dict(color="#9467bd")), row=2, col=1)

# Row 3: Shock (Fast Data) - Notice it uses entirely different timestamps
fig_master.add_trace(go.Scatter(x=df_fast['Timestamp'], y=df_fast['Shock_G'], name="Shock (G)", line=dict(color="#ff7f0e")), row=3, col=1)

# Format the layout to be clean and tall
fig_master.update_layout(height=800, hovermode="x unified")
fig_master.update_yaxes(title_text="Risk (0-1)", row=1, col=1)
fig_master.update_yaxes(title_text="Temp (C)", row=2, col=1)
fig_master.update_yaxes(title_text="Force (G)", row=3, col=1)

st.plotly_chart(fig_master, use_container_width=True)