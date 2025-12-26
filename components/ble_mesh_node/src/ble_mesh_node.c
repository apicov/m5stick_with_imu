/*
 * ============================================================================
 *                  BLE MESH NODE - EXTENSIBLE IMPLEMENTATION (V2)
 * ============================================================================
 *
 * This is Phase 1 of the extensible architecture implementation.
 *
 * WHAT'S IMPLEMENTED:
 * -------------------
 * ✅ Model registry system (foundation for all models)
 * ✅ Dynamic model building from configuration
 * ✅ Generic OnOff model (fully working)
 * ✅ Backward compatibility with old API
 * ✅ All existing functionality preserved
 *
 * WHAT'S NOT YET IMPLEMENTED:
 * ---------------------------
 * ⏳ Generic Level model (Phase 2)
 * ⏳ Sensor model (Phase 3)
 * ⏳ Battery model (Phase 5)
 * ⏳ Vendor model (Phase 4)
 *
 * This file maintains all the comprehensive educational comments from the
 * original while adding the new extensible architecture.
 */

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_sensor_model_api.h"
#include <string.h>

// Include our headers AFTER ESP-IDF headers (they need the types defined above)
#include "ble_mesh_node.h"
#include "ble_mesh_models.h"

#define TAG "BLE_MESH_NODE"

// Maximum models supported per node
#define MAX_MODELS 8

/*
 * ============================================================================
 *                         MODEL REGISTRY SYSTEM
 * ============================================================================
 *
 * The model registry is the core of the extensible architecture.
 * It tracks all configured models and their runtime state.
 *
 * DESIGN:
 * -------
 * When user configures models in node_config_t, we:
 * 1. Parse each mesh_model_config_t
 * 2. Allocate ESP-IDF model structures
 * 3. Register in model_registry
 * 4. Build element structure dynamically
 * 5. Initialize BLE Mesh with dynamic composition
 *
 * This allows any combination of models without code changes!
 */

/**
 * OnOff model runtime state
 * Tracks state and configuration for one OnOff model instance
 */
typedef struct {
    uint8_t onoff;                        // Current state (0 or 1)
    mesh_onoff_callback_t callback;       // User's callback
    void *user_data;                      // User's context pointer
    esp_ble_mesh_gen_onoff_srv_t server; // ESP-IDF server structure
    esp_ble_mesh_model_pub_t pub;        // Publication context
    esp_ble_mesh_model_t *esp_model;     // Pointer to ESP-IDF model for publishing
} onoff_model_state_t;

/**
 * Level model runtime state
 * Tracks state and configuration for one Level model instance
 */
typedef struct {
    int16_t level;                        // Current level (-32768 to +32767)
    mesh_level_callback_t callback;       // User's callback
    void *user_data;                      // User's context pointer
    esp_ble_mesh_gen_level_srv_t server; // ESP-IDF server structure
    esp_ble_mesh_model_pub_t pub;        // Publication context
    esp_ble_mesh_model_t *esp_model;     // Pointer to ESP-IDF model for publishing
} level_model_state_t;

/**
 * Model registry entry
 * One entry per configured model
 */
typedef struct {
    mesh_model_type_t type;               // Model type (OnOff, Level, etc.)
    esp_ble_mesh_model_t *esp_model;     // ESP-IDF model structure
    mesh_model_config_t user_config;     // User's original configuration
    void *runtime_state;                  // Model-specific runtime state
} model_registry_entry_t;

/**
 * Global model registry
 * Stores all configured models
 */
static model_registry_entry_t model_registry[MAX_MODELS];
static uint8_t registered_model_count = 0;

/**
 * Dynamic model storage
 * Allocated during initialization based on configuration
 * SIG models and vendor models must be in separate arrays
 */
static esp_ble_mesh_model_t *dynamic_sig_models = NULL;
static uint8_t sig_model_count = 0;
static esp_ble_mesh_model_t *dynamic_vnd_models = NULL;
static uint8_t vnd_model_count = 0;

/*
 * ============================================================================
 *                         GLOBAL STATE (from original)
 * ============================================================================
 */

static uint8_t dev_uuid[16] = {0};
static node_callbacks_t app_callbacks = {0};
static const char *device_name = NULL;

// Configuration Server (always present - mandatory)
static esp_ble_mesh_cfg_srv_t config_server = {
    .relay = ESP_BLE_MESH_RELAY_DISABLED,
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER)
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
#else
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif
    .default_ttl = 7,
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
};

// Dynamic element (built at runtime)
static esp_ble_mesh_elem_t *elements = NULL;
static esp_ble_mesh_comp_t composition = {
    .cid = 0xFFFF,
    .pid = 0x0000,
    .vid = 0x0000,
};

static esp_ble_mesh_prov_t provision;

/*
 * ============================================================================
 *                         FORWARD DECLARATIONS
 * ============================================================================
 */

static void mesh_prov_cb(esp_ble_mesh_prov_cb_event_t event,
                        esp_ble_mesh_prov_cb_param_t *param);
static void mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                                   esp_ble_mesh_cfg_server_cb_param_t *param);
static void mesh_generic_server_cb(esp_ble_mesh_generic_server_cb_event_t event,
                                    esp_ble_mesh_generic_server_cb_param_t *param);
static void mesh_sensor_server_cb(esp_ble_mesh_sensor_server_cb_event_t event,
                                   esp_ble_mesh_sensor_server_cb_param_t *param);

/*
 * ============================================================================
 *                    ONOFF MODEL IMPLEMENTATION
 * ============================================================================
 */

/**
 * Find OnOff model by index
 * @param index Model index (0, 1, 2, ...)
 * @return Pointer to model state, or NULL if not found
 */
static onoff_model_state_t* find_onoff_model(uint8_t index)
{
    uint8_t onoff_idx = 0;
    for (int i = 0; i < registered_model_count; i++) {
        if (model_registry[i].type == MESH_MODEL_TYPE_ONOFF) {
            if (onoff_idx == index) {
                return (onoff_model_state_t*)model_registry[i].runtime_state;
            }
            onoff_idx++;
        }
    }
    return NULL;
}

/**
 * Find Level model by index
 * @param index Model index (0, 1, 2, ...)
 * @return Pointer to model state, or NULL if not found
 */
static level_model_state_t* find_level_model(uint8_t index)
{
    uint8_t level_idx = 0;
    for (int i = 0; i < registered_model_count; i++) {
        if (model_registry[i].type == MESH_MODEL_TYPE_LEVEL) {
            if (level_idx == index) {
                return (level_model_state_t*)model_registry[i].runtime_state;
            }
            level_idx++;
        }
    }
    return NULL;
}

/*
 * ============================================================================
 *                    SENSOR MODEL IMPLEMENTATION
 * ============================================================================
 */

/**
 * Sensor model runtime state
 * Stores the ESP-IDF Sensor Server structure and user configuration
 */
typedef struct {
    mesh_sensor_config_t *sensors;              // Array of sensor configurations
    uint8_t sensor_count;                       // Number of sensors
    esp_ble_mesh_sensor_state_t *sensor_states; // ESP-IDF sensor states array
    struct net_buf_simple **sensor_bufs;        // Array of buffers for raw_value (one per sensor)
    esp_ble_mesh_sensor_srv_t server;          // ESP-IDF server structure
    esp_ble_mesh_sensor_setup_srv_t setup;     // ESP-IDF setup server structure (REQUIRED)
    esp_ble_mesh_model_pub_t pub;              // Publication context for Sensor Server
    esp_ble_mesh_model_pub_t setup_pub;        // Publication context for Setup Server (REQUIRED)
    esp_ble_mesh_model_t *esp_model;           // Pointer to ESP-IDF model for publishing
} sensor_model_state_t;

/**
 * Find Sensor model by index
 * @param index Model index (0, 1, 2, ...)
 * @return Pointer to model state, or NULL if not found
 */
static sensor_model_state_t* find_sensor_model(uint8_t index)
{
    uint8_t sensor_idx = 0;
    for (int i = 0; i < registered_model_count; i++) {
        if (model_registry[i].type == MESH_MODEL_TYPE_SENSOR) {
            if (sensor_idx == index) {
                return (sensor_model_state_t*)model_registry[i].runtime_state;
            }
            sensor_idx++;
        }
    }
    return NULL;
}

/*
 * ============================================================================
 *                    BATTERY MODEL IMPLEMENTATION
 * ============================================================================
 */

/**
 * Battery model runtime state
 * Stores battery level callback and reporting configuration
 */
typedef struct {
    uint8_t battery_level;                  // Current battery % (0-100)
    mesh_battery_callback_t callback;       // Callback to read battery
    uint32_t publish_period_ms;             // Publish period
    void *user_data;                        // User context
    esp_ble_mesh_gen_battery_srv_t server; // ESP-IDF server structure
    esp_ble_mesh_model_pub_t pub;          // Publication context
    esp_ble_mesh_model_t *esp_model;       // Pointer to ESP-IDF model (for publishing)
} battery_model_state_t;

/**
 * Find Battery model by index
 * @param index Model index (0, 1, 2, ...)
 * @return Pointer to model state, or NULL if not found
 */
static battery_model_state_t* find_battery_model(uint8_t index)
{
    uint8_t battery_idx = 0;
    for (int i = 0; i < registered_model_count; i++) {
        if (model_registry[i].type == MESH_MODEL_TYPE_BATTERY) {
            if (battery_idx == index) {
                return (battery_model_state_t*)model_registry[i].runtime_state;
            }
            battery_idx++;
        }
    }
    return NULL;
}

/*
 * ============================================================================
 *                    VENDOR MODEL IMPLEMENTATION
 * ============================================================================
 */

/**
 * Vendor model runtime state
 * Stores vendor-specific configuration and message handler
 */
typedef struct {
    uint16_t company_id;                    // Company ID (0xFFFF for testing)
    uint16_t model_id;                      // Model ID (your choice)
    mesh_vendor_handler_t handler;          // Message handler callback
    void *user_data;                        // User context pointer
    uint16_t publish_addr;                  // Publication address (set by provisioner)
    esp_ble_mesh_model_pub_t pub;           // Publication context
    esp_ble_mesh_model_t *esp_model;        // ESP-IDF model structure (for opcodes)
} vendor_model_state_t;

/**
 * Find Vendor model by index
 * @param index Model index (0, 1, 2, ...)
 * @return Pointer to model state, or NULL if not found
 */
