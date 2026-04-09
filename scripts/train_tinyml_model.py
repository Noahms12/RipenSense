#!/usr/bin/env python3
"""
RipenSense TinyML Model Training
================================
Trains an Autoencoder anomaly detection model on synthetic banana ripeness data.
Outputs a TensorFlow Lite Micro model for embedding in nRF52840 firmware.

Usage:
  python3 train_tinyml_model.py
"""

import os
import numpy as np
import pandas as pd
from sklearn.preprocessing import StandardScaler
import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import layers, Model
import tempfile
import subprocess

# ============================================================================
# CONFIGURATION
# ============================================================================

DATA_FILE = "synthetic_banana_data.csv"
OUTPUT_MODEL = "model.tflite"
OUTPUT_HEADER = "model_data.h"
RANDOM_SEED = 42

# Model parameters
LATENT_DIM = 8
TIME_WINDOW = 15  # 15 timesteps (150 seconds at 10s intervals)
TRAIN_SPLIT = 0.8
EPOCHS = 50
BATCH_SIZE = 16
VALIDATION_SPLIT = 0.2

# Quantization for TFLite Micro
TARGET_MODEL_SIZE_KB = 200

# ============================================================================
# LOAD & PREPROCESS DATA
# ============================================================================

def load_and_preprocess_data(filepath):
    """Load synthetic data and prepare for training."""
    print("[*] Loading data from", filepath)
    df = pd.read_csv(filepath)
    print(f"    Loaded {len(df)} samples")
    
    # Select feature columns (exclude banana_id, hour, RI which is label)
    feature_cols = ["temp_C", "humidity_percent", "shock_G", "ethylene_ppm"]
    X = df[feature_cols].values
    
    print(f"    Features: {feature_cols}")
    print(f"    Anomaly label (RI): {df['RI'].min():.2f} - {df['RI'].max():.2f}")
    
    # Normalize features
    print("[*] Normalizing features...")
    scaler = StandardScaler()
    X_scaled = scaler.fit_transform(X)
    
    print(f"    Mean: {X_scaled.mean(axis=0)}")
    print(f"    Std: {X_scaled.std(axis=0)}")
    
    return X_scaled, scaler, df


def create_time_windows(X, window_size=TIME_WINDOW):
    """Create sliding time windows for sequential modeling."""
    print(f"[*] Creating {window_size}-timestep windows...")
    windows = []
    for i in range(len(X) - window_size + 1):
        windows.append(X[i:i + window_size])
    
    X_windowed = np.array(windows)
    print(f"    Created {len(X_windowed)} windows of shape {X_windowed[0].shape}")
    
    return X_windowed


# ============================================================================
# BUILD AUTOENCODER MODEL
# ============================================================================

def build_autoencoder(input_shape, latent_dim=LATENT_DIM):
    """
    Build a lightweight Autoencoder for anomaly detection.
    
    Encoder: input -> dense layers -> latent vector
    Decoder: latent -> dense layers -> reconstruction
    
    Architecture designed to fit in ~100-200 KB for TFLite Micro.
    """
    print(f"[*] Building Autoencoder with latent_dim={latent_dim}")
    
    # Flatten input
    input_layer = keras.Input(shape=input_shape)
    x = layers.Flatten()(input_layer)
    
    # Encoder
    encoded = layers.Dense(32, activation="relu")(x)
    encoded = layers.Dense(16, activation="relu")(encoded)
    encoded = layers.Dense(latent_dim, activation="relu", name="latent")(encoded)
    
    # Decoder
    decoded = layers.Dense(16, activation="relu")(encoded)
    decoded = layers.Dense(32, activation="relu")(decoded)
    decoded = layers.Dense(np.prod(input_shape), activation="linear")(decoded)
    decoded = layers.Reshape(input_shape)(decoded)
    
    # Full autoencoder
    autoencoder = Model(input_layer, decoded)
    autoencoder.compile(optimizer="adam", loss="mse")
    
    print("    Model Summary:")
    autoencoder.summary()
    
    return autoencoder


# ============================================================================
# TRAINING
# ============================================================================

def train_autoencoder(X_windowed, model, epochs=EPOCHS):
    """Train the autoencoder on normal ripening data."""
    print(f"[*] Training Autoencoder ({epochs} epochs, batch_size={BATCH_SIZE})...")
    
    history = model.fit(
        X_windowed, X_windowed,
        epochs=epochs,
        batch_size=BATCH_SIZE,
        validation_split=VALIDATION_SPLIT,
        verbose=1
    )
    
    print("[*] Training complete!")
    print(f"    Final training loss: {history.history['loss'][-1]:.4f}")
    print(f"    Final validation loss: {history.history['val_loss'][-1]:.4f}")
    
    return history


# ============================================================================
# CONVERT TO TFLITE & QUANTIZE
# ============================================================================

