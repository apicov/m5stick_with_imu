/*
 * ============================================================================
 *                    BLE MESH NODE COMPONENT - EXTENSIBLE
 * ============================================================================
 *
 * This is a reusable, EXTENSIBLE component that turns your ESP32 into a
 * BLE Mesh node with configurable models.
 *
 * NEW IN V2: PLUGIN-BASED ARCHITECTURE
 * =====================================
 * Instead of hardcoded models, you can now configure ANY combination of models:
 * - Generic OnOff (simple ON/OFF)
 * - Generic Level (dimming, positioning)
 * - Sensor (temperature, humidity, motion, etc.)
 * - Battery (battery status reporting)
 * - Vendor (your own custom protocol)
 *
 * WHAT THIS COMPONENT PROVIDES:
 * ==============================
 * - Simple API to initialize BLE Mesh with custom models
 * - Pre-built model configurations (use as-is or customize)
 * - Automatic handling of provisioning process
 * - Model library with common models (see ble_mesh_models.h)
 * - Callbacks for application integration
 *
 * BASIC USAGE (Simple OnOff Node):
 * =================================
 * ```c
 * void led_changed(uint8_t onoff, void *user_data) {
 *     gpio_set_level(LED_PIN, onoff);
 * }
 *
 * mesh_model_config_t models[] = {
 *     MESH_MODEL_ONOFF(led_changed, 0, NULL),
 * };
 *
 * node_config_t config = {
 *     .device_uuid_prefix = {0xdd, 0xdd},
 *     .models = models,
 *     .model_count = 1,
 * };
 *
 * node_init(&config);
 * node_start();
 * ```
 *
 * ADVANCED USAGE (Multi-Model Node):
 * ===================================
 * ```c
 * // Callbacks
 * void led_changed(uint8_t onoff, void *ctx) { ... }
 * void dimmer_changed(int16_t level, void *ctx) { ... }
 * esp_err_t read_temp(uint16_t type, int32_t *value, void *ctx) { ... }
 *
 * // Sensor configuration
 * mesh_sensor_config_t sensors[] = {
 *     {.type = SENSOR_TEMPERATURE, .read = read_temp, .publish_period_ms = 10000},
 *     {.type = SENSOR_HUMIDITY, .read = read_humidity, .publish_period_ms = 10000},
 * };
 *
 * // Model configuration
 * mesh_model_config_t models[] = {
 *     MESH_MODEL_ONOFF(led_changed, 0, NULL),
 *     MESH_MODEL_LEVEL(dimmer_changed, 0, NULL),
 *     MESH_MODEL_SENSOR(sensors, 2),
 * };
 *
 * node_config_t config = {
 *     .device_uuid_prefix = {0xdd, 0xdd},
 *     .models = models,
 *     .model_count = 3,
 * };
 *
 * node_init(&config);
 * node_start();
 * ```
 *
 * BACKWARD COMPATIBILITY:
 * =======================
 * The old API (node_init with just callbacks) still works for simple OnOff nodes.
 * Use node_init_with_models() for the new extensible architecture.
 *
 * SECURITY:
 * =========
 * - All mesh communication is encrypted
 * - NetKey: Network layer encryption (shared by all nodes in network)
 * - AppKey: Application layer encryption (shared by nodes in same app)
 * - DevKey: Device-specific key (used for node configuration)
 */

#ifndef BLE_MESH_NODE_H
#define BLE_MESH_NODE_H

#include "esp_err.h"
#include "ble_mesh_models.h"  // Model library
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 *                         NODE-LEVEL CALLBACKS
 * ============================================================================
 * These callbacks are for node-wide events (provisioning, reset, etc.)
 * For model-specific callbacks (OnOff, Level, etc.), see ble_mesh_models.h
 */

/**
 * Node-level event callbacks
 * These are called for important node lifecycle events
 */
typedef struct {
    /**
     * Called when provisioning is complete
     * @param unicast_addr The unicast address assigned to this node
     *
     * At this point, the node is part of the network but not yet fully
     * configured. The provisioner will soon add AppKey and bind models.
     */
    void (*provisioned)(uint16_t unicast_addr);

    /**
     * Called when node is reset (factory reset)
     * You should clear any stored state and optionally restart
     *
     * After this callback, all mesh credentials are erased from NVS.
     */
    void (*reset)(void);

    /**
     * Called when configuration is complete
     * @param app_key_idx The AppKey index that was added
     *
     * At this point, models are bound and ready to communicate.
     * The node is fully operational.
     */
    void (*config_complete)(uint16_t app_key_idx);
} node_callbacks_t;

