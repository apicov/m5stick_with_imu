/*
 * ═══════════════════════════════════════════════════════════════════════════
 *                      M5STICK IMU BLE MESH NODE
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * EDUCATIONAL PROJECT: High-frequency IMU data streaming over BLE Mesh
 *
 * KEY LEARNING POINTS:
 * ===================
 *
 * 1. BLE MESH NETWORK LIMITS & BUFFER MANAGEMENT
 *    - BLE Mesh segment size: 11 bytes max payload per segment
 *    - Messages >11 bytes require segmentation (slow, uses multiple buffers)
 *    - Network buffers are LIMITED (60 in CONFIG_BLE_MESH_ADV_BUF_COUNT)
 *    - HCI buffers are CRITICAL bottleneck (20 in CONFIG_BLE_MESH_BLE_ADV_BUF_COUNT)
 *    - Each message transmission takes ~30-50ms to complete
 *    - Buffers aren't freed until transmission completes
 *    - Sending messages too fast = buffer exhaustion
 *
 * 2. DATA COMPRESSION STRATEGY
 *    - Original: 6 values × int16_t = 12 bytes → requires segmentation!
 *    - Optimized: 6 values × int8_t = 6 bytes + 2 byte timestamp = 8 bytes total
 *    - Result: Fits in single segment, no fragmentation, much faster
 *    - Trade-off: Reduced precision (0.1g for accel, 10dps for gyro) but sufficient for motion tracking
 *
 * 3. FREERTOS TASK PRIORITY & MESH STACK
 *    - BLE Mesh advertising task runs at priority ~5-8
 *    - Application tasks MUST run at LOWER priority (we use 3)
 *    - Why? Mesh tasks need CPU time to process and free buffers
 *    - Running app at same/higher priority = starves mesh stack = buffer exhaustion
 *    - This is CRITICAL for multi-node scalability
 *
 * 4. VENDOR MODELS vs STANDARD MODELS
 *    - Vendor models: Custom opcodes (0xC00000-0xFFFFFF range) for proprietary data
 *    - Standard Sensor model: Also used but limited to individual sensor readings
 *    - Vendor model advantage: Can pack multiple values in custom format
 *    - Company ID 0x0001 used (test/development, not officially assigned)
 *
 * 5. SCALABILITY ACHIEVED
 *    - Single 8-byte message every 100ms (10 Hz)
 *    - 1 node: 10 msg/sec = 80 bytes/sec
 *    - 10 nodes: 100 msg/sec = 800 bytes/sec (well within BLE Mesh capacity)
 *    - 50 nodes: 500 msg/sec = 4000 bytes/sec (approaching limit but feasible)
 *    - Key: No segmentation + proper task priorities + sufficient buffers
 *
 * AUTHOR NOTES:
 * =============
 * This implementation evolved through debugging buffer exhaustion issues.
 * Critical lessons: message size matters, task priority matters, buffer config matters.
 * All three must be optimized for high-frequency sensor data in BLE Mesh.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 */

#include <stdio.h>
#include <M5Unified.h>

extern "C" {
    #include "ble_mesh_node.h"
    #include "ble_mesh_models.h"
}

// Provisioning state flag (set by callback when node joins network)
static bool is_provisioned = false;

// Forward declaration for publishing function
void publish_imu_data(void);

/*
 * ───────────────────────────────────────────────────────────────────────────
 *                          IMU DATA STORAGE
 * ───────────────────────────────────────────────────────────────────────────
 *
 * We store IMU data in int16_t for intermediate precision:
 * - Accel: stored in mg (milli-g), range ±32767mg = ±32.7g
 * - Gyro: stored in dps (degrees per second), range ±32767dps
 *
 * This gives us good precision for calculations while being memory efficient.
 * Later compressed to int8_t for transmission (see imu_compact_data_t).
 */
