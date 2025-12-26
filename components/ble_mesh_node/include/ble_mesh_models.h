/*
 * ============================================================================
 *                    BLE MESH MODEL LIBRARY - EXTENSIBLE ARCHITECTURE
 * ============================================================================
 *
 * This file provides a plugin-based architecture for BLE Mesh models.
 * You can easily add any combination of models to your node by including
 * them in the configuration - no need to modify the core component!
 *
 * PHILOSOPHY:
 * -----------
 * Models are like LEGO blocks - you pick which ones you need and snap them
 * together. Each model is self-contained with its own:
 * - State management
 * - Message handlers
 * - Callbacks
 * - Publication setup
 *
 * USAGE EXAMPLE:
 * --------------
 * ```c
 * // Simple node with just OnOff
 * mesh_model_config_t models[] = {
 *     MESH_MODEL_ONOFF(.callback = my_onoff_cb),
 * };
 * node_init_with_models(models, 1);
 *
 * // Complex node with multiple models
 * mesh_model_config_t models[] = {
 *     MESH_MODEL_ONOFF(.callback = led_control),
 *     MESH_MODEL_LEVEL(.callback = dimmer_control),
 *     MESH_MODEL_SENSOR(.type = SENSOR_TEMPERATURE, .callback = temp_changed),
 *     MESH_MODEL_VENDOR(.company_id = 0x1234, .model_id = 0x0001,
 *                       .handler = my_vendor_handler),
 * };
 * node_init_with_models(models, 4);
 * ```
 *
 * ============================================================================
 */

#ifndef BLE_MESH_MODELS_H
#define BLE_MESH_MODELS_H

#include <stdint.h>
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations - avoid pulling in ESP-IDF headers in C++ context

/*
 * ============================================================================
 *                         MODEL TYPE ENUMERATION
 * ============================================================================
 */

/**
 * Available model types
 * Each model type has different capabilities and callbacks
 */
typedef enum {
    MESH_MODEL_TYPE_ONOFF,      // Generic OnOff (simple on/off control)
    MESH_MODEL_TYPE_LEVEL,      // Generic Level (0-65535 dimming/position)
    MESH_MODEL_TYPE_SENSOR,     // Sensor (temperature, humidity, etc.)
    MESH_MODEL_TYPE_POWER_LEVEL,// Power Level (device power control)
    MESH_MODEL_TYPE_BATTERY,    // Battery status reporting
    MESH_MODEL_TYPE_VENDOR,     // Custom vendor model (your own protocol)
} mesh_model_type_t;

/*
 * ============================================================================
 *                         MODEL CALLBACKS
 * ============================================================================
 */

/**
 * GENERIC ONOFF MODEL CALLBACKS
 * ==============================
 * Called when OnOff state changes (from mesh command or local control)
 *
 * @param onoff - New state (0=OFF, 1=ON)
 * @param user_data - User-provided context pointer
 */
typedef void (*mesh_onoff_callback_t)(uint8_t onoff, void *user_data);

/**
 * GENERIC LEVEL MODEL CALLBACKS
 * ==============================
 * Called when Level state changes
 *
 * @param level - New level (-32768 to +32767)
 * @param user_data - User-provided context pointer
 *
 * COMMON USES:
 * - Dimmer: Map -32768..32767 to 0..100%
 * - Position: Map to servo angle
 * - Volume: Map to audio level
 */
typedef void (*mesh_level_callback_t)(int16_t level, void *user_data);

/**
 * SENSOR MODEL CALLBACKS
 * =======================
 * Called when sensor data should be read
 *
 * @param sensor_type - Which sensor is being queried
 * @param value_out - Pointer to store sensor value
 * @param user_data - User-provided context pointer
 * @return ESP_OK if sensor read successfully
 *
 * IMPLEMENTATION:
 * Your callback should read the sensor and write the value to value_out
 */
typedef esp_err_t (*mesh_sensor_read_callback_t)(uint16_t sensor_type,
                                                  int32_t *value_out,
                                                  void *user_data);

/**
 * VENDOR MODEL MESSAGE HANDLER
 * ==============================
 * Called when a vendor-specific message is received
 *
 * @param opcode - Message opcode (your custom command)
 * @param data - Message payload
 * @param length - Payload length in bytes
 * @param ctx - Message context (source address, etc.)
 * @param user_data - User-provided context pointer
 */