/*
 * ============================================================================
 *                    NODE CONFIGURATION (NEW EXTENSIBLE API)
 * ============================================================================
 */

/**
 * Node configuration structure (V2 - Extensible)
 *
 * This structure allows you to configure:
 * - Device identity (UUID prefix)
 * - Which models to include (OnOff, Level, Sensor, etc.)
 * - Node-level callbacks (provisioning, reset, etc.)
 * - Optional device name
 */
typedef struct {
    /**
     * First 2 bytes of device UUID
     * The provisioner uses this to filter which devices to provision
     * MUST match the provisioner's match_prefix configuration
     *
     * Example: {0xdd, 0xdd} matches provisioner filter
     */
    uint8_t device_uuid_prefix[2];

    /**
     * Models to include in this node
     * Array of model configurations (OnOff, Level, Sensor, etc.)
     *
     * See ble_mesh_models.h for model types and configuration macros.
     */
    mesh_model_config_t *models;

    /**
     * Number of models in the array
     */
    uint8_t model_count;

    /**
     * Optional node-level callbacks
     * Set fields to NULL if you don't need them
     */
    node_callbacks_t callbacks;

    /**
     * Optional device name (shown in provisioner's scan)
     * Max 29 characters. If NULL, defaults to "ESP-Mesh-Node"
     */
    const char *device_name;
} node_config_t;

/*
 * ============================================================================
 *                         NODE INITIALIZATION AND CONTROL
 * ============================================================================
 */

/**
 * INITIALIZE BLE MESH NODE (V2 - Extensible API)
 * ===============================================
 *
 * Initializes the Bluetooth stack and BLE Mesh node with configurable models.
 * Must be called once before node_start().
 *
 * WHAT THIS DOES:
 * ---------------
 * 1. Initializes NVS (for storing mesh configuration)
 * 2. Initializes Bluetooth controller and host
 * 3. Generates device UUID from prefix + BT MAC
 * 4. Builds element/model structure from config
 * 5. Initializes BLE Mesh stack with all configured models
 * 6. Registers all callbacks
 *
 * @param config Node configuration (UUID, models, callbacks)
 * @return ESP_OK on success, error code otherwise
 *
 * EXAMPLE (Simple OnOff Node):
 * -----------------------------
 * mesh_model_config_t models[] = {
 *     MESH_MODEL_ONOFF(led_callback, 0, NULL),
 * };
 * node_config_t config = {
 *     .device_uuid_prefix = {0xdd, 0xdd},
 *     .models = models,
 *     .model_count = 1,
 *     .device_name = "My LED Node",
 * };
 * esp_err_t err = node_init(&config);
 *
 * EXAMPLE (Multi-Model Node):
 * ----------------------------
 * mesh_sensor_config_t sensors[] = {
 *     {.type = SENSOR_TEMPERATURE, .read = read_temp, .publish_period_ms = 10000},
 * };
 * mesh_model_config_t models[] = {
 *     MESH_MODEL_ONOFF(led_callback, 0, NULL),
 *     MESH_MODEL_LEVEL(dimmer_callback, 0, NULL),
 *     MESH_MODEL_SENSOR(sensors, 1),
 * };
 * node_config_t config = {
 *     .device_uuid_prefix = {0xdd, 0xdd},
 *     .models = models,
 *     .model_count = 3,
 *     .device_name = "Smart Light",
 * };
 * esp_err_t err = node_init(&config);
 */
esp_err_t node_init(const node_config_t *config);

/**
 * START BLE MESH NODE
 * ===================
 *
 * Starts broadcasting as an unprovisioned device (or rejoins if already provisioned).
 * The node will send "Unprovisioned Device Beacons" that provisioners can discover.
 *
 * WHAT HAPPENS NEXT:
 * ------------------
 * IF NOT PROVISIONED:
 * 1. Node broadcasts beacons containing its UUID
 * 2. Provisioner discovers the node (if UUID matches filter)
 * 3. Provisioner initiates provisioning
 * 4. Node receives NetKey and unicast address
 * 5. Provisioner configures node (AppKey, model binding)
 * 6. Node is ready to receive commands
 *
 * IF ALREADY PROVISIONED (stored in NVS):
 * 1. Node rejoins network with stored credentials
 * 2. Immediately ready to communicate
 * 3. No provisioning needed
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t node_start(void);

/*
 * ============================================================================
 *                    MODEL API FUNCTIONS (NEW EXTENSIBLE API)
 * ============================================================================
 * These functions allow you to interact with individual models by index.
 * Use these for multi-model configurations.
 */

