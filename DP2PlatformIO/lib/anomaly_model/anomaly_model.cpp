#include "anomaly_model.h"

// Placeholder for model data - will be generated from training script
const unsigned char g_model_data[] = {};  // TODO: Generated model.tflite as C++ header
const unsigned int g_model_data_len = 0;

AnomalyModel::AnomalyModel()
    : initialized(false), error_reporter(nullptr), model(nullptr), interpreter(nullptr), tensor_arena(nullptr) {
    memset(&lastOutput, 0, sizeof(Model_Output));
}

Model_Status AnomalyModel::init() {
    // Create static error reporter
    static tflite::MicroErrorReporter micro_error_reporter;
    error_reporter = &micro_error_reporter;
    
    // Parse model
    model = tflite::GetModel(g_model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        return MODEL_DATA_INVALID;
    }
    
    // Create resolver
    static tflite::AllOpsResolver resolver;
    
    // Allocate tensor arena
    tensor_arena = (uint8_t *)malloc(TENSOR_ARENA_SIZE);
    if (tensor_arena == nullptr) {
        return MODEL_INIT_FAILED;
    }
    
    // Create and check interpreter
    static tflite::MicroInterpreter static_interpreter(
        model, resolver, tensor_arena, TENSOR_ARENA_SIZE, error_reporter);
    interpreter = &static_interpreter;
    
    if (interpreter->AllocateTensors() != kTfLiteOk) {
        return MODEL_INIT_FAILED;
    }
    
    initialized = true;
    return MODEL_OK;
}

Model_Status AnomalyModel::runInference(const Model_FeatureVector &features, Model_Output &output) {
    if (!initialized) {
        return MODEL_INIT_FAILED;
    }
    
    uint32_t start_time_ms = millis();
    
    // Get input tensor
    TfLiteTensor *input = interpreter->input(0);
    if (input == nullptr) {
        return MODEL_INFERENCE_FAILED;
    }
    
    // Copy feature data to input tensor
    // Assuming input is float32 with 9 features (or 18 for window)
    // Layout: [temp, humidity, accel_x, accel_y, accel_z, gyro_x, gyro_y, gyro_z, ethylene, probe_temp]
    float *input_data = input->data.f;
    if (input_data == nullptr) {
        return MODEL_INFERENCE_FAILED;
    }
    
    // TODO: Populate input buffer from features (may need normalization)
    input_data[0] = features.temperature_c;
    input_data[1] = features.humidity_percent;
    input_data[2] = features.accel_x;
    input_data[3] = features.accel_y;
    input_data[4] = features.accel_z;
    input_data[5] = features.gyro_x;
    input_data[6] = features.gyro_y;
    input_data[7] = features.gyro_z;
    input_data[8] = features.ethylene_ppm;
    input_data[9] = features.probe_temperature_c;
    
    // Run inference
    if (interpreter->Invoke() != kTfLiteOk) {
        return MODEL_INFERENCE_FAILED;
    }
    
    // Get output tensor (anomaly score)
    TfLiteTensor *output_tensor = interpreter->output(0);
    if (output_tensor == nullptr || output_tensor->data.f == nullptr) {
        return MODEL_INFERENCE_FAILED;
    }
    
    // Extract anomaly score
    float anomaly_score = output_tensor->data.f[0];
    
    // Clamp to [0.0, 1.0]
    anomaly_score = constrain(anomaly_score, 0.0f, 1.0f);
    
    output.anomaly_score = anomaly_score;
    output.inference_time_ms = millis() - start_time_ms;
    
    lastOutput = output;
    return MODEL_OK;
}

uint8_t AnomalyModel::scoreToCategory(float anomaly_score) {
    if (anomaly_score < 0.3) return 0;        // green
    else if (anomaly_score < 0.6) return 1;   // yellow
    else if (anomaly_score < 0.85) return 2;  // brown
    else return 3;                             // rotten
}

uint8_t AnomalyModel::scoreToRipeness(float anomaly_score) {
    // Map [0.0, 1.0] to [0, 100]
    return (uint8_t)(anomaly_score * 100.0f);
}

void AnomalyModel::getStatsMemory(size_t &tensors_size, size_t &model_size) {
    tensors_size = TENSOR_ARENA_SIZE;
    model_size = g_model_data_len;
}

Model_Status AnomalyModel::getLatestStats(Model_Output &output) {
    if (!initialized) {
        return MODEL_INIT_FAILED;
    }
    output = lastOutput;
    return MODEL_OK;
}

bool AnomalyModel::isReady() {
    return initialized;
}

void AnomalyModel::normalizeFeatures(Model_FeatureVector &features) {
    // TODO: Apply normalization / standardization
    // This depends on training data statistics (mean, std, min, max)
    // Placeholder for now - assumes features are already in valid ranges
}