typedef void (*mesh_vendor_handler_t)(uint32_t opcode,
                                      uint8_t *data,
                                      uint16_t length,
                                      void *ctx,
                                      void *user_data);

/**
 * BATTERY STATUS CALLBACK
 * ========================
 * Called when battery status should be reported
 *
 * @param battery_level_out - Battery percentage (0-100)
 * @param user_data - User-provided context pointer
 * @return ESP_OK if battery read successfully
 */
typedef esp_err_t (*mesh_battery_callback_t)(uint8_t *battery_level_out,
                                             void *user_data);

/*
 * ============================================================================
 *                    SENSOR TYPE DEFINITIONS
 * ============================================================================
 */

/**
 * Standard sensor types (from Bluetooth SIG specification)
 * These are interoperable with all BLE Mesh devices
 */
typedef enum {
    SENSOR_TEMPERATURE = 0x004F,           // Temperature in 0.01°C
    SENSOR_HUMIDITY = 0x004D,              // Humidity in 0.01%
    SENSOR_PRESSURE = 0x2A6D,              // Pressure in 0.1 Pa
    SENSOR_MOTION_DETECTED = 0x0042,       // Motion sensor (0/1)
    SENSOR_PEOPLE_COUNT = 0x004C,          // Number of people
    SENSOR_AMBIENT_LIGHT = 0x004E,         // Light level in lux
    SENSOR_BATTERY_LEVEL = 0x2A19,         // Battery % (0-100)
    SENSOR_VOLTAGE = 0x2B18,               // Voltage in 1/64 V

    // IMU sensors (custom types)
    SENSOR_ACCEL_X = 0x5001,               // Accelerometer X in mg (milli-g)
    SENSOR_ACCEL_Y = 0x5002,               // Accelerometer Y in mg
    SENSOR_ACCEL_Z = 0x5003,               // Accelerometer Z in mg
    SENSOR_GYRO_X = 0x5004,                // Gyroscope X in mdps (milli degrees/sec)
    SENSOR_GYRO_Y = 0x5005,                // Gyroscope Y in mdps
    SENSOR_GYRO_Z = 0x5006,                // Gyroscope Z in mdps
} mesh_sensor_type_t;

/*
 * ============================================================================
 *                    VENDOR MODEL CONFIGURATION
 * ============================================================================
 */

/**
 * Vendor model configuration
 * Use this to define custom models with your own protocol
 */
typedef struct {
    uint16_t company_id;         // Your company ID (0xFFFF for testing)
    uint16_t model_id;           // Your model ID (choose any)
    mesh_vendor_handler_t handler; // Message handler callback
    void *user_data;             // Optional user context
} mesh_vendor_config_t;

/*
 * ============================================================================
 *                    SENSOR MODEL CONFIGURATION
 * ============================================================================
 */

/**
 * Sensor model configuration
 * Configure one or more sensors
 */
typedef struct {
    mesh_sensor_type_t type;           // Sensor type (temperature, humidity, etc.)
    mesh_sensor_read_callback_t read;  // Callback to read sensor value
    uint32_t publish_period_ms;        // How often to publish (0 = manual only)
    void *user_data;                   // Optional user context
} mesh_sensor_config_t;

/*
 * ============================================================================
 *                    UNIFIED MODEL CONFIGURATION
 * ============================================================================
 */

/**
 * Model configuration structure
 * This is the MAIN structure you'll use to configure your node
 *
 * DESIGN PATTERN:
 * ---------------
 * Each model type has an associated config union member.
 * Set the 'type' field, then fill in the corresponding union member.
 */
typedef struct {
    mesh_model_type_t type;        // Which model to enable
    bool enable_publication;       // Allow publishing state changes?

    // Model-specific configuration (union - only one active)
    union {
        // Generic OnOff configuration
        struct {
            mesh_onoff_callback_t callback;  // State change callback
            uint8_t initial_state;           // Initial state (0 or 1)
            void *user_data;                 // Optional context
        } onoff;

        // Generic Level configuration
        struct {
            mesh_level_callback_t callback;  // Level change callback
            int16_t initial_level;           // Initial level (-32768 to 32767)
            void *user_data;                 // Optional context
        } level;

        // Sensor configuration
        struct {
            mesh_sensor_config_t *sensors;   // Array of sensors
            uint8_t sensor_count;            // Number of sensors
        } sensor;

        // Battery configuration
        struct {
            mesh_battery_callback_t callback; // Battery read callback
            uint32_t publish_period_ms;      // Publish period
            void *user_data;                 // Optional context
        } battery;

        // Vendor model configuration
        mesh_vendor_config_t vendor;
    } config;
} mesh_model_config_t;