static vendor_model_state_t* find_vendor_model(uint8_t index)
{
    uint8_t vendor_idx = 0;
    for (int i = 0; i < registered_model_count; i++) {
        if (model_registry[i].type == MESH_MODEL_TYPE_VENDOR) {
            if (vendor_idx == index) {
                return (vendor_model_state_t*)model_registry[i].runtime_state;
            }
            vendor_idx++;
        }
    }
    return NULL;
}

/**
 * Initialize OnOff model
 * Called during node_init() for each configured OnOff model
 */
static esp_err_t init_onoff_model(mesh_model_config_t *config,
                                   model_registry_entry_t *registry_entry)
{
    // Allocate runtime state
    onoff_model_state_t *state = calloc(1, sizeof(onoff_model_state_t));
    if (!state) {
        ESP_LOGE(TAG, "Failed to allocate OnOff model state");
        return ESP_ERR_NO_MEM;
    }

    // Initialize state from configuration
    state->onoff = config->config.onoff.initial_state;
    state->callback = config->config.onoff.callback;
    state->user_data = config->config.onoff.user_data;

    // Initialize ESP-IDF server structure
    state->server.rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP;
    state->server.rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP;
    state->server.state.onoff = state->onoff;
    state->server.state.target_onoff = state->onoff;

    // Initialize publication context (if enabled)
    if (config->enable_publication) {
        state->pub.msg = NET_BUF_SIMPLE(2 + 3);  // Message buffer
        state->pub.update = 0;                    // No periodic publishing (0 = manual only)
        // Note: dev_role is deprecated in newer ESP-IDF, omit it
    }

    // Store state in registry
    registry_entry->runtime_state = state;

    ESP_LOGI(TAG, "OnOff model initialized (initial_state=%d)", state->onoff);
    return ESP_OK;
}

/**
 * Initialize Level model
 * Called during node_init() for each configured Level model
 */
static esp_err_t init_level_model(mesh_model_config_t *config,
                                   model_registry_entry_t *registry_entry)
{
    // Allocate runtime state
    level_model_state_t *state = calloc(1, sizeof(level_model_state_t));
    if (!state) {
        ESP_LOGE(TAG, "Failed to allocate Level model state");
        return ESP_ERR_NO_MEM;
    }

    // Initialize state from configuration
    state->level = config->config.level.initial_level;
    state->callback = config->config.level.callback;
    state->user_data = config->config.level.user_data;

    // Initialize ESP-IDF server structure
    state->server.rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP;
    state->server.rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP;
    state->server.state.level = state->level;
    state->server.state.target_level = state->level;

    // Initialize publication context (if enabled)
    if (config->enable_publication) {
        state->pub.msg = NET_BUF_SIMPLE(2 + 5);  // Message buffer for level
        state->pub.update = 0;                    // No periodic publishing (0 = manual only)
    }

    // Store state in registry
    registry_entry->runtime_state = state;

    ESP_LOGI(TAG, "Level model initialized (initial_level=%d)", state->level);
    return ESP_OK;
}

/**
 * Initialize Sensor model
 * Called during node_init() for each configured Sensor model
 */
static esp_err_t init_sensor_model(mesh_model_config_t *config,
                                    model_registry_entry_t *registry_entry)
{
    // Allocate runtime state
    sensor_model_state_t *state = calloc(1, sizeof(sensor_model_state_t));
    if (!state) {
        ESP_LOGE(TAG, "Failed to allocate Sensor model state");
        return ESP_ERR_NO_MEM;
    }

    // Store sensor configuration
    state->sensors = config->config.sensor.sensors;
    state->sensor_count = config->config.sensor.sensor_count;

    // Allocate ESP-IDF sensor states array (required by ESP-IDF)
    state->sensor_states = calloc(state->sensor_count, sizeof(esp_ble_mesh_sensor_state_t));
    if (!state->sensor_states) {
        ESP_LOGE(TAG, "Failed to allocate sensor states");
        free(state);
        return ESP_ERR_NO_MEM;
    }

    // Allocate array of buffer pointers (one per sensor)
    state->sensor_bufs = calloc(state->sensor_count, sizeof(struct net_buf_simple *));
    if (!state->sensor_bufs) {
        ESP_LOGE(TAG, "Failed to allocate sensor buffer array");
        free(state->sensor_states);
        free(state);
        return ESP_ERR_NO_MEM;
    }

    // Initialize each sensor state with property ID, descriptor, and sensor_data
    for (int i = 0; i < state->sensor_count; i++) {
        state->sensor_states[i].sensor_property_id = state->sensors[i].type;

        // Initialize descriptor (required by ESP-IDF validation)
        state->sensor_states[i].descriptor.positive_tolerance = 0;
        state->sensor_states[i].descriptor.negative_tolerance = 0;
        state->sensor_states[i].descriptor.sampling_function = 0x00;  // Unspecified
        state->sensor_states[i].descriptor.measure_period = 0;        // Not applicable
        state->sensor_states[i].descriptor.update_interval = 0;       // Not applicable

        // Allocate buffer for sensor raw value (4 bytes for int32_t sensor data)
        state->sensor_bufs[i] = calloc(1, sizeof(struct net_buf_simple) + 4);
        if (!state->sensor_bufs[i]) {
            ESP_LOGE(TAG, "Failed to allocate sensor buffer #%d", i);
            // Clean up previously allocated buffers
            for (int j = 0; j < i; j++) {
                free(state->sensor_bufs[j]);
            }
            free(state->sensor_bufs);
            free(state->sensor_states);
            free(state);
            return ESP_ERR_NO_MEM;
        }
        // Initialize the net_buf_simple structure
        state->sensor_bufs[i]->data = (uint8_t *)(state->sensor_bufs[i] + 1);  // Data after struct
        state->sensor_bufs[i]->len = 0;
        state->sensor_bufs[i]->size = 4;
        state->sensor_bufs[i]->__buf = state->sensor_bufs[i]->data;

        // Initialize sensor_data (required by ESP-IDF validation)
        state->sensor_states[i].sensor_data.format = ESP_BLE_MESH_SENSOR_DATA_FORMAT_A;
        state->sensor_states[i].sensor_data.length = 0;  // 0 means length is 1 byte
        state->sensor_states[i].sensor_data.raw_value = state->sensor_bufs[i];  // MUST NOT BE NULL

        // settings, cadence, series_column are optional (NULL/0 is fine)
    }

    // Initialize ESP-IDF server structure with state_count and states
    // Use compound literal because state_count is const
    esp_ble_mesh_sensor_srv_t temp_server = {
        .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
        .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
        .state_count = state->sensor_count,
        .states = state->sensor_states,
    };
    memcpy(&state->server, &temp_server, sizeof(esp_ble_mesh_sensor_srv_t));

    // Initialize Sensor Setup Server (REQUIRED)
    // Use compound literal because state_count is const
    esp_ble_mesh_sensor_setup_srv_t temp_setup = {
        .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
        .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
        .state_count = state->sensor_count,
        .states = state->sensor_states,
    };
    memcpy(&state->setup, &temp_setup, sizeof(esp_ble_mesh_sensor_setup_srv_t));

    // Initialize publication context for Sensor Server (if enabled)
    if (config->enable_publication) {
        // Allocate publication message buffer (must persist beyond this function)
        struct net_buf_simple *pub_msg = calloc(1, sizeof(struct net_buf_simple) + 34);
        if (!pub_msg) {
            ESP_LOGE(TAG, "Failed to allocate publication buffer");
            // Cleanup
            for (int j = 0; j < state->sensor_count; j++) {
                free(state->sensor_bufs[j]);
            }
            free(state->sensor_bufs);
            free(state->sensor_states);
            free(state);
            return ESP_ERR_NO_MEM;
        }
        pub_msg->data = (uint8_t *)(pub_msg + 1);
        pub_msg->len = 0;
        pub_msg->size = 34;
        pub_msg->__buf = pub_msg->data;
        state->pub.msg = pub_msg;
        state->pub.update = 0;  // No periodic publishing (handled by timer)
    }

    // Initialize publication context for Setup Server (ALWAYS REQUIRED)
    struct net_buf_simple *setup_msg = calloc(1, sizeof(struct net_buf_simple) + 34);
    if (!setup_msg) {
        ESP_LOGE(TAG, "Failed to allocate setup publication buffer");
        // Cleanup
        if (state->pub.msg) free(state->pub.msg);
        for (int j = 0; j < state->sensor_count; j++) {
            free(state->sensor_bufs[j]);
        }
        free(state->sensor_bufs);
        free(state->sensor_states);
        free(state);
        return ESP_ERR_NO_MEM;
    }
    setup_msg->data = (uint8_t *)(setup_msg + 1);
    setup_msg->len = 0;
    setup_msg->size = 34;
    setup_msg->__buf = setup_msg->data;
    state->setup_pub.msg = setup_msg;
    state->setup_pub.update = 0;

    // Store state in registry
    registry_entry->runtime_state = state;

    ESP_LOGI(TAG, "Sensor model initialized (%d sensors)", state->sensor_count);
    for (int i = 0; i < state->sensor_count; i++) {
        ESP_LOGI(TAG, "  Sensor #%d: type=0x%04X, period=%d ms",
                 i, state->sensors[i].type, (int)state->sensors[i].publish_period_ms);
    }

    return ESP_OK;
}

/**
 * Initialize Vendor model
 * Called during node_init() for each configured Vendor model
 */
static esp_err_t init_vendor_model(mesh_model_config_t *config,
                                    model_registry_entry_t *registry_entry)
{
    // Allocate runtime state
    vendor_model_state_t *state = calloc(1, sizeof(vendor_model_state_t));
    if (!state) {
        ESP_LOGE(TAG, "Failed to allocate Vendor model state");
        return ESP_ERR_NO_MEM;
    }

    // Store vendor configuration
    state->company_id = config->config.vendor.company_id;
    state->model_id = config->config.vendor.model_id;
    state->handler = config->config.vendor.handler;
    state->user_data = config->config.vendor.user_data;

    // Store state in registry
    registry_entry->runtime_state = state;

    ESP_LOGI(TAG, "Vendor model initialized (CID=0x%04X, MID=0x%04X)",
             state->company_id, state->model_id);

    return ESP_OK;
}

/**
 * Initialize Battery model
 * Called during node_init() for each configured Battery model
 */