/**
 * GET ONOFF STATE
 * ===============
 * Returns the current Generic OnOff state of a specific OnOff model.
 *
 * @param model_index Index of the OnOff model (0 for first, 1 for second, etc.)
 * @return Current state (0 = OFF, 1 = ON), or -1 if model not found
 */
int mesh_model_get_onoff(uint8_t model_index);

/**
 * SET ONOFF STATE
 * ===============
 * Changes the Generic OnOff state of a specific OnOff model.
 * Optionally publishes the state change to the network.
 *
 * @param model_index Index of the OnOff model (0 for first, 1 for second, etc.)
 * @param onoff New state (0 = OFF, 1 = ON)
 * @param publish If true, publish state to network (not yet implemented)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mesh_model_set_onoff(uint8_t model_index, uint8_t onoff, bool publish);

/**
 * PUBLISH ONOFF STATE
 * ===================
 * Publishes the current OnOff state to the network.
 *
 * @param model_index Index of the OnOff model
 * @param onoff State to publish
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED (not yet implemented)
 */
esp_err_t mesh_model_publish_onoff(uint8_t model_index, uint8_t onoff);

/**
 * GET LEVEL STATE
 * ===============
 * Returns the current Generic Level state of a specific Level model.
 *
 * @param model_index Index of the Level model (0 for first, 1 for second, etc.)
 * @return Current level (-32768 to +32767), or INT16_MIN if model not found
 */
int16_t mesh_model_get_level(uint8_t model_index);

/**
 * SET LEVEL STATE
 * ===============
 * Changes the Generic Level state of a specific Level model.
 * Optionally publishes the state change to the network.
 *
 * @param model_index Index of the Level model (0 for first, 1 for second, etc.)
 * @param level New level (-32768 to +32767)
 * @param publish If true, publish state to network (not yet implemented)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mesh_model_set_level(uint8_t model_index, int16_t level, bool publish);

/**
 * PUBLISH LEVEL STATE
 * ===================
 * Publishes the current Level state to the network.
 *
 * @param model_index Index of the Level model
 * @param level Level to publish
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED (not yet implemented)
 */
esp_err_t mesh_model_publish_level(uint8_t model_index, int16_t level);

/**
 * GET BATTERY LEVEL
 * =================
 * Returns the current battery level. Calls the battery callback if configured.
 *
 * @param model_index Index of the Battery model (usually 0)
 * @return Current battery level (0-100%), or 0 if model not found
 */
uint8_t mesh_model_get_battery(uint8_t model_index);

/**
 * SET BATTERY LEVEL
 * =================
 * Sets the battery level manually.
 *
 * @param model_index Index of the Battery model (usually 0)
 * @param battery_level Battery percentage (0-100)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mesh_model_set_battery(uint8_t model_index, uint8_t battery_level);

/**
 * PUBLISH BATTERY STATE
 * =====================
 * Publishes the current battery level to the network.
 *
 * @param model_index Index of the Battery model
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED (not yet implemented)
 */
esp_err_t mesh_model_publish_battery(uint8_t model_index);

/*
 * ============================================================================
 *                    BACKWARD COMPATIBILITY (LEGACY API)
 * ============================================================================
 * These functions provide compatibility with the old API.
 * They work ONLY if you configured a Generic OnOff model.
 * For the new API, use mesh_model_get_onoff() and mesh_model_set_onoff().
 */

/**
 * GET CURRENT ONOFF STATE (Legacy API)
 * =====================================
 *
 * Returns the current Generic OnOff state of the FIRST OnOff model.
 *
 * @return Current state (0 = OFF, 1 = ON), or 0xFF if no OnOff model
 *
 * NOTE: For new code, use mesh_model_get_onoff(model_index) instead.
 */
uint8_t node_get_onoff_state(void);

/**
 * SET ONOFF STATE LOCALLY (Legacy API)
 * =====================================
 *
 * Changes the Generic OnOff state of the FIRST OnOff model and publishes it.
 * Use this when you want to change state locally (e.g., button press).
 *
 * @param onoff New state (0 = OFF, 1 = ON)
 * @return ESP_OK on success, error code otherwise
 *
 * NOTE: For new code, use mesh_model_set_onoff(model_index, state, true) instead.
 */
esp_err_t node_set_onoff_state(uint8_t onoff);

#ifdef __cplusplus
}
#endif

#endif // BLE_MESH_NODE_H