/*
 * ============================================================================
 *                    CONVENIENCE MACROS FOR MODEL CONFIGURATION
 * ============================================================================
 */

/**
 * Configure Generic OnOff model
 *
 * @param cb - Callback function
 * @param init - Initial state (0 or 1)
 * @param ctx - User data pointer (can be NULL)
 *
 * EXAMPLE:
 * mesh_model_config_t models[] = {
 *     MESH_MODEL_ONOFF(led_callback, 0, NULL),
 * };
 */
#define MESH_MODEL_ONOFF(cb, init, ctx) { \
    MESH_MODEL_TYPE_ONOFF, \
    true, \
    { .onoff = { (cb), (init), (ctx) } } \
}

/**
 * Configure Generic Level model
 *
 * @param cb - Callback function
 * @param init - Initial level (-32768 to 32767)
 * @param ctx - User data pointer (can be NULL)
 *
 * EXAMPLE:
 * mesh_model_config_t models[] = {
 *     MESH_MODEL_LEVEL(dimmer_callback, 0, NULL),
 * };
 */
#define MESH_MODEL_LEVEL(cb, init, ctx) { \
    .type = MESH_MODEL_TYPE_LEVEL, \
    .enable_publication = true, \
    .config.level = { \
        .callback = (cb), \
        .initial_level = (init), \
        .user_data = (ctx) \
    } \
}

/**
 * Configure Sensor model
 *
 * @param sensor_array - Array of sensor configurations
 * @param count - Number of sensors in array
 *
 * EXAMPLE:
 * mesh_sensor_config_t my_sensors[] = {
 *     {.type = SENSOR_TEMPERATURE, .read = read_temp, .publish_period_ms = 10000},
 *     {.type = SENSOR_HUMIDITY, .read = read_humidity, .publish_period_ms = 10000},
 * };
 * mesh_model_config_t models[] = {
 *     MESH_MODEL_SENSOR(my_sensors, 2),
 * };
 */
#ifdef __cplusplus
#define MESH_MODEL_SENSOR(sensor_array, count) { \
    MESH_MODEL_TYPE_SENSOR, \
    true, \
    { .sensor = { (sensor_array), (count) } } \
}
#else
#define MESH_MODEL_SENSOR(sensor_array, count) { \
    .type = MESH_MODEL_TYPE_SENSOR, \
    .enable_publication = true, \
    .config.sensor = { \
        .sensors = (sensor_array), \
        .sensor_count = (count) \
    } \
}
#endif

/**
 * Configure Vendor model
 *
 * @param cid - Company ID
 * @param mid - Model ID
 * @param handler - Message handler
 * @param ctx - User data pointer
 *
 * EXAMPLE:
 * mesh_model_config_t models[] = {
 *     MESH_MODEL_VENDOR(0x1234, 0x0001, my_handler, NULL),
 * };
 */
#ifdef __cplusplus
#define MESH_MODEL_VENDOR(cid, mid, handler, ctx) { \
    MESH_MODEL_TYPE_VENDOR, \
    true, \
    { .vendor = { (cid), (mid), (handler), (ctx) } } \
}
#else
#define MESH_MODEL_VENDOR(cid, mid, handler, ctx) { \
    .type = MESH_MODEL_TYPE_VENDOR, \
    .enable_publication = true, \
    .config.vendor = { \
        .company_id = (cid), \
        .model_id = (mid), \
        .handler = (handler), \
        .user_data = (ctx) \
    } \
}
#endif

/**
 * Configure Battery model
 *
 * @param cb - Battery read callback
 * @param period - Publish period in milliseconds
 * @param ctx - User data pointer
 *
 * EXAMPLE:
 * mesh_model_config_t models[] = {
 *     MESH_MODEL_BATTERY(read_battery, 60000, NULL),  // Report every 60 seconds
 * };
 */