static esp_err_t init_battery_model(mesh_model_config_t *config,
                                     model_registry_entry_t *registry_entry)
{
    // Allocate runtime state
    battery_model_state_t *state = calloc(1, sizeof(battery_model_state_t));
    if (!state) {
        ESP_LOGE(TAG, "Failed to allocate Battery model state");
        return ESP_ERR_NO_MEM;
    }

    // Initialize from configuration
    state->callback = config->config.battery.callback;
    state->publish_period_ms = config->config.battery.publish_period_ms;
    state->user_data = config->config.battery.user_data;
    state->battery_level = 100;  // Default to 100%

    // Initialize ESP-IDF server structure
    state->server.rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP;
    state->server.rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP;

    // Initialize publication context
    if (config->enable_publication) {
        state->pub.msg = NET_BUF_SIMPLE(2 + 8);  // Buffer for battery status
        state->pub.update = 0;                    // Manual publishing
    }

    // Store state in registry
    registry_entry->runtime_state = state;

    ESP_LOGI(TAG, "Battery model initialized (period=%d ms)", (int)state->publish_period_ms);

    return ESP_OK;
}

/*
 * ============================================================================
 *                    DYNAMIC MODEL BUILDING
 * ============================================================================
 *
 * This is the heart of the extensible architecture.
 * We build the model array dynamically based on user configuration.
 */

/**
 * Build models from configuration
 *
 * This function:
 * 1. Counts total models needed (config server + user models)
 * 2. Allocates model array
 * 3. Initializes each model
 * 4. Registers in model registry
 *
 * @param user_models Array of user-configured models
 * @param model_count Number of models in array
 * @return ESP_OK on success
 */
static esp_err_t build_models(mesh_model_config_t *user_models, uint8_t model_count)
{
    esp_err_t ret;

    // Calculate total models: separate SIG and vendor
    // SIG models: 1 (config server) + user SIG models
    // Vendor models: user vendor models only
    uint8_t total_sig = 1;  // Start with config server
    uint8_t total_vnd = 0;

    for (int i = 0; i < model_count; i++) {
        if (user_models[i].type == MESH_MODEL_TYPE_VENDOR) {
            total_vnd += 1;  // Vendor model
        } else if (user_models[i].type == MESH_MODEL_TYPE_SENSOR) {
            total_sig += 2;  // Sensor Server + Sensor Setup Server
        } else {
            total_sig += 1;  // Other SIG models (OnOff, Level, Battery)
        }
    }
    sig_model_count = total_sig;
    vnd_model_count = total_vnd;

    // Allocate SIG models array
    dynamic_sig_models = calloc(sig_model_count, sizeof(esp_ble_mesh_model_t));
    if (!dynamic_sig_models) {
        ESP_LOGE(TAG, "Failed to allocate SIG models array");
        return ESP_ERR_NO_MEM;
    }

    // Allocate vendor models array (if needed)
    if (vnd_model_count > 0) {
        dynamic_vnd_models = calloc(vnd_model_count, sizeof(esp_ble_mesh_model_t));
        if (!dynamic_vnd_models) {
            ESP_LOGE(TAG, "Failed to allocate vendor models array");
            free(dynamic_sig_models);
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI(TAG, "Building %d SIG models + %d vendor models (%d user + 1 config server)",
             sig_model_count, vnd_model_count, model_count);

    // Model 0: Configuration Server (always present - mandatory)
    esp_ble_mesh_model_t cfg_model = ESP_BLE_MESH_MODEL_CFG_SRV(&config_server);
    memcpy(&dynamic_sig_models[0], &cfg_model, sizeof(esp_ble_mesh_model_t));

    // Build user models
    uint8_t sig_slot = 1;   // Start after config server (slot 0)
    uint8_t vnd_slot = 0;   // Vendor models start at 0

    for (int i = 0; i < model_count; i++) {
        mesh_model_config_t *config = &user_models[i];
        model_registry_entry_t *registry = &model_registry[registered_model_count];

        // Store user configuration
        memcpy(&registry->user_config, config, sizeof(mesh_model_config_t));
        registry->type = config->type;

        // Will be set later based on model type
        registry->esp_model = NULL;

        switch (config->type) {
        case MESH_MODEL_TYPE_ONOFF:
            // Initialize OnOff model
            ret = init_onoff_model(config, registry);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to init OnOff model");
                return ret;
            }

            // Get runtime state
            onoff_model_state_t *onoff_state = (onoff_model_state_t*)registry->runtime_state;

            // Build ESP-IDF model structure
            esp_ble_mesh_model_pub_t *onoff_pub_ctx = config->enable_publication ? &onoff_state->pub : NULL;
            esp_ble_mesh_model_t onoff_model = ESP_BLE_MESH_MODEL_GEN_ONOFF_SRV(onoff_pub_ctx, &onoff_state->server);
            memcpy(&dynamic_sig_models[sig_slot], &onoff_model, sizeof(esp_ble_mesh_model_t));

            // Store pointer to ESP-IDF model for publishing
            onoff_state->esp_model = &dynamic_sig_models[sig_slot];
            registry->esp_model = &dynamic_sig_models[sig_slot];

            ESP_LOGI(TAG, "Added Generic OnOff Server model #%d", registered_model_count);
            sig_slot++;
            break;

        case MESH_MODEL_TYPE_LEVEL:
            // Initialize Level model
            ret = init_level_model(config, registry);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to init Level model");
                return ret;
            }

            // Get runtime state
            level_model_state_t *level_state = (level_model_state_t*)registry->runtime_state;

            // Build ESP-IDF model structure
            esp_ble_mesh_model_pub_t *level_pub_ctx = config->enable_publication ? &level_state->pub : NULL;
            esp_ble_mesh_model_t level_model = ESP_BLE_MESH_MODEL_GEN_LEVEL_SRV(level_pub_ctx, &level_state->server);
            memcpy(&dynamic_sig_models[sig_slot], &level_model, sizeof(esp_ble_mesh_model_t));

            // Store pointer to ESP-IDF model for publishing
            level_state->esp_model = &dynamic_sig_models[sig_slot];
            registry->esp_model = &dynamic_sig_models[sig_slot];

            ESP_LOGI(TAG, "Added Generic Level Server model #%d", registered_model_count);
            sig_slot++;
            break;

        case MESH_MODEL_TYPE_SENSOR:
            // Initialize Sensor model
            ret = init_sensor_model(config, registry);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to init Sensor model");
                return ret;
            }

            // Get runtime state
            sensor_model_state_t *sensor_state = (sensor_model_state_t*)registry->runtime_state;

            // Build ESP-IDF model structures
            // IMPORTANT: Sensor model requires TWO models: Server + Setup Server
            esp_ble_mesh_model_pub_t *sensor_pub_ctx = config->enable_publication ? &sensor_state->pub : NULL;
            esp_ble_mesh_model_t sensor_model = ESP_BLE_MESH_MODEL_SENSOR_SRV(sensor_pub_ctx, &sensor_state->server);
            // Setup Server MUST have publication context (ESP-IDF requirement)
            esp_ble_mesh_model_t setup_model = ESP_BLE_MESH_MODEL_SENSOR_SETUP_SRV(&sensor_state->setup_pub, &sensor_state->setup);

            // Add Sensor Server to sig_slot
            memcpy(&dynamic_sig_models[sig_slot], &sensor_model, sizeof(esp_ble_mesh_model_t));
            sensor_state->esp_model = &dynamic_sig_models[sig_slot];  // Store model pointer for publishing
            registry->esp_model = &dynamic_sig_models[sig_slot];
            sig_slot++;

            // Add Sensor Setup Server to sig_slot+1
            memcpy(&dynamic_sig_models[sig_slot], &setup_model, sizeof(esp_ble_mesh_model_t));
            sig_slot++;

            ESP_LOGI(TAG, "Added Sensor Server + Setup Server model #%d", registered_model_count);
            break;

        case MESH_MODEL_TYPE_VENDOR:
            // Initialize Vendor model
            ret = init_vendor_model(config, registry);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to init Vendor model");
                return ret;
            }

            // Get runtime state
            vendor_model_state_t *vendor_state = (vendor_model_state_t*)registry->runtime_state;

            // Create vendor operation array with our IMU opcodes
            // IMPORTANT: Vendor models MUST have opcodes defined for all messages they send.
            // Opcode 0xC00001 = ESP_BLE_MESH_MODEL_OP_3(0xC0, 0x0001) - Accelerometer
            // Opcode 0xC00002 = ESP_BLE_MESH_MODEL_OP_3(0xC0, 0x0002) - Gyroscope
            static esp_ble_mesh_model_op_t vendor_op[] = {
                ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_MODEL_OP_3(0xC0, 0x0001), 0),  // Accel opcode, min length 0
                ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_MODEL_OP_3(0xC0, 0x0002), 0),  // Gyro opcode, min length 0
                ESP_BLE_MESH_MODEL_OP_END,
            };

            // Build ESP-IDF vendor model structure
            // Vendor models use a different macro with company_id and model_id
            esp_ble_mesh_model_pub_t *pub_ctx = config->enable_publication ? &vendor_state->pub : NULL;
            esp_ble_mesh_model_t vendor_model = ESP_BLE_MESH_VENDOR_MODEL(
                vendor_state->company_id,
                vendor_state->model_id,
                vendor_op,  // Operation array with IMU data opcode
                pub_ctx,    // Publication context (if enabled)
                NULL        // No user_data
            );
            memcpy(&dynamic_vnd_models[vnd_slot], &vendor_model, sizeof(esp_ble_mesh_model_t));

            // Store ESP model pointer for later use
            vendor_state->esp_model = &dynamic_vnd_models[vnd_slot];
            registry->esp_model = &dynamic_vnd_models[vnd_slot];

            ESP_LOGI(TAG, "Added Vendor model #%d (CID=0x%04X, MID=0x%04X)",
                     registered_model_count, vendor_state->company_id, vendor_state->model_id);
            vnd_slot++;
            break;

        case MESH_MODEL_TYPE_BATTERY:
            // Initialize Battery model
            ret = init_battery_model(config, registry);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to init Battery model");
                return ret;
            }

            // Get runtime state
            battery_model_state_t *battery_state = (battery_model_state_t*)registry->runtime_state;

            // Build ESP-IDF model structure
            esp_ble_mesh_model_pub_t *battery_pub_ctx = config->enable_publication ? &battery_state->pub : NULL;
            esp_ble_mesh_model_t battery_model = ESP_BLE_MESH_MODEL_GEN_BATTERY_SRV(battery_pub_ctx, &battery_state->server);
            memcpy(&dynamic_sig_models[sig_slot], &battery_model, sizeof(esp_ble_mesh_model_t));

            // Store pointer to ESP-IDF model for publishing
            battery_state->esp_model = &dynamic_sig_models[sig_slot];
            registry->esp_model = &dynamic_sig_models[sig_slot];

            ESP_LOGI(TAG, "Added Battery Server model #%d", registered_model_count);
            sig_slot++;
            break;

        default:
            ESP_LOGE(TAG, "Unknown model type: %d", config->type);
            return ESP_ERR_INVALID_ARG;
        }

        registered_model_count++;
    }

    return ESP_OK;
}