static int16_t accel_x = 0;  // Acceleration X in mg (milli-g)
static int16_t accel_y = 0;  // Acceleration Y in mg
static int16_t accel_z = 0;  // Acceleration Z in mg
static int16_t gyro_x = 0;   // Gyroscope X in dps (degrees per second)
static int16_t gyro_y = 0;   // Gyroscope Y in dps
static int16_t gyro_z = 0;   // Gyroscope Z in dps

/*
 * ───────────────────────────────────────────────────────────────────────────
 *                      VENDOR MODEL OPCODE
 * ───────────────────────────────────────────────────────────────────────────
 *
 * BLE Mesh vendor opcodes are 3 bytes:
 *   Byte 0: 0xC0-0xFF (vendor opcode range, we use 0xC0)
 *   Bytes 1-2: Custom opcode (0x0001)
 *
 * Result: 0xC00001 = our custom IMU data opcode
 *
 * IMPORTANT: This must match the opcode registered in the vendor model
 * and the opcode expected by the provisioner!
 */
#define VENDOR_MODEL_OP_IMU_DATA  0xC00001

/*
 * ───────────────────────────────────────────────────────────────────────────
 *                    COMPRESSED IMU DATA STRUCTURE
 * ───────────────────────────────────────────────────────────────────────────
 *
 * THE CRITICAL DESIGN DECISION: 8-BYTE MESSAGE SIZE
 *
 * Why 8 bytes?
 * ------------
 * BLE Mesh has an 11-byte payload limit per segment. Messages >11 bytes require:
 * - Segmentation into multiple packets
 * - More network buffers (scarce resource)
 * - Longer transmission time (~3x slower)
 * - Higher probability of buffer exhaustion with multiple nodes
 *
 * Our 8-byte design:
 * ------------------
 * 1. Timestamp: 2 bytes (uint16_t, wraps every 65 seconds)
 *    - Allows receiver to correlate accel+gyro measurements
 *    - Detect dropped packets
 *
 * 2. Accel X,Y,Z: 3 bytes (3× int8_t)
 *    - Stored in units of 0.1g (divide mg by 100)
 *    - Range: -12.7g to +12.7g (sufficient for motion tracking)
 *    - Example: 1.5g → 1500mg → 1500/100 = 15 → int8_t = 15
 *
 * 3. Gyro X,Y,Z: 3 bytes (3× int8_t)
 *    - Stored in units of 10 dps (divide dps by 10)
 *    - Range: -1270 dps to +1270 dps (sufficient for most applications)
 *    - Example: 250 dps → 250/10 = 25 → int8_t = 25
 *
 * Total: 2 + 3 + 3 = 8 bytes
 *
 * __attribute__((packed)): Ensures no padding between struct members
 */
typedef struct {
    uint16_t timestamp_ms;  // Timestamp in milliseconds
    int8_t accel_x;         // Acceleration X (0.1g units)
    int8_t accel_y;         // Acceleration Y (0.1g units)
    int8_t accel_z;         // Acceleration Z (0.1g units)
    int8_t gyro_x;          // Gyroscope X (10 dps units)
    int8_t gyro_y;          // Gyroscope Y (10 dps units)
    int8_t gyro_z;          // Gyroscope Z (10 dps units)
} __attribute__((packed)) imu_compact_data_t;

/*
 * ───────────────────────────────────────────────────────────────────────────
 *                      IMU DATA UPDATE FUNCTION
 * ───────────────────────────────────────────────────────────────────────────
 *
 * Reads current IMU values from M5StickC's IMU sensor (MPU6886 or similar)
 *
 * CRITICAL: M5.Imu.update() must be called before getImuData()
 * - Without update(), you get stale cached values!
 * - M5Unified caches sensor data for performance
 *
 * Unit conversions:
 * - M5 returns accel in 'g' (1g = 9.8 m/s²), we multiply by 1000 → mg
 * - M5 returns gyro in dps already, no conversion needed
 */