#define MESH_MODEL_BATTERY(cb, period, ctx) { \
    .type = MESH_MODEL_TYPE_BATTERY, \
    .enable_publication = true, \
    .config.battery = { \
        .callback = (cb), \
        .publish_period_ms = (period), \
        .user_data = (ctx) \
    } \
}

/*
 * ============================================================================
 *                    MODEL API FUNCTIONS
 * ============================================================================
 */

/**
 * Publish OnOff state manually
 * Useful when you change state locally and want to notify the network
 *
 * @param model_index - Which OnOff model (usually 0)
 * @param onoff - State to publish (0 or 1)
 * @return ESP_OK on success
 */
esp_err_t mesh_model_publish_onoff(uint8_t model_index, uint8_t onoff);

/**
 * Publish Level state manually
 *
 * @param model_index - Which Level model (usually 0)
 * @param level - Level to publish (-32768 to 32767)
 * @return ESP_OK on success
 */
esp_err_t mesh_model_publish_level(uint8_t model_index, int16_t level);

/**
 * Send vendor model message
 *
 * @param model_index - Which Vendor model (usually 0)
 * @param opcode - Your custom opcode
 * @param data - Message payload
 * @param length - Payload length
 * @param dest_addr - Destination address (0x0001 = provisioner)
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED (not yet fully implemented)
 */
esp_err_t mesh_model_send_vendor(uint8_t model_index, uint32_t opcode, uint8_t *data,
                                 uint16_t length, uint16_t dest_addr);

/**
 * Publish vendor message (to configured publish address)
 *
 * Sends a vendor message to the model's publication address (configured by provisioner).
 * Use this for broadcasting to multiple subscribers or periodic status updates.
 *
 * @param model_index - Which vendor model to use
 * @param opcode - 3-byte vendor opcode (use ESP_BLE_MESH_MODEL_OP_3)
 * @param data - Message payload
 * @param length - Payload length
 * @return ESP_OK on success
 */
esp_err_t mesh_model_publish_vendor(uint8_t model_index, uint32_t opcode, uint8_t *data,
                                    uint16_t length);

/**
 * Get current OnOff state
 *
 * @param model_index - Which OnOff model (usually 0)
 * @return Current state (0 or 1), or -1 on error
 */
int mesh_model_get_onoff(uint8_t model_index);

/**
 * Set OnOff state locally (and optionally publish)
 *
 * @param model_index - Which OnOff model (usually 0)
 * @param onoff - New state (0 or 1)
 * @param publish - Publish change to network?
 * @return ESP_OK on success
 */
esp_err_t mesh_model_set_onoff(uint8_t model_index, uint8_t onoff, bool publish);

/**
 * Get current Level
 *
 * @param model_index - Which Level model (usually 0)
 * @return Current level, or INT16_MIN on error
 */
int16_t mesh_model_get_level(uint8_t model_index);

/**
 * Set Level locally (and optionally publish)
 *
 * @param model_index - Which Level model (usually 0)
 * @param level - New level (-32768 to 32767)
 * @param publish - Publish change to network?
 * @return ESP_OK on success
 */
esp_err_t mesh_model_set_level(uint8_t model_index, int16_t level, bool publish);

/*
 * ============================================================================
 *                    SENSOR MODEL API
 * ============================================================================
 */

/**
 * Read sensor value
 *
 * @param model_index - Which Sensor model (usually 0)
 * @param sensor_type - Sensor type to read (e.g., SENSOR_TEMPERATURE)
 * @param value_out - Pointer to store sensor value
 * @return ESP_OK on success
 *
 * EXAMPLE:
 * int32_t temp;
 * esp_err_t ret = mesh_model_read_sensor(0, SENSOR_TEMPERATURE, &temp);
 * if (ret == ESP_OK) {
 *     printf("Temperature: %d (0.01°C)\n", (int)temp);
 * }
 */
esp_err_t mesh_model_read_sensor(uint8_t model_index, uint16_t sensor_type, int32_t *value_out);

/**
 * Publish sensor value to mesh network
 *
 * @param model_index - Which Sensor model (usually 0)
 * @param sensor_type - Sensor type to publish
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED (not yet implemented)
 */
esp_err_t mesh_model_publish_sensor(uint8_t model_index, uint16_t sensor_type);

#ifdef __cplusplus
}
#endif

#endif // BLE_MESH_MODELS_H