/**
 * Build element structure
 * Creates the element that contains all our models
 */
static esp_err_t build_element(void)
{
    // Allocate single element (simple nodes have 1 element)
    elements = calloc(1, sizeof(esp_ble_mesh_elem_t));
    if (!elements) {
        ESP_LOGE(TAG, "Failed to allocate element");
        return ESP_ERR_NO_MEM;
    }

    // Configure element - use initializer to avoid const issues
    esp_ble_mesh_elem_t elem = {
        .location = 0x0000,
        .sig_model_count = sig_model_count,
        .sig_models = dynamic_sig_models,
        .vnd_model_count = vnd_model_count,
        .vnd_models = dynamic_vnd_models,
    };
    memcpy(&elements[0], &elem, sizeof(esp_ble_mesh_elem_t));

    // Update composition data
    composition.elements = elements;
    composition.element_count = 1;

    ESP_LOGI(TAG, "Element created with %d SIG models and %d vendor models",
             sig_model_count, vnd_model_count);
    return ESP_OK;
}

/*
 * ============================================================================
 *                    MESH CALLBACKS (from original)
 * ============================================================================
 */

static void mesh_generic_server_cb(esp_ble_mesh_generic_server_cb_event_t event,
                                    esp_ble_mesh_generic_server_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_GENERIC_SERVER_STATE_CHANGE_EVT:
        switch (param->ctx.recv_op) {
        case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET:
        case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK:
        {
            // Find which OnOff model received this message
            // For now, assume first OnOff model (Phase 1 limitation)
            onoff_model_state_t *state = find_onoff_model(0);
            if (state) {
                uint8_t new_state = param->value.state_change.onoff_set.onoff;
                state->onoff = new_state;
                state->server.state.onoff = new_state;
                state->server.state.target_onoff = new_state;

                ESP_LOGI(TAG, "OnOff state changed to: %d", new_state);

                // Notify application
                if (state->callback) {
                    state->callback(new_state, state->user_data);
                }
            }
            break;
        }
        case ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_SET:
        case ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_SET_UNACK:
        {
            // Find which Level model received this message
            // For now, assume first Level model
            level_model_state_t *state = find_level_model(0);
            if (state) {
                int16_t new_level = param->value.state_change.level_set.level;
                state->level = new_level;
                state->server.state.level = new_level;
                state->server.state.target_level = new_level;

                ESP_LOGI(TAG, "Level state changed to: %d", new_level);

                // Notify application
                if (state->callback) {
                    state->callback(new_level, state->user_data);
                }
            }
            break;
        }
        case ESP_BLE_MESH_MODEL_OP_GEN_DELTA_SET:
        case ESP_BLE_MESH_MODEL_OP_GEN_DELTA_SET_UNACK:
        case ESP_BLE_MESH_MODEL_OP_GEN_MOVE_SET:
        case ESP_BLE_MESH_MODEL_OP_GEN_MOVE_SET_UNACK:
        {
            // Delta and Move operations - just log for now
            // ESP-IDF will auto-respond based on current state
            ESP_LOGI(TAG, "Level delta/move operation received (auto-handled by stack)");
            break;
        }
        default:
            break;
        }
        break;

    case ESP_BLE_MESH_GENERIC_SERVER_RECV_GET_MSG_EVT:
        ESP_LOGI(TAG, "Received Generic Get message");
        break;

    case ESP_BLE_MESH_GENERIC_SERVER_RECV_SET_MSG_EVT:
        ESP_LOGI(TAG, "Received Generic Set message");
        break;

    default:
        break;
    }
}

static void mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                                   esp_ble_mesh_cfg_server_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT:
        switch (param->ctx.recv_op) {
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
            ESP_LOGI(TAG, "AppKey added: NetKeyIndex=0x%04x, AppKeyIndex=0x%04x",
                     param->value.state_change.appkey_add.net_idx,
                     param->value.state_change.appkey_add.app_idx);

            // Notify application
            if (app_callbacks.config_complete) {
                app_callbacks.config_complete(param->value.state_change.appkey_add.app_idx);
            }
            break;

        case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
            ESP_LOGI(TAG, "Model app bind: ElementAddr=0x%04x, AppKeyIndex=0x%04x, ModelID=0x%04x",
                     param->value.state_change.mod_app_bind.element_addr,
                     param->value.state_change.mod_app_bind.app_idx,
                     param->value.state_change.mod_app_bind.model_id);
            break;

        case ESP_BLE_MESH_MODEL_OP_MODEL_PUB_SET:
            ESP_LOGI(TAG, "Model publication set: ElementAddr=0x%04x, PublishAddr=0x%04x, ModelID=0x%04x",
                     param->value.state_change.mod_pub_set.element_addr,
                     param->value.state_change.mod_pub_set.pub_addr,
                     param->value.state_change.mod_pub_set.model_id);
            ESP_LOGI(TAG, "Publication configured! Sensor data will now be published");
            break;

        default:
            break;
        }
        break;

    default:
        break;
    }
}

static void mesh_sensor_server_cb(esp_ble_mesh_sensor_server_cb_event_t event,
                                   esp_ble_mesh_sensor_server_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_SENSOR_SERVER_RECV_GET_MSG_EVT:
        ESP_LOGI(TAG, "Sensor Get received - opcode: 0x%04x", param->ctx.recv_op);
        // Find the sensor model
        sensor_model_state_t *state = find_sensor_model(0);
        if (state && param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_SENSOR_GET) {
            // Client is requesting sensor data
            // The auto-response will be handled by reading current sensor values
            ESP_LOGI(TAG, "Sensor data requested for %d sensors", state->sensor_count);
        }
        break;

    case ESP_BLE_MESH_SENSOR_SERVER_RECV_SET_MSG_EVT:
        ESP_LOGI(TAG, "Sensor Set received");
        break;

    default:
        ESP_LOGI(TAG, "Sensor server event: %d", event);
        break;
    }
}

/*
 * ════════════════════════════════════════════════════════════════════════
 *                     CUSTOM MODEL (VENDOR) CALLBACK
 * ════════════════════════════════════════════════════════════════════════
 *
 * Handles vendor model messages (both direct unicast and published).
 * Dispatches to user-registered vendor handlers.
 */
static void mesh_custom_model_cb(esp_ble_mesh_model_cb_event_t event,
                                 esp_ble_mesh_model_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_MODEL_OPERATION_EVT:
        /*
         * Received a vendor message (direct unicast or published to us)
         */
        {
            uint32_t opcode = param->model_operation.opcode;
            uint16_t src_addr = param->model_operation.ctx->addr;
            uint16_t length = param->model_operation.length;
            uint8_t *data = param->model_operation.msg;
            esp_ble_mesh_model_t *model = param->model_operation.model;

            ESP_LOGI(TAG, "📩 Vendor message recv: opcode=0x%06" PRIx32 " from=0x%04x len=%d",
                     opcode, src_addr, length);

            // Find the vendor model in our registry
            for (int i = 0; i < registered_model_count; i++) {
                if (model_registry[i].type == MESH_MODEL_TYPE_VENDOR) {
                    vendor_model_state_t *vstate = (vendor_model_state_t*)model_registry[i].runtime_state;

                    if (vstate && vstate->esp_model == model) {
                        // Call user's vendor handler if registered
                        if (vstate->handler) {
                            vstate->handler(opcode, data, length,
                                           param->model_operation.ctx,
                                           vstate->user_data);
                        } else {
                            ESP_LOGW(TAG, "No handler registered for vendor model CID=0x%04X MID=0x%04X",
                                     vstate->company_id, vstate->model_id);
                        }
                        break;
                    }
                }
            }
        }
        break;

    case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
        if (param->model_send_comp.err_code) {
            ESP_LOGE(TAG, "Vendor send failed: opcode=0x%06" PRIx32 " err=%d",
                     param->model_send_comp.opcode, param->model_send_comp.err_code);
        } else {
            ESP_LOGD(TAG, "Vendor send complete: opcode=0x%06" PRIx32,
                     param->model_send_comp.opcode);
        }
        break;

    case ESP_BLE_MESH_CLIENT_MODEL_RECV_PUBLISH_MSG_EVT:
        /*
         * Received a published vendor message (for vendor client models)
         */
        {
            uint32_t opcode = param->client_recv_publish_msg.opcode;
            uint16_t src_addr = param->client_recv_publish_msg.ctx->addr;
            uint16_t length = param->client_recv_publish_msg.length;
            uint8_t *data = param->client_recv_publish_msg.msg;

            ESP_LOGI(TAG, "📦 Vendor publish recv: opcode=0x%06" PRIx32 " from=0x%04x len=%d",
                     opcode, src_addr, length);

            // TODO: Dispatch to vendor client handlers when implemented
        }
        break;

    default:
        ESP_LOGD(TAG, "Custom model event: %d", event);
        break;
    }
}

static void mesh_prov_cb(esp_ble_mesh_prov_cb_event_t event,
                        esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "BLE Mesh provisioning registered, err_code %d",
                 param->prov_register_comp.err_code);
        break;

    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "Node provisioning enabled, err_code %d",
                 param->node_prov_enable_comp.err_code);
        break;

    case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
        ESP_LOGI(TAG, "Provisioning link opened with bearer: %s",
                 param->node_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        break;

    case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
        ESP_LOGI(TAG, "Provisioning link closed with bearer: %s",
                 param->node_prov_link_close.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        break;

    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        ESP_LOGI(TAG, "Provisioning complete!");
        ESP_LOGI(TAG, "  Unicast address: 0x%04x", param->node_prov_complete.addr);
        ESP_LOGI(TAG, "  NetKey index: 0x%04x", param->node_prov_complete.net_idx);

        // Notify application
        if (app_callbacks.provisioned) {
            app_callbacks.provisioned(param->node_prov_complete.addr);
        }
        break;

    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        ESP_LOGI(TAG, "Node reset - returning to unprovisioned state");

        // Notify application
        if (app_callbacks.reset) {
            app_callbacks.reset();
        }
        break;

    default:
        break;
    }
}