void update_imu_data(void)
{
    // Force IMU sensor to update (reads I2C, updates internal cache)
    M5.Imu.update();

    // Get cached data from M5Unified
    auto imu_data = M5.Imu.getImuData();

    // Convert floating point to integers with appropriate units
    accel_x = (int16_t)(imu_data.accel.x * 1000.0f);  // g → mg
    accel_y = (int16_t)(imu_data.accel.y * 1000.0f);
    accel_z = (int16_t)(imu_data.accel.z * 1000.0f);
    gyro_x = (int16_t)(imu_data.gyro.x);  // Already in dps
    gyro_y = (int16_t)(imu_data.gyro.y);
    gyro_z = (int16_t)(imu_data.gyro.z);

    // Debug output: print every 10 cycles (1 second at 10 Hz rate)
    static int debug_counter = 0;
    if (++debug_counter >= 10) {
        printf("🔍 IMU: accel[%d,%d,%d]mg gyro[%d,%d,%d]dps\n",
               accel_x, accel_y, accel_z, gyro_x, gyro_y, gyro_z);
        debug_counter = 0;
    }
}

/*
 * ───────────────────────────────────────────────────────────────────────────
 *              SENSOR MODEL CALLBACKS (NOT USED FOR VENDOR DATA)
 * ───────────────────────────────────────────────────────────────────────────
 *
 * These functions provide compatibility with the standard BLE Mesh Sensor Server model.
 * The Sensor model can publish individual sensor readings using standard opcodes.
 *
 * However, we primarily use the VENDOR model for efficient bulk IMU transmission.
 * These callbacks exist because we registered a Sensor Server model in app_main().
 *
 * Why keep both?
 * - Standard Sensor model: For compatibility with standard mesh tools
 * - Vendor model: For efficient high-frequency streaming (our primary method)
 */

esp_err_t read_accel_x(uint16_t sensor_type, int32_t *value_out, void *user_data)
{
    *value_out = (int32_t)accel_x;  // Cast int16_t → int32_t (API requirement)
    return ESP_OK;
}

esp_err_t read_accel_y(uint16_t sensor_type, int32_t *value_out, void *user_data)
{
    *value_out = (int32_t)accel_y;
    return ESP_OK;
}

esp_err_t read_accel_z(uint16_t sensor_type, int32_t *value_out, void *user_data)
{
    *value_out = (int32_t)accel_z;
    return ESP_OK;
}

esp_err_t read_gyro_x(uint16_t sensor_type, int32_t *value_out, void *user_data)
{
    *value_out = (int32_t)gyro_x;
    return ESP_OK;
}

esp_err_t read_gyro_y(uint16_t sensor_type, int32_t *value_out, void *user_data)
{
    *value_out = (int32_t)gyro_y;
    return ESP_OK;
}

esp_err_t read_gyro_z(uint16_t sensor_type, int32_t *value_out, void *user_data)
{
    *value_out = (int32_t)gyro_z;
    return ESP_OK;
}

/*
 * ═══════════════════════════════════════════════════════════════════════════
 *                         IMU PUBLISHING TASK
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * CRITICAL CONCEPT: TASK PRIORITY AND BLE MESH BUFFER MANAGEMENT
 *
 * Problem We Solved:
 * ------------------
 * Initially, publishing from main task at default priority caused buffer exhaustion:
 * - Main task ran at priority 1 (low)
 * - BLE Mesh advertising task runs at priority ~5-8 (medium-high)
 * - BUT: If we publish in tight loop from main, we queue messages faster than mesh can send
 * - Result: Network buffer pool exhausted, error -105 (ENOBUFS)
 *
 * Solution:
 * ---------
 * 1. Dedicated publishing task at priority 3 (lower than mesh tasks)
 * 2. FreeRTOS scheduler gives mesh tasks preference when they need CPU
 * 3. Mesh advertising task gets time to:
 *    - Copy messages to HCI buffers
 *    - Transmit via Bluetooth controller
 *    - Free buffers when transmission completes
 * 4. Our publishing task naturally pauses when buffers are busy
 *
 * Task Priority Hierarchy:
 * ------------------------
 * Priority 8-10: System critical (Bluetooth controller)
 * Priority 5-8:  BLE Mesh advertising task
 * Priority 3:    IMU publishing task (THIS TASK)
 * Priority 1:    Main UI task
 *
 * Why This Works:
 * ---------------
 * - Higher priority tasks preempt lower priority tasks
 * - Mesh gets CPU whenever it has work to do
 * - We only publish when mesh isn't busy
 * - Natural flow control prevents buffer overflow
 *
 * Timing:
 * -------
 * - 5 second delay at startup: Wait for provisioning config to complete
 * - 100ms publish interval: 10 Hz rate, sustainable with multiple nodes
 * - Each message takes ~30-50ms to transmit, but we don't block
 *
 * ═══════════════════════════════════════════════════════════════════════════
 */
