#ifndef ANOMALY_MODEL_H
#define ANOMALY_MODEL_H

#include <Arduino.h>
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"

// Forward declare the model data (generated from training script)
extern const unsigned char g_model_data[];
extern const unsigned int g_model_data_len;

typedef enum {
    MODEL_OK = 0,
    MODEL_INIT_FAILED = -1,
    MODEL_INFERENCE_FAILED = -2,
    MODEL_DATA_INVALID = -3
} Model_Status;

typedef struct {
    float temperature_c;
    float humidity_percent;
    float accel_x, accel_y, accel_z;
    float gyro_x, gyro_y, gyro_z;
    float ethylene_ppm;
    float probe_temperature_c;
} Model_FeatureVector;

typedef struct {
    float anomaly_score;        // 0.0 – 1.0
    uint32_t inference_time_ms;
} Model_Output;

class AnomalyModel {
public:
    AnomalyModel();
    
    // Initialize TFLite model and interpreter
    Model_Status init();
    
    // Run inference on feature vector
    Model_Status runInference(const Model_FeatureVector &features, Model_Output &output);
    
    // Convert anomaly score to ripeness category
    // 0 = green (score 0.0-0.3)
    // 1 = yellow (score 0.3-0.6)
    // 2 = brown (score 0.6-0.85)
    // 3 = rotten (score > 0.85)
    static uint8_t scoreToCategory(float anomaly_score);
    
    // Convert anomaly score to numeric ripeness (0-100 scale)
    // Maps anomaly_score [0.0, 1.0] to ripeness [0, 100]
    static uint8_t scoreToRipeness(float anomaly_score);
    
    // Get model memory usage stats
    void getStatsMemory(size_t &tensors_size, size_t &model_size);
    
    // Get latest inference stats
    Model_Status getLatestStats(Model_Output &output);
    
    // Check if model is initialized
    bool isReady();
    
private:
    bool initialized;
    Model_Output lastOutput;
    
    // TFLite components
    tflite::MicroErrorReporter *error_reporter;
    const tflite::Model *model;
    tflite::MicroInterpreter *interpreter;
    uint8_t *tensor_arena;
    
    static constexpr size_t TENSOR_ARENA_SIZE = 64 * 1024;  // 64 KB for tensors
    
    // Helper: normalize input features
    void normalizeFeatures(Model_FeatureVector &features);
};

#endif // ANOMALY_MODEL_H