/*
 * ============================================================================
 *                    HELPER FUNCTIONS (from original)
 * ============================================================================
 */

static void generate_dev_uuid(const uint8_t prefix[2])
{
    const uint8_t *mac = esp_bt_dev_get_address();
    memset(dev_uuid, 0, 16);
    dev_uuid[0] = prefix[0];
    dev_uuid[1] = prefix[1];
    memcpy(dev_uuid + 2, mac, 6);
    ESP_LOGI(TAG, "Generated UUID with prefix [0x%02x 0x%02x]", prefix[0], prefix[1]);
}

static esp_err_t bluetooth_init(void)
{
    esp_err_t ret;

    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller release classic bt memory failed");
        return ret;
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller initialize failed");
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller enable failed");
        return ret;
    }

    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ret = esp_bluedroid_init_with_cfg(&bluedroid_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth bluedroid init failed");
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth bluedroid enable failed");
        return ret;
    }

    ESP_LOGI(TAG, "Bluetooth initialized");
    return ESP_OK;
}

/*
 * ============================================================================
 *                    PUBLIC API IMPLEMENTATION
 * ============================================================================
 */

esp_err_t node_init(const node_config_t *config)
{
    esp_err_t ret;

    if (!config) {
        ESP_LOGE(TAG, "Config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "=== BLE Mesh Node V2 Initialization (Extensible) ===");

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS flash init failed");
        return ret;
    }

    // Initialize Bluetooth
    ret = bluetooth_init();
    if (ret != ESP_OK) {
        return ret;
    }

    // Generate UUID
    generate_dev_uuid(config->device_uuid_prefix);
    ESP_LOGI(TAG, "Device UUID: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
             dev_uuid[0], dev_uuid[1], dev_uuid[2], dev_uuid[3],
             dev_uuid[4], dev_uuid[5], dev_uuid[6], dev_uuid[7],
             dev_uuid[8], dev_uuid[9], dev_uuid[10], dev_uuid[11],
             dev_uuid[12], dev_uuid[13], dev_uuid[14], dev_uuid[15]);

    // Store device name
    device_name = config->device_name ? config->device_name : "ESP-Mesh-Node";

    // Store callbacks
    memcpy(&app_callbacks, &config->callbacks, sizeof(node_callbacks_t));

    // Build models from configuration
    if (config->models && config->model_count > 0) {
        ret = build_models(config->models, config->model_count);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to build models");
            return ret;
        }
    } else {
        ESP_LOGW(TAG, "No models configured! Only Config Server will be present.");
        // Build with just config server
        sig_model_count = 1;
        vnd_model_count = 0;
        dynamic_sig_models = calloc(1, sizeof(esp_ble_mesh_model_t));
        if (!dynamic_sig_models) {
            return ESP_ERR_NO_MEM;
        }
        esp_ble_mesh_model_t cfg_model = ESP_BLE_MESH_MODEL_CFG_SRV(&config_server);
        memcpy(&dynamic_sig_models[0], &cfg_model, sizeof(esp_ble_mesh_model_t));
        dynamic_vnd_models = NULL;
    }

    // Build element
    ret = build_element();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build element");
        return ret;
    }

    // Register callbacks
    esp_ble_mesh_register_prov_callback(mesh_prov_cb);
    esp_ble_mesh_register_config_server_callback(mesh_config_server_cb);
    esp_ble_mesh_register_generic_server_callback(mesh_generic_server_cb);
    esp_ble_mesh_register_sensor_server_callback(mesh_sensor_server_cb);
    esp_ble_mesh_register_custom_model_callback(mesh_custom_model_cb);

    // Initialize provision structure
    esp_ble_mesh_prov_t temp_prov = {
        .uuid = dev_uuid,
#if CONFIG_BLE_MESH_PROVISIONER
        .prov_uuid = dev_uuid,
        .prov_unicast_addr = 0,
        .prov_start_address = 0,
        .prov_attention = 0x00,
        .prov_algorithm = 0x00,
        .prov_pub_key_oob = 0x00,
        .prov_static_oob_val = NULL,
        .prov_static_oob_len = 0x00,
        .flags = 0x00,
        .iv_index = 0x00,
#else
        .output_size = 0,
        .output_actions = 0,
#endif
    };
    memcpy(&provision, &temp_prov, sizeof(provision));

    // Initialize BLE Mesh
    ret = esp_ble_mesh_init(&provision, &composition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE Mesh init failed (err %d)", ret);
        return ret;
    }

    // Set device name
    ret = esp_ble_mesh_set_unprovisioned_device_name(device_name);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set device name (err %d)", ret);
    }

    ESP_LOGI(TAG, "BLE Mesh Node initialized successfully");
    ESP_LOGI(TAG, "  Device name: %s", device_name);
    ESP_LOGI(TAG, "  Total models: %d SIG + %d vendor", sig_model_count, vnd_model_count);
    ESP_LOGI(TAG, "  Registered models: %d", registered_model_count);

    return ESP_OK;
}

esp_err_t node_start(void)
{
    esp_err_t ret;

    vTaskDelay(pdMS_TO_TICKS(100));

    ret = esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable mesh node provisioning");
        return ret;
    }

    ESP_LOGI(TAG, "BLE Mesh Node started - broadcasting beacons");
    ESP_LOGI(TAG, "Waiting to be provisioned...");
    return ESP_OK;
}

/*
 * ============================================================================
 *                    MODEL API FUNCTIONS
 * ============================================================================
 */

int mesh_model_get_onoff(uint8_t model_index)
{
    onoff_model_state_t *state = find_onoff_model(model_index);
    if (!state) {
        ESP_LOGW(TAG, "OnOff model #%d not found", model_index);
        return -1;
    }
    return state->onoff;
}

esp_err_t mesh_model_set_onoff(uint8_t model_index, uint8_t onoff, bool publish)
{
    onoff_model_state_t *state = find_onoff_model(model_index);
    if (!state) {
        ESP_LOGE(TAG, "OnOff model #%d not found", model_index);
        return ESP_ERR_NOT_FOUND;
    }

    state->onoff = onoff;
    state->server.state.onoff = onoff;
    state->server.state.target_onoff = onoff;

    // Notify application
    if (state->callback) {
        state->callback(onoff, state->user_data);
    }

    ESP_LOGI(TAG, "OnOff model #%d set to: %d", model_index, onoff);

    // Publish if requested
    if (publish) {
        return mesh_model_publish_onoff(model_index, onoff);
    }

    return ESP_OK;
}

/*
 * ════════════════════════════════════════════════════════════════════════════
 *                    PUBLISH ONOFF STATE TO BLE MESH NETWORK
 * ════════════════════════════════════════════════════════════════════════════
 *
 * Publishes the OnOff state to the network, notifying other devices.
 *
 * WHY PUBLISH ONOFF STATE?
 * ------------------------
 * When a light changes state (locally via button or remotely via command),
 * other devices may need to know:
 *   - Status displays showing which lights are on
 *   - Linked lights that should mirror this one
 *   - Control panels updating their UI
 *
 * MESSAGE FORMAT:
 * ---------------
 * Generic OnOff Status message (BLE Mesh Model Spec):
 *   Opcode: 0x8204 (GENERIC_ONOFF_STATUS)
 *   Payload: [present_onoff] [target_onoff (optional)] [remaining_time (optional)]
 *
 * For simple on/off (no transitions), we send just 1 byte: present_onoff
 *
 * @param model_index Which OnOff model (usually 0)
 * @param onoff State to publish (0 = OFF, 1 = ON)
 * @return ESP_OK on success
 */
esp_err_t mesh_model_publish_onoff(uint8_t model_index, uint8_t onoff)
{
    onoff_model_state_t *state = find_onoff_model(model_index);
    if (!state) {
        ESP_LOGE(TAG, "OnOff model #%d not found", model_index);
        return ESP_ERR_NOT_FOUND;
    }

    // Check if publication is configured
    if (state->pub.publish_addr == ESP_BLE_MESH_ADDR_UNASSIGNED) {
        ESP_LOGD(TAG, "Publication not configured for OnOff model #%d", model_index);
        return ESP_ERR_INVALID_STATE;
    }

    // Update state
    state->onoff = onoff;
    state->server.state.onoff = onoff;
    state->server.state.target_onoff = onoff;

    // Prepare message buffer
    struct net_buf_simple *msg = state->pub.msg;
    if (!msg) {
        ESP_LOGE(TAG, "Publication message buffer not allocated");
        return ESP_ERR_NO_MEM;
    }

    net_buf_simple_reset(msg);

    // Add OnOff state (1 byte)
    net_buf_simple_add_u8(msg, onoff);

    ESP_LOGI(TAG, "📤 Publishing OnOff state: %d", onoff);

    // Build message context
    esp_ble_mesh_msg_ctx_t pub_ctx = {
        .net_idx = 0,                      // Primary network key
        .app_idx = 0,                      // Primary application key
        .addr = state->pub.publish_addr,   // Where to send (configured by provisioner)
        .send_ttl = 7,                     // Allow up to 7 relay hops
        .send_rel = false,                 // Unacknowledged (best for status updates)
    };

    // Publish using Generic OnOff Status opcode
    esp_err_t err = esp_ble_mesh_server_model_send_msg(
        state->esp_model,                           // OnOff Server model
        &pub_ctx,                                   // Message context
        ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_STATUS,     // Opcode = 0x8204
        msg->len,                                   // Payload length (1 byte)
        msg->data);                                 // Payload

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to publish OnOff state, err %d", err);
        return err;
    }

    ESP_LOGI(TAG, "📡 Published OnOff state: %d", onoff);
    return ESP_OK;
}

int16_t mesh_model_get_level(uint8_t model_index)
{
    level_model_state_t *state = find_level_model(model_index);
    if (!state) {
        ESP_LOGW(TAG, "Level model #%d not found", model_index);
        return INT16_MIN;  // Return minimum level as error indicator
    }
    return state->level;
}

esp_err_t mesh_model_set_level(uint8_t model_index, int16_t level, bool publish)
{
    level_model_state_t *state = find_level_model(model_index);
    if (!state) {
        ESP_LOGE(TAG, "Level model #%d not found", model_index);
        return ESP_ERR_NOT_FOUND;
    }

    state->level = level;
    state->server.state.level = level;
    state->server.state.target_level = level;

    // Notify application
    if (state->callback) {
        state->callback(level, state->user_data);
    }

    ESP_LOGI(TAG, "Level model #%d set to: %d", model_index, level);

    // Publish if requested
    if (publish) {
        return mesh_model_publish_level(model_index, level);
    }

    return ESP_OK;
}