void imu_publish_task(void *pvParameters)
{
    // Wait for initial provisioning and configuration to complete
    // The provisioner needs time to:
    // 1. Complete provisioning handshake
    // 2. Bind AppKey to our models
    // 3. Configure publication addresses
    // Without this delay, we'd try to send before being properly configured
    vTaskDelay(pdMS_TO_TICKS(5000));

    while(1) {
        // Check if node has been provisioned (joined the mesh network)
        if (!is_provisioned) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;  // Wait for provisioning
        }

        // Update IMU sensor readings
        M5.Imu.update();
        auto imu_data = M5.Imu.getImuData();

        // Store latest values in global variables
        accel_x = (int16_t)(imu_data.accel.x * 1000.0f);
        accel_y = (int16_t)(imu_data.accel.y * 1000.0f);
        accel_z = (int16_t)(imu_data.accel.z * 1000.0f);
        gyro_x = (int16_t)(imu_data.gyro.x);
        gyro_y = (int16_t)(imu_data.gyro.y);
        gyro_z = (int16_t)(imu_data.gyro.z);

        // Send compressed IMU data via BLE Mesh
        publish_imu_data();

        // 100ms interval = 10 Hz update rate
        // This is a good balance:
        // - Fast enough for motion tracking
        // - Slow enough to avoid overwhelming the mesh network
        // - Allows ~50+ nodes to coexist in same network
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/*
 * ═══════════════════════════════════════════════════════════════════════════
 *                      IMU DATA PUBLISHING FUNCTION
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Packs all 6 IMU values into an 8-byte message and sends via vendor model.
 *
 * COMPRESSION ALGORITHM:
 * ----------------------
 * Input:  int16_t values (accel in mg, gyro in dps)
 * Output: int8_t values (accel in 0.1g, gyro in 10dps)
 *
 * Accel compression:
 *   - Divide mg by 100 → 0.1g units
 *   - Example: 1500 mg = 1.5g → 1500/100 = 15
 *   - Range: ±127 * 0.1g = ±12.7g (sufficient for most motion)
 *
 * Gyro compression:
 *   - Divide dps by 10 → 10dps units
 *   - Example: 250 dps → 250/10 = 25
 *   - Range: ±127 * 10dps = ±1270 dps (sufficient for human motion)
 *
 * NETWORK TRANSMISSION:
 * ---------------------
 * mesh_model_send_vendor() does:
 * 1. Looks up vendor model by index (0 = first vendor model)
 * 2. Prepares BLE Mesh message with opcode 0xC00001
 * 3. Encrypts payload with AppKey
 * 4. Adds network headers (src, dst, TTL)
 * 5. Queues for transmission to address 0x0001 (provisioner)
 *
 * NO SEGMENTATION:
 * ----------------
 * 8 bytes < 11 byte segment limit = single packet transmission
 * - Fast: ~30ms total time
 * - Efficient: Uses 1 network buffer (not 3+)
 * - Reliable: Lower chance of packet loss
 *
 * ═══════════════════════════════════════════════════════════════════════════
 */
void publish_imu_data(void)
{
    // Get microsecond timestamp, convert to milliseconds
    // esp_timer_get_time() returns microseconds since boot
    // Wraps every ~49 days (uint64_t), but we use uint16_t (wraps every ~65 seconds)
    // This is fine because we only need relative timing for correlation
    uint16_t timestamp = (uint16_t)(esp_timer_get_time() / 1000);

    // Pack all 6 IMU values + timestamp into 8 bytes
    imu_compact_data_t imu_data = {
        .timestamp_ms = timestamp,
        .accel_x = (int8_t)(accel_x / 100),  // mg → 0.1g units
        .accel_y = (int8_t)(accel_y / 100),
        .accel_z = (int8_t)(accel_z / 100),
        .gyro_x = (int8_t)(gyro_x / 10),     // dps → 10dps units
        .gyro_y = (int8_t)(gyro_y / 10),
        .gyro_z = (int8_t)(gyro_z / 10)
    };

    // Send via vendor model to provisioner (address 0x0001)
    esp_err_t ret = mesh_model_send_vendor(
        0,                            // Vendor model index (we only have 1)
        VENDOR_MODEL_OP_IMU_DATA,    // Our custom opcode (0xC00001)
        (uint8_t*)&imu_data,         // Pointer to data struct
        sizeof(imu_data),            // 8 bytes
        0x0001                       // Destination: provisioner unicast address
    );

    // Error handling: Log failures but don't halt
    // Common errors:
    // - ESP_ERR_INVALID_STATE (259): Not provisioned yet or AppKey not bound
    // - ENOBUFS (-105): Network buffers exhausted (shouldn't happen with our design)
    if (ret != ESP_OK) {
        printf("⚠️  IMU send failed: %d\n", ret);
    }

    // Update display with compressed data being sent
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(0, 0);
    M5.Display.printf("Sending:\n\n");
    M5.Display.printf("Accel (0.1g):\n");
    M5.Display.printf(" X: %d\n", imu_data.accel_x);
    M5.Display.printf(" Y: %d\n", imu_data.accel_y);
    M5.Display.printf(" Z: %d\n\n", imu_data.accel_z);
    M5.Display.printf("Gyro (10dps):\n");
    M5.Display.printf(" X: %d\n", imu_data.gyro_x);
    M5.Display.printf(" Y: %d\n", imu_data.gyro_y);
    M5.Display.printf(" Z: %d\n", imu_data.gyro_z);
}

/*
 * ───────────────────────────────────────────────────────────────────────────
 *                     MESH PROVISIONING CALLBACKS
 * ───────────────────────────────────────────────────────────────────────────
 */

// Called when node successfully joins the mesh network
void provisioned_callback(uint16_t unicast_addr)
{
    is_provisioned = true;

    // Update UI to show successful provisioning
    M5.Display.fillScreen(TFT_BLUE);
    M5.Display.setCursor(10, 10);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.printf("Provisioned!\n");
    M5.Display.printf("Addr: 0x%04X\n", unicast_addr);
    vTaskDelay(pdMS_TO_TICKS(2000));

    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextSize(1);
}

// Called when node receives a reset command from provisioner
void reset_callback(void)
{
    is_provisioned = false;

    M5.Display.fillScreen(TFT_ORANGE);
    M5.Display.setCursor(10, 10);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.printf("RESET!\n");
    M5.Display.printf("Rebooting...\n");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

// Show waiting screen while scanning for provisioner
void show_waiting_screen(void)
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(10, 10);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_YELLOW);
    M5.Display.printf("BLE Mesh\n");
    M5.Display.printf("IMU Node\n\n");
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextSize(1);
    M5.Display.printf("Waiting for\nprovisioner...\n");
}