def convert_to_tflite(model, output_file=OUTPUT_MODEL):
    """Convert Keras model to TensorFlow Lite with quantization."""
    print(f"[*] Converting model to TFLite...")
    
    # Convert to concrete function for accurate shape inference
    run_model = tf.function(lambda x: model(x))
    
    # Get representative dataset (use training data)
    # This helps with quantization calibration
    concrete_func = run_model.get_concrete_function(
        tf.TensorSpec(model.inputs[0].shape, model.inputs[0].dtype)
    )
    
    # Convert with quantization
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.target_spec.supported_ops = [
        tf.lite.OpsSet.TFLITE_BUILTINS
    ]
    
    # Set float32 as supported type (nRF52840 supports float ops)
    converter.target_spec.supported_types = [tf.float32]
    
    tflite_model = converter.convert()
    
    # Save model
    with open(output_file, "wb") as f:
        f.write(tflite_model)
    
    model_size_kb = len(tflite_model) / 1024
    print(f"    Model saved to {output_file}")
    print(f"    Model size: {model_size_kb:.1f} KB")
    
    if model_size_kb > TARGET_MODEL_SIZE_KB:
        print(f"    WARNING: Model exceeds target size of {TARGET_MODEL_SIZE_KB} KB!")
    
    return tflite_model


def generate_c_header(tflite_model, output_header=OUTPUT_HEADER):
    """Generate C++ header file for embedding model in firmware."""
    print(f"[*] Generating C++ header ({output_header})...")
    
    # Use xxd-like output
    c_code = "#ifndef MODEL_DATA_H\n"
    c_code += "#define MODEL_DATA_H\n\n"
    c_code += f"// Auto-generated model data ({len(tflite_model)} bytes)\n"
    c_code += "const unsigned char g_model_data[] = {\n"
    
    # Format as hex bytes (16 per line for readability)
    for i in range(0, len(tflite_model), 16):
        chunk = tflite_model[i:i+16]
        hex_str = ", ".join(f"0x{b:02x}" for b in chunk)
        c_code += "  " + hex_str
        if i + 16 < len(tflite_model):
            c_code += ",\n"
        else:
            c_code += "\n"
    
    c_code += "};\n\n"
    c_code += f"const unsigned int g_model_data_len = {len(tflite_model)};\n"
    c_code += "\n#endif // MODEL_DATA_H\n"
    
    with open(output_header, "w") as f:
        f.write(c_code)
    
    print(f"    Header saved to {output_header}")


# ============================================================================
# INFERENCE TEST (Desktop)
# ============================================================================

def test_inference(model, X_test, scaler):
    """Test inference on desktop to validate model behavior."""
    print("[*] Testing inference on validation data...")
    
    # Get reconstruction errors (anomaly scores)
    X_pred = model.predict(X_test)
    mse = np.mean(np.power(X_test - X_pred, 2), axis=(1, 2))  # MSE per sample
    
    print(f"    MSE stats:")
    print(f"      Min: {mse.min():.4f}")
    print(f"      Max: {mse.max():.4f}")
    print(f"      Mean: {mse.mean():.4f}")
    print(f"      Std: {mse.std():.4f}")
    
    # Anomaly score (normalized MSE to [0, 1])
    anomaly_scores = mse / (mse.max() + 1e-8)
    print(f"\n    Anomaly scores (normalized):")
    print(f"      Min: {anomaly_scores.min():.4f}")
    print(f"      Max: {anomaly_scores.max():.4f}")
    print(f"      Mean: {anomaly_scores.mean():.4f}")
    
    return anomaly_scores


# ============================================================================
# MAIN
# ============================================================================

def main():
    print("=" * 70)
    print("RipenSense TinyML Model Training")
    print("=" * 70 + "\n")
    
    # Set random seed for reproducibility
    np.random.seed(RANDOM_SEED)
    tf.random.set_seed(RANDOM_SEED)
    
    # Load & preprocess
    if not os.path.exists(DATA_FILE):
        print(f"ERROR: {DATA_FILE} not found!")
        print("Please ensure you've run: python3 synthetic_data.py")
        return
    
    X_scaled, scaler, df = load_and_preprocess_data(DATA_FILE)
    
    # Create time windows
    X_windowed = create_time_windows(X_scaled)
    
    # Build and train
    model = build_autoencoder(
        input_shape=(TIME_WINDOW, X_scaled.shape[1]),
        latent_dim=LATENT_DIM
    )
    history = train_autoencoder(X_windowed, model, epochs=EPOCHS)
    
    # Convert to TFLite
    tflite_model = convert_to_tflite(model)
    
    # Generate C++ header
    generate_c_header(tflite_model)
    
    # Test inference
    X_test = X_windowed[:min(100, len(X_windowed))]  # Test on first 100 samples
    test_inference(model, X_test, scaler)
    
    print("\n" + "=" * 70)
    print(f"Training complete!")
    print(f"Next steps:")
    print(f"  1. Copy {OUTPUT_HEADER} to DP2PlatformIO/lib/anomaly_model/")
    print(f"  2. Run: pio run --target upload")
    print(f"  3. Monitor with: pio device monitor")
    print("=" * 70)


if __name__ == "__main__":
    main()