/*
 * ════════════════════════════════════════════════════════════════════════════
 *                    PUBLISH LEVEL STATE TO BLE MESH NETWORK
 * ════════════════════════════════════════════════════════════════════════════
 *
 * Publishes the Level state to the network (e.g., brightness, position).
 *
 * WHY PUBLISH LEVEL STATE?
 * ------------------------
 * Level is used for:
 *   - Light brightness: -32768 (off) to +32767 (max brightness)
 *   - Position: -32768 (fully closed) to +32767 (fully open)
 *   - Volume, temperature setpoints, etc.
 *
 * Other devices that need to know the level:
 *   - Control panels showing current brightness
 *   - Synchronized lights that should match brightness
 *   - Status displays
 *
 * MESSAGE FORMAT:
 * ---------------
 * Generic Level Status message (BLE Mesh Model Spec):
 *   Opcode: 0x8208 (GENERIC_LEVEL_STATUS)
 *   Payload: [present_level (2 bytes)] [target_level (2 bytes, optional)] [remaining_time (1 byte, optional)]
 *
 * For simple level (no transitions), we send 2 bytes: present_level (little-endian)
 *
 * @param model_index Which Level model (usually 0)
 * @param level Level to publish (-32768 to +32767)
 * @return ESP_OK on success
 */
esp_err_t mesh_model_publish_level(uint8_t model_index, int16_t level)
{
    level_model_state_t *state = find_level_model(model_index);
    if (!state) {
        ESP_LOGE(TAG, "Level model #%d not found", model_index);
        return ESP_ERR_NOT_FOUND;
    }

    // Check if publication is configured
    if (state->pub.publish_addr == ESP_BLE_MESH_ADDR_UNASSIGNED) {
        ESP_LOGD(TAG, "Publication not configured for Level model #%d", model_index);
        return ESP_ERR_INVALID_STATE;
    }

    // Update state
    state->level = level;
    state->server.state.level = level;
    state->server.state.target_level = level;

    // Prepare message buffer
    struct net_buf_simple *msg = state->pub.msg;
    if (!msg) {
        ESP_LOGE(TAG, "Publication message buffer not allocated");
        return ESP_ERR_NO_MEM;
    }

    net_buf_simple_reset(msg);

    // Add Level state (2 bytes, little-endian)
    net_buf_simple_add_le16(msg, (uint16_t)level);

    ESP_LOGI(TAG, "📤 Publishing Level state: %d", level);

    // Build message context
    esp_ble_mesh_msg_ctx_t pub_ctx = {
        .net_idx = 0,                      // Primary network key
        .app_idx = 0,                      // Primary application key
        .addr = state->pub.publish_addr,   // Where to send (configured by provisioner)
        .send_ttl = 7,                     // Allow up to 7 relay hops
        .send_rel = false,                 // Unacknowledged (best for status updates)
    };

    // Publish using Generic Level Status opcode
    esp_err_t err = esp_ble_mesh_server_model_send_msg(
        state->esp_model,                           // Level Server model
        &pub_ctx,                                   // Message context
        ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_STATUS,     // Opcode = 0x8208
        msg->len,                                   // Payload length (2 bytes)
        msg->data);                                 // Payload

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to publish Level state, err %d", err);
        return err;
    }

    ESP_LOGI(TAG, "📡 Published Level state: %d", level);
    return ESP_OK;
}

esp_err_t mesh_model_read_sensor(uint8_t model_index, uint16_t sensor_type, int32_t *value_out)
{
    sensor_model_state_t *state = find_sensor_model(model_index);
    if (!state) {
        ESP_LOGE(TAG, "Sensor model #%d not found", model_index);
        return ESP_ERR_NOT_FOUND;
    }

    // Find the sensor by type
    for (int i = 0; i < state->sensor_count; i++) {
        if (state->sensors[i].type == sensor_type) {
            // Call user's read callback
            if (state->sensors[i].read) {
                esp_err_t ret = state->sensors[i].read(sensor_type, value_out, state->sensors[i].user_data);
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "Sensor 0x%04X read: %d", sensor_type, (int)*value_out);
                }
                return ret;
            } else {
                ESP_LOGW(TAG, "Sensor 0x%04X has no read callback", sensor_type);
                return ESP_ERR_INVALID_STATE;
            }
        }
    }

    ESP_LOGW(TAG, "Sensor type 0x%04X not found in model #%d", sensor_type, model_index);
    return ESP_ERR_NOT_FOUND;
}

/*
 * ════════════════════════════════════════════════════════════════════════════
 *                    PUBLISH SENSOR DATA TO BLE MESH NETWORK
 * ════════════════════════════════════════════════════════════════════════════
 *
 * This function publishes a single sensor's value to the mesh network.
 *
 * WHAT IS "PUBLISHING" IN BLE MESH?
 * ----------------------------------
 * Publishing is how BLE Mesh devices BROADCAST their state to the network.
 * Instead of waiting for someone to ask "what's your temperature?", the sensor
 * proactively announces "my temperature is 25.3°C" to anyone who's listening.
 *
 * KEY CONCEPTS:
 * -------------
 * 1. PUBLISH ADDRESS: Where the message goes (configured during provisioning)
 *    - 0x0001 = Provisioner (our gateway)
 *    - 0xC000-0xFFFF = Group addresses (multiple subscribers)
 *
 * 2. PUBLICATION PERIOD: How often to auto-publish (0 = manual only)
 *    - We set this to 0 and manually call this function when we want to publish
 *    - This gives us control over exactly when data is sent
 *
 * 3. MODEL-SPECIFIC ENCODING: Each model type has its own message format
 *    - Sensor Server uses MPID (Marshalled Property ID) format
 *    - This is defined in the BLE Mesh Model spec, not the core spec
 *
 * HOW THIS WORKS:
 * ---------------
 * 1. Look up the sensor by type (e.g., 0x5001 = Accel X)
 * 2. Call user's callback to READ the current value from hardware
 * 3. Format the data according to BLE Mesh Sensor Server spec (MPID)
 * 4. Send the formatted message using esp_ble_mesh_server_model_send_msg()
 *
 * @param model_index Which Sensor Server model (usually 0)
 * @param sensor_type Which sensor to publish (e.g., SENSOR_ACCEL_X)
 * @return ESP_OK on success
 */