/*
 * ═══════════════════════════════════════════════════════════════════════════
 *                              MAIN FUNCTION
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * ARCHITECTURE OVERVIEW:
 * ----------------------
 * This node demonstrates dual-model BLE Mesh approach:
 *
 * 1. SENSOR SERVER MODEL (standard BLE Mesh)
 *    - Provides individual sensor readings
 *    - Uses standard opcodes (0x52 = SENSOR_STATUS)
 *    - Compatible with any BLE Mesh gateway
 *    - Good for: compatibility, simple queries
 *
 * 2. VENDOR SERVER MODEL (custom)
 *    - Provides bulk IMU data in efficient format
 *    - Uses vendor opcode (0xC00001)
 *    - Optimized for high-frequency streaming
 *    - Good for: real-time motion tracking, efficiency
 *
 * INITIALIZATION SEQUENCE:
 * ------------------------
 * 1. Initialize M5Unified (display, IMU, buttons)
 * 2. Configure mesh models (Sensor + Vendor)
 * 3. Initialize BLE Mesh stack
 * 4. Start provisioning (scan for provisioner)
 * 5. Create publishing task (runs after provisioning)
 * 6. Main loop handles UI updates only
 *
 * ═══════════════════════════════════════════════════════════════════════════
 */
extern "C" void app_main(void)
{
    esp_err_t ret;

    // Initialize M5StickC hardware
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);
    M5.Display.setTextSize(2);

    /*
     * ───────────────────────────────────────────────────────────────────────
     *                    SENSOR MODEL CONFIGURATION
     * ───────────────────────────────────────────────────────────────────────
     *
     * Configure 6 separate sensor instances for standard Sensor Server model.
     * Each sensor has:
     * - type: Sensor Property ID (e.g., SENSOR_ACCEL_X = 0x5001)
     * - read: Callback function to get current value
     * - publish_period_ms: How often to auto-publish (100ms = 10 Hz)
     *
     * NOTE: We don't actually use auto-publish for these.
     * They exist for compatibility but vendor model is our primary transport.
     */
    mesh_sensor_config_t sensors[] = {
        { .type = SENSOR_ACCEL_X, .read = read_accel_x, .publish_period_ms = 100, .user_data = NULL },
        { .type = SENSOR_ACCEL_Y, .read = read_accel_y, .publish_period_ms = 100, .user_data = NULL },
        { .type = SENSOR_ACCEL_Z, .read = read_accel_z, .publish_period_ms = 100, .user_data = NULL },
        { .type = SENSOR_GYRO_X, .read = read_gyro_x, .publish_period_ms = 100, .user_data = NULL },
        { .type = SENSOR_GYRO_Y, .read = read_gyro_y, .publish_period_ms = 100, .user_data = NULL },
        { .type = SENSOR_GYRO_Z, .read = read_gyro_z, .publish_period_ms = 100, .user_data = NULL },
    };

    /*
     * ───────────────────────────────────────────────────────────────────────
     *                       MODEL ARRAY CONFIGURATION
     * ───────────────────────────────────────────────────────────────────────
     *
     * Define the models this node supports:
     *
     * 1. MESH_MODEL_SENSOR(sensors, 6)
     *    - Creates Sensor Server model + Sensor Setup Server model
     *    - Registers 6 sensor instances
     *    - Publication enabled by default
     *
     * 2. MESH_MODEL_VENDOR(0x0001, 0x0001, NULL, NULL)
     *    - Company ID: 0x0001 (test/development ID)
     *    - Model ID: 0x0001 (Server model - can send data)
     *    - Handler: NULL (we don't receive vendor messages, only send)
     *    - User data: NULL
     *    - Publication: enabled by default (set in macro)
     *
     * IMPORTANT: Order matters!
     * - mesh_model_send_vendor(0, ...) refers to first vendor model
     * - If you had multiple vendor models, use index 1, 2, etc.
     */
    mesh_model_config_t models[] = {
        MESH_MODEL_SENSOR(sensors, 6),                     // Standard sensor model
        MESH_MODEL_VENDOR(0x0001, 0x0001, NULL, NULL),     // Vendor model for bulk IMU
    };

    /*
     * ───────────────────────────────────────────────────────────────────────
     *                      NODE CONFIGURATION
     * ───────────────────────────────────────────────────────────────────────
     *
     * device_uuid_prefix: [0xAA, 0xBB, ...]
     * - First 2 bytes of UUID used for filtering
     * - Provisioner can filter: "only provision devices with UUID starting 0xAABB"
     * - Useful when multiple types of devices in same area
     *
     * models: Array of model configurations
     * model_count: 2 (Sensor + Vendor)
     *
     * callbacks:
     * - provisioned: Called when provisioning succeeds
     * - reset: Called when provisioner sends node reset
     * - config_complete: NULL (we don't need notification)
     *
     * device_name: "M5Stick-IMU"
     * - Appears in logs, useful for debugging
     * - Not transmitted in mesh (only UUID identifies node)
     */
    node_config_t config = {};
    config.device_uuid_prefix[0] = 0xAA;  // Match provisioner's UUID filter
    config.device_uuid_prefix[1] = 0xBB;
    config.models = models;
    config.model_count = 2;
    config.callbacks.provisioned = provisioned_callback;
    config.callbacks.reset = reset_callback;
    config.callbacks.config_complete = NULL;
    config.device_name = "M5Stick-IMU";

    // Initialize BLE Mesh stack
    ret = node_init(&config);
    if (ret != ESP_OK) {
        M5.Display.fillScreen(TFT_RED);
        M5.Display.setCursor(10, 10);
        M5.Display.setTextColor(TFT_WHITE);
        M5.Display.printf("Init Failed!\nErr: 0x%X", ret);
        while(1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Start provisioning (begin broadcasting unprovisioned device beacons)
    ret = node_start();
    if (ret != ESP_OK) {
        M5.Display.fillScreen(TFT_RED);
        M5.Display.setCursor(10, 10);
        M5.Display.setTextColor(TFT_WHITE);
        M5.Display.printf("Start Failed!\nErr: 0x%X", ret);
        while(1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    show_waiting_screen();

    /*
     * ───────────────────────────────────────────────────────────────────────
     *              CREATE IMU PUBLISHING TASK (CRITICAL!)
     * ───────────────────────────────────────────────────────────────────────
     *
     * xTaskCreate parameters:
     * 1. Task function: imu_publish_task
     * 2. Task name: "imu_publish" (for debugging)
     * 3. Stack size: 4096 bytes (sufficient for our simple task)
     * 4. Parameters: NULL (task doesn't need parameters)
     * 5. Priority: 3 (CRITICAL: lower than mesh tasks which run at ~5-8)
     * 6. Task handle: NULL (we don't need to reference this task later)
     *
     * WHY PRIORITY 3?
     * ---------------
     * - FreeRTOS is preemptive priority-based scheduler
     * - Higher priority tasks run first
     * - BLE Mesh advertising task = priority ~5-8
     * - Our task = priority 3 (lower)
     * - Result: Mesh always gets CPU when it needs to transmit/free buffers
     * - This prevents buffer exhaustion!
     *
     * CRITICAL LESSON:
     * ----------------
     * Running IMU publishing at same/higher priority than mesh tasks
     * causes buffer exhaustion because we queue messages faster than
     * mesh can transmit them. Lower priority = natural flow control.
     */
    xTaskCreate(
        imu_publish_task,           // Task function
        "imu_publish",              // Task name (debugging)
        4096,                       // Stack size in bytes
        NULL,                       // Task parameters
        3,                          // Priority (MUST be < mesh task priority!)
        NULL                        // Task handle (not needed)
    );

    /*
     * ───────────────────────────────────────────────────────────────────────
     *                         MAIN LOOP
     * ───────────────────────────────────────────────────────────────────────
     *
     * Main loop is minimal - only handles UI updates.
     * All IMU publishing happens in the dedicated imu_publish_task.
     *
     * M5.update() checks:
     * - Button presses
     * - Power management
     * - Internal state updates
     *
     * This separation of concerns is clean architecture:
     * - Main loop: UI/input handling
     * - IMU task: Data acquisition and transmission
     * - Mesh tasks: Network operations
     */
    while(1) {
        M5.update();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