esp_err_t mesh_model_publish_sensor(uint8_t model_index, uint16_t sensor_type)
{
    // Find our Sensor Server model's runtime state
    sensor_model_state_t *state = find_sensor_model(model_index);
    if (!state) {
        ESP_LOGE(TAG, "Sensor model #%d not found", model_index);
        return ESP_ERR_NOT_FOUND;
    }

    /*
     * FINDING THE SENSOR IN OUR ARRAY
     * ================================
     *
     * A single Sensor Server model can support MULTIPLE sensors.
     * For example, our IMU node has 6 sensors:
     *   - 0x5001: Accelerometer X
     *   - 0x5002: Accelerometer Y
     *   - 0x5003: Accelerometer Z
     *   - 0x5004: Gyroscope X
     *   - 0x5005: Gyroscope Y
     *   - 0x5006: Gyroscope Z
     *
     * We need to find which index in the sensors[] array matches
     * the sensor_type we want to publish.
     */
    int sensor_idx = -1;
    for (int i = 0; i < state->sensor_count; i++) {
        if (state->sensors[i].type == sensor_type) {
            sensor_idx = i;
            break;
        }
    }

    if (sensor_idx < 0) {
        ESP_LOGW(TAG, "Sensor type 0x%04X not found", sensor_type);
        return ESP_ERR_NOT_FOUND;
    }

    /*
     * READING THE SENSOR VALUE
     * =========================
     *
     * Call the user-provided callback to get the CURRENT sensor value.
     *
     * WHY A CALLBACK?
     * ---------------
     * The BLE mesh component doesn't know HOW to read your sensors.
     * It could be:
     *   - Reading an I2C sensor (MPU6050, BME280, etc.)
     *   - Reading an ADC pin (analog temperature sensor)
     *   - Getting data from another task (pre-computed values)
     *
     * The callback abstracts this away. You provide a function that
     * returns the value, and we'll format/send it for you.
     *
     * EXAMPLE CALLBACK:
     * -----------------
     * esp_err_t read_accel_x(uint16_t sensor_type, int32_t *value_out, void *user_data)
     * {
     *     auto imu_data = M5.Imu.getImuData();  // Read from MPU6050
     *     *value_out = (int32_t)(imu_data.accel.x * 1000.0f);  // Convert to mg
     *     return ESP_OK;
     * }
     */
    int32_t sensor_value = 0;
    if (state->sensors[sensor_idx].read) {
        esp_err_t ret = state->sensors[sensor_idx].read(sensor_type, &sensor_value,
                                                         state->sensors[sensor_idx].user_data);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read sensor 0x%04X", sensor_type);
            return ret;
        }
    }

    /*
     * PREPARE THE SENSOR VALUE BUFFER
     * ================================
     *
     * We need to convert our int32_t sensor value into little-endian bytes.
     * ESP-IDF provides buffers and helper functions for this.
     *
     * THE raw_value BUFFER:
     * ---------------------
     * This is a network buffer (net_buf_simple) that holds the raw sensor data.
     * For each sensor, ESP-IDF allocates one of these buffers during init.
     *
     * It's separate from the final message buffer because:
     *   1. The raw value needs to be stored in ESP-IDF's internal sensor state
     *   2. We'll copy it into the message buffer along with the MPID header
     */
    struct net_buf_simple *buf = state->sensor_states[sensor_idx].sensor_data.raw_value;
    net_buf_simple_reset(buf);  // Clear any old data
    net_buf_simple_add_le32(buf, sensor_value);  // Add 4-byte value in little-endian

    // Check if publication is configured
    if (state->pub.publish_addr == ESP_BLE_MESH_ADDR_UNASSIGNED) {
        ESP_LOGD(TAG, "Publication not configured yet (addr=0x%04x)", state->pub.publish_addr);
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * ════════════════════════════════════════════════════════════════════════
     *            BUILDING THE BLE MESH SENSOR STATUS MESSAGE
     * ════════════════════════════════════════════════════════════════════════
     *
     * MESSAGE STRUCTURE:
     * ------------------
     * The BLE Mesh stack will add:
     *   [Network Layer Headers] [Transport Layer Headers] [Access Layer]
     *
     * We only build the ACCESS LAYER payload:
     *   [Opcode: 0x52] [MPID Header] [Sensor Data]
     *
     * The opcode (SENSOR_STATUS = 0x52) is passed separately to the send function.
     * We only need to build [MPID Header + Data].
     *
     * USING THE PUBLICATION BUFFER:
     * -----------------------------
     * state->pub.msg is a buffer pre-allocated for this model's publications.
     * It's sized to hold the largest message this model might send.
     *
     * For sensors, we configured it as NET_BUF_SIMPLE(2 + 32):
     *   - 2 bytes for BLE Mesh headers
     *   - 32 bytes for our data (MPID + sensor value)
     */
    struct net_buf_simple *msg = state->pub.msg;
    net_buf_simple_reset(msg);  // Clear any old message

    /*
     * ════════════════════════════════════════════════════════════════════════
     *                  ENCODING MPID (MARSHALLED PROPERTY ID)
     * ════════════════════════════════════════════════════════════════════════
     *
     * MPID FORMAT SELECTION:
     * ----------------------
     * We have two choices (see detailed docs in provisioner code):
     *   - Format A (2 bytes): For property IDs 0x0000-0x07FF
     *   - Format B (3 bytes): For property IDs 0x0800-0xFFFF
     *
     * Our IMU sensors use custom property IDs:
     *   0x5001-0x5006 (Accel X/Y/Z, Gyro X/Y/Z)
     *
     * Since 0x5001 > 0x07FF, we MUST use Format B.
     *
     * FORMAT B ENCODING:
     * ------------------
     *   Byte 0: [LLLLLLL|1]     - Length field (7 bits) + format bit (1)
     *   Byte 1: [PPPPPPPP]      - Property ID low byte
     *   Byte 2: [PPPPPPPP]      - Property ID high byte
     *
     * EXAMPLE: Accel X (0x5001), 4-byte data
     * ----------------------------------------
     *   sensor_type = 0x5001
     *   value_len = 4
     *
     *   Step 1: Build format byte
     *     format_byte = (4 << 1) | 1
     *                 = (0b0000100 << 1) | 1
     *                 = 0b00001001
     *                 = 0x09
     *
     *   Step 2: Add property ID (little-endian)
     *     0x5001 in little-endian = [0x01, 0x50]
     *
     *   Final MPID: [0x09, 0x01, 0x50]
     *
     * COMPLETE MESSAGE:
     * -----------------
     *   MPID:  09 01 50
     *   Data:  50 00 00 00  (80 in little-endian = 0x00000050)
     *   Total: 09 01 50 50 00 00 00  (7 bytes)
     */
    uint8_t value_len = 4;  // Our sensors use 4-byte (int32_t) values

    // Build Format B header
    uint8_t format_byte = (value_len << 1) | 0x01;  // Length in bits 1-7, format=1 in bit 0

    // Write the message: [format_byte] [property_id_low] [property_id_high] [data...]
    net_buf_simple_add_u8(msg, format_byte);         // Byte 0: Format B header
    net_buf_simple_add_le16(msg, sensor_type);       // Bytes 1-2: Property ID (little-endian)
    net_buf_simple_add_mem(msg, buf->data, buf->len); // Bytes 3-6: Sensor value

    // Debug: show what we're sending
    ESP_LOGI(TAG, "📤 Sending %d bytes:", msg->len);
    ESP_LOG_BUFFER_HEX(TAG, msg->data, msg->len);

    /*
     * ════════════════════════════════════════════════════════════════════════
     *              SENDING THE MESSAGE TO THE BLE MESH NETWORK
     * ════════════════════════════════════════════════════════════════════════
     *
     * Now we hand off our formatted message to the BLE Mesh stack.
     * The stack will:
     *   1. Add Access Layer headers (opcode)
     *   2. Encrypt with the Application Key
     *   3. Add Transport Layer headers (segmentation if needed)
     *   4. Add Network Layer headers (source/dest addresses, TTL)
     *   5. Transmit over BLE advertising
     *
     * MESSAGE CONTEXT (esp_ble_mesh_msg_ctx_t):
     * ------------------------------------------
     * This tells the mesh stack HOW to send the message.
     *
     * FIELDS:
     * -------
     * - net_idx: Which Network Key to use (0 = primary network)
     *   A mesh can have multiple overlapping networks with different keys.
     *   We only have one network, so we use 0.
     *
     * - app_idx: Which Application Key to use (0 = primary app key)
     *   Even within a network, different apps can use different keys.
     *   The provisioner gave us app key 0 during configuration.
     *
     * - addr: Destination address (where to send)
     *   This was configured during provisioning (MODEL_PUB_SET).
     *   Typically 0x0001 (provisioner) or 0xC000-0xFFFF (group address).
     *
     * - send_ttl: Time To Live (how many hops allowed)
     *   Each relay decrements TTL. Message is dropped when TTL=0.
     *   7 is a good default (allows message to cross several relay nodes).
     *
     * - send_rel: Use reliable transport? (false = no acknowledgment)
     *   true  = Segmented/acknowledged (for important commands)
     *   false = Unsegmented/unacknowledged (for frequent sensor data)
     *
     * WHY send_rel = false FOR SENSORS?
     * ----------------------------------
     * Sensor data is:
     *   - Sent frequently (every 100ms in our case)
     *   - Not critical if one reading is lost
     *   - Better to send next reading than retry old one
     *
     * Using reliable transport would:
     *   - Add overhead (ACKs, retries)
     *   - Increase latency
     *   - Waste bandwidth
     *
     * FUNCTION: esp_ble_mesh_server_model_send_msg()
     * -----------------------------------------------
     * This is the CORRECT API for server models to send messages.
     *
     * PARAMETERS:
     *   - state->esp_model: Which model is sending (our Sensor Server)
     *   - &pub_ctx: Message context (where/how to send)
     *   - ESP_BLE_MESH_MODEL_OP_SENSOR_STATUS: The opcode (0x52)
     *   - msg->len: How many bytes in our payload
     *   - msg->data: The payload (MPID + sensor data)
     *
     * WHAT HAPPENS NEXT:
     * ------------------
     * The mesh stack will:
     *   1. Validate we have the app key
     *   2. Build the complete Access Layer message: [0x52] [our data]
     *   3. Encrypt using AES-CCM with the app key
     *   4. Add transport headers (no segmentation for small messages)
     *   5. Add network headers (src=our address, dst=publish address, TTL=7)
     *   6. Transmit as BLE advertisement packet(s)
     *   7. If there are relay nodes, they'll rebroadcast (decrement TTL)
     *   8. The provisioner receives and decrypts using same app key
     *   9. Provisioner's Sensor Client callback fires with our data!
     */
    esp_ble_mesh_msg_ctx_t pub_ctx = {
        .net_idx = 0,                      // Primary network key
        .app_idx = 0,                      // Primary application key
        .addr = state->pub.publish_addr,   // Where to send (configured by provisioner)
        .send_ttl = 7,                     // Allow up to 7 relay hops
        .send_rel = false,                 // Unacknowledged (best for sensors)
    };

    esp_err_t err = esp_ble_mesh_server_model_send_msg(
        state->esp_model,                       // Our Sensor Server model
        &pub_ctx,                               // Message context (where/how)
        ESP_BLE_MESH_MODEL_OP_SENSOR_STATUS,    // Opcode = 0x52 (SENSOR_STATUS)
        msg->len,                               // Payload length (7 bytes)
        msg->data);                             // Payload (MPID + value)

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to publish sensor 0x%04X, err %d", sensor_type, err);
        return err;
    }

    ESP_LOGI(TAG, "📡 Published sensor 0x%04X = %d", sensor_type, (int)sensor_value);
    return ESP_OK;
}

/*
 * ════════════════════════════════════════════════════════════════════════════
 *                    SEND VENDOR MODEL MESSAGE
 * ════════════════════════════════════════════════════════════════════════════
 *
 * Sends a custom vendor message (your own protocol).
 *
 * WHAT ARE VENDOR MODELS?
 * ------------------------
 * Vendor models let you define custom messages for application-specific needs:
 *   - Proprietary sensors not in the BLE Mesh spec
 *   - Custom control protocols
 *   - Special device features
 *
 * Each vendor model is identified by:
 *   - Company ID (assigned by Bluetooth SIG, or 0xFFFF for testing)
 *   - Model ID (your choice, e.g., 0x0001 for your temperature sensor)
 *
 * OPCODES:
 * --------
 * Vendor opcodes are 3 bytes:
 *   Byte 0: 0xC0-0xFF (vendor opcode range)
 *   Bytes 1-2: Your custom opcode
 *
 * EXAMPLE:
 * --------
 * Company ID = 0x1234 (your company)
 * Model ID = 0x0001 (temperature sensor)
 * Opcode = 0xC00001 (get temperature command)
 * Payload = [unit: 0=C, 1=F]
 *
 * @param model_index Which vendor model
 * @param opcode Your custom 3-byte opcode
 * @param data Message payload
 * @param length Payload length
 * @param dest_addr Destination address
 * @return ESP_OK on success
 */
esp_err_t mesh_model_send_vendor(uint8_t model_index, uint32_t opcode,
                                  uint8_t *data, uint16_t length, uint16_t dest_addr)
{
    vendor_model_state_t *state = find_vendor_model(model_index);
    if (!state) {
        ESP_LOGE(TAG, "Vendor model #%d not found", model_index);
        return ESP_ERR_NOT_FOUND;
    }

    if (!state->esp_model) {
        ESP_LOGE(TAG, "Vendor model ESP-IDF structure not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "📤 Sending vendor message: CID=0x%04X MID=0x%04X op=0x%06X len=%d to=0x%04X",
             state->company_id, state->model_id, (unsigned int)opcode, length, dest_addr);

    // Build message context
    esp_ble_mesh_msg_ctx_t msg_ctx = {
        .net_idx = 0,          // Primary network key
        .app_idx = 0,          // Primary application key
        .addr = dest_addr,     // Destination address
        .send_ttl = 7,         // Allow 7 relay hops
        .send_rel = false,     // Unacknowledged - vendor models don't support ACKs well at high rates
    };

    // Send vendor message
    // Note: For vendor models, ESP-IDF expects the FULL opcode (3 bytes)
    esp_err_t err = esp_ble_mesh_server_model_send_msg(
        state->esp_model,      // Vendor model
        &msg_ctx,              // Message context
        opcode,                // Your 3-byte vendor opcode
        length,                // Payload length
        data);                 // Payload

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Vendor send failed: opcode=0x%06x err=%d", (unsigned int)opcode, err);
    }

    return err;
}

/*
 * ════════════════════════════════════════════════════════════════════════
 *                     PUBLISH VENDOR MESSAGE
 * ════════════════════════════════════════════════════════════════════════
 *
 * Send a vendor message using the model's configured publication address.
 * This is different from mesh_model_send_vendor() which sends to a specific
 * unicast address.
 *
 * USAGE:
 *   - For broadcasting to multiple subscribers
 *   - For periodic status updates
 *   - When you don't know/care about specific destinations
 *
 * The publication address must be configured by the provisioner first.
 */
esp_err_t mesh_model_publish_vendor(uint8_t model_index, uint32_t opcode,
                                    uint8_t *data, uint16_t length)
{
    vendor_model_state_t *state = find_vendor_model(model_index);
    if (!state) {
        ESP_LOGE(TAG, "Vendor model #%d not found", model_index);
        return ESP_ERR_NOT_FOUND;
    }

    if (!state->esp_model) {
        ESP_LOGE(TAG, "Vendor model not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Check if publication is configured by looking at ESP-IDF's internal pub structure
    if (!state->esp_model->pub) {
        ESP_LOGW(TAG, "Vendor model pub structure is NULL");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Vendor model pub: addr=0x%04x", state->esp_model->pub->publish_addr);

    if (state->esp_model->pub->publish_addr == ESP_BLE_MESH_ADDR_UNASSIGNED) {
        ESP_LOGW(TAG, "Vendor model publish address not configured (waiting for provisioner)");
        return ESP_ERR_INVALID_STATE;
    }

    // Use ESP-IDF's configured publication address from the model
    esp_ble_mesh_msg_ctx_t pub_ctx = {
        .net_idx = 0,
        .app_idx = 0,
        .addr = state->esp_model->pub->publish_addr,  // ESP-IDF sets this when provisioner configures
        .send_ttl = 7,
        .send_rel = false,
    };

    ESP_LOGI(TAG, "📡 Publishing vendor message: opcode=0x%06" PRIx32 " len=%d to=0x%04x",
             opcode, length, pub_ctx.addr);

    esp_err_t err = esp_ble_mesh_server_model_send_msg(
        state->esp_model,
        &pub_ctx,
        opcode,
        length,
        data);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Vendor publish failed: opcode=0x%06x err=%d", (unsigned int)opcode, err);
    }

    return err;
}

uint8_t mesh_model_get_battery(uint8_t model_index)
{
    battery_model_state_t *state = find_battery_model(model_index);
    if (!state) {
        ESP_LOGW(TAG, "Battery model #%d not found", model_index);
        return 0;
    }

    // Try to read from callback if available
    if (state->callback) {
        uint8_t level;
        if (state->callback(&level, state->user_data) == ESP_OK) {
            state->battery_level = level;
            ESP_LOGI(TAG, "Battery level read: %d%%", level);
        }
    }

    return state->battery_level;
}

esp_err_t mesh_model_set_battery(uint8_t model_index, uint8_t battery_level)
{
    battery_model_state_t *state = find_battery_model(model_index);
    if (!state) {
        ESP_LOGE(TAG, "Battery model #%d not found", model_index);
        return ESP_ERR_NOT_FOUND;
    }

    if (battery_level > 100) {
        battery_level = 100;
    }

    state->battery_level = battery_level;
    ESP_LOGI(TAG, "Battery model #%d set to: %d%%", model_index, battery_level);

    return ESP_OK;
}

/*
 * ============================================================================
 *                    BATTERY PUBLISHING IMPLEMENTATION
 * ============================================================================
 *
 * EDUCATIONAL NOTES - BLE Mesh Generic Battery Model
 * ---------------------------------------------------
 *
 * The Generic Battery Server is part of the SIG-standardized models for
 * reporting battery status in BLE Mesh networks.
 *
 * MESSAGE FORMAT:
 * ---------------
 * The Battery Status message contains:
 * - Battery Level (8 bits): Battery percentage (0-100) or special values:
 *   - 0x00-0x64 (0-100): Normal battery level percentage
 *   - 0xFF: Battery level unknown
 *
 * - Time to Discharge (24 bits): Time (minutes) until battery is fully
 *   discharged. Special values:
 *   - 0x000000-0xFFFFFE: Valid time in minutes
 *   - 0xFFFFFF: Unknown or not discharging
 *
 * - Time to Charge (24 bits): Time (minutes) until battery is fully charged.
 *   Special values:
 *   - 0x000000-0xFFFFFE: Valid time in minutes
 *   - 0xFFFFFF: Unknown or not charging
 *
 * - Flags (8 bits): Battery status flags
 *   - Bit 0-1: Charge state (00=Unknown, 01=Discharging, 10=Charging, 11=Critical)
 *   - Bit 2-3: Presence (00=Unknown, 01=Not present, 10=Present, 11=Present and removable)
 *   - Bit 4-5: Service Required (00=Unknown, 01=Not required, 10=Required)
 *   - Bit 6-7: Reserved
 *
 * OPCODE:
 * -------
 * - ESP_BLE_MESH_MODEL_OP_GEN_BATTERY_STATUS = 0x8224 (2-byte opcode)
 *   This is the status message that servers send to clients
 *
 * PUBLICATION USE CASE:
 * ---------------------
 * In this implementation we simplify the battery report to just battery level
 * percentage (0-100). This is typical for IoT sensor nodes where detailed
 * time estimates and flags aren't necessary. We set:
 * - Battery Level: Read from user callback or current state
 * - Time to Discharge: 0xFFFFFF (unknown)
 * - Time to Charge: 0xFFFFFF (unknown)
 * - Flags: 0x00 (all unknown states)
 *
 * The message is published to the configured publish_addr (typically a group
 * address or the provisioner's address).
 */
esp_err_t mesh_model_publish_battery(uint8_t model_index)
{
    battery_model_state_t *state = find_battery_model(model_index);
    if (!state) {
        ESP_LOGE(TAG, "Battery model #%d not found", model_index);
        return ESP_ERR_NOT_FOUND;
    }

    if (!state->esp_model) {
        ESP_LOGE(TAG, "Battery model #%d not initialized (esp_model is NULL)", model_index);
        return ESP_ERR_INVALID_STATE;
    }

    // Read battery level (from callback or cached value)
    uint8_t battery_level = state->battery_level;
    if (state->callback) {
        if (state->callback(&battery_level, state->user_data) == ESP_OK) {
            state->battery_level = battery_level;
        }
    }

    // ────────────────────────────────────────────────────────────────
    // STEP 1: BUILD THE BATTERY STATUS MESSAGE
    // ────────────────────────────────────────────────────────────────
    // Format: [Battery Level(1)] [Time to Discharge(3)] [Time to Charge(3)] [Flags(1)]
    // Total: 8 bytes
    //
    // We use simplified values:
    // - Battery Level: 0-100 (%)
    // - Time to Discharge: 0xFFFFFF (unknown)
    // - Time to Charge: 0xFFFFFF (unknown)
    // - Flags: 0x00 (all states unknown)

    struct net_buf_simple *msg = state->esp_model->pub->msg;
    if (!msg) {
        ESP_LOGE(TAG, "Battery model #%d publication message buffer is NULL", model_index);
        return ESP_ERR_INVALID_STATE;
    }

    // Reset message buffer
    net_buf_simple_reset(msg);

    // Add battery level (1 byte)
    net_buf_simple_add_u8(msg, battery_level);

    // Add time to discharge (3 bytes, little-endian, 0xFFFFFF = unknown)
    net_buf_simple_add_u8(msg, 0xFF);
    net_buf_simple_add_u8(msg, 0xFF);
    net_buf_simple_add_u8(msg, 0xFF);

    // Add time to charge (3 bytes, little-endian, 0xFFFFFF = unknown)
    net_buf_simple_add_u8(msg, 0xFF);
    net_buf_simple_add_u8(msg, 0xFF);
    net_buf_simple_add_u8(msg, 0xFF);

    // Add flags (1 byte, 0x00 = all states unknown)
    net_buf_simple_add_u8(msg, 0x00);

    // ────────────────────────────────────────────────────────────────
    // STEP 2: SETUP MESSAGE CONTEXT
    // ────────────────────────────────────────────────────────────────
    // Configure where and how the message is sent:
    // - net_idx: Network key index (0 = primary network)
    // - app_idx: Application key index (0 = primary app key)
    // - addr: Destination address (from publication config)
    // - send_ttl: Time To Live (7 = max 7 hops)
    // - send_rel: Reliable sending (false = best effort)

    esp_ble_mesh_msg_ctx_t pub_ctx = {
        .net_idx = 0,                               // Primary network
        .app_idx = 0,                               // Primary app key
        .addr = state->esp_model->pub->publish_addr, // Target address
        .send_ttl = 7,                              // Max 7 hops
        .send_rel = false,                          // Best-effort delivery
    };

    // ────────────────────────────────────────────────────────────────
    // STEP 3: SEND THE MESSAGE
    // ────────────────────────────────────────────────────────────────
    // Use ESP-IDF's server_model_send_msg API to transmit the Battery
    // Status message to the mesh network.

    esp_err_t ret = esp_ble_mesh_server_model_send_msg(
        state->esp_model,                           // Model instance
        &pub_ctx,                                   // Message context
        ESP_BLE_MESH_MODEL_OP_GEN_BATTERY_STATUS,  // Opcode = 0x8224
        msg->len,                                   // Message length (8 bytes)
        msg->data);                                 // Message data

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Battery model #%d published: level=%d%%", model_index, battery_level);
    } else {
        ESP_LOGE(TAG, "Battery model #%d publish failed: 0x%X", model_index, ret);
    }

    return ret;
}

/*
 * ============================================================================
 *                    BACKWARD COMPATIBILITY (Legacy API)
 * ============================================================================
 */

uint8_t node_get_onoff_state(void)
{
    int state = mesh_model_get_onoff(0);
    return (state >= 0) ? (uint8_t)state : 0xFF;
}

esp_err_t node_set_onoff_state(uint8_t onoff)
{
    return mesh_model_set_onoff(0, onoff, true);
}
