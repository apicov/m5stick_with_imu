# How to Send Custom Data Packets in BLE Mesh

## Understanding the Limitation of Generic OnOff

The **Generic OnOff** model is intentionally simple - it can only send:
- 1 byte of data
- Only values 0 or 1
- No custom payloads

This is by design! Bluetooth SIG standardized it for interoperability.

## Solution: Vendor Models

Vendor Models let you define **your own custom messages** with any data format.

---

## Example: Sending Sensor Data (Temperature + Humidity)

### Step 1: Define Your Vendor Model

```c
// In ble_mesh_node.c

// Company ID (use 0xFFFF for testing, register real ID for production)
#define VENDOR_COMPANY_ID  0xFFFF

// Model ID (you choose - must be unique within your company)
#define VENDOR_MODEL_ID_SENSOR  0x0001

// Opcodes for your custom messages
#define VENDOR_OP_SENSOR_GET       ESP_BLE_MESH_MODEL_OP_3(0x01, VENDOR_COMPANY_ID)
#define VENDOR_OP_SENSOR_STATUS    ESP_BLE_MESH_MODEL_OP_3(0x02, VENDOR_COMPANY_ID)

// Your custom data structure
typedef struct {
    int16_t temperature;  // Temperature in 0.01°C (e.g., 2550 = 25.50°C)
    uint16_t humidity;    // Humidity in 0.01% (e.g., 6542 = 65.42%)
    uint16_t pressure;    // Pressure in Pa
} sensor_data_t;

// Vendor model server
static esp_ble_mesh_model_t vendor_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(
        VENDOR_COMPANY_ID,
        VENDOR_MODEL_ID_SENSOR,
        vendor_model_ops,      // Operations (Get, Status, etc.)
        NULL,                  // Publication
        &vendor_model_data     // User data
    ),
};

// Add to element definition
static esp_ble_mesh_elem_t elements[] = {
    {
        .location = 0x0000,
        .sig_model_count = ARRAY_SIZE(root_models),
        .sig_models = root_models,
        .vnd_model_count = ARRAY_SIZE(vendor_models),  // Add this
        .vnd_models = vendor_models,                    // Add this
    },
};
```

### Step 2: Implement Message Handlers

```c
// Handle incoming GET requests
static void vendor_model_get_handler(esp_ble_mesh_model_t *model,
                                     esp_ble_mesh_msg_ctx_t *ctx,
                                     uint16_t length, uint8_t *data)
{
    ESP_LOGI(TAG, "Received Sensor Get from 0x%04x", ctx->addr);

    // Send current sensor data as response
    send_sensor_status(model, ctx);
}

// Send sensor data to provisioner/client
static void send_sensor_status(esp_ble_mesh_model_t *model,
                               esp_ble_mesh_msg_ctx_t *ctx)
{
    sensor_data_t data;

    // Read real sensor data (example values)
    data.temperature = 2550;  // 25.50°C
    data.humidity = 6542;     // 65.42%
    data.pressure = 101325;   // 101.325 kPa (sea level)

    // Prepare message
    esp_ble_mesh_msg_ctx_t send_ctx = {0};
    if (ctx) {
        send_ctx = *ctx;  // Reply to requester
    } else {
        send_ctx.net_idx = 0;      // Use primary network
        send_ctx.app_idx = 0;      // Use primary app key
        send_ctx.addr = 0x0001;    // Send to provisioner (or group address)
        send_ctx.send_ttl = 3;     // Time-to-live (max hops)
    }

    // Send the message
    esp_ble_mesh_server_model_send_msg(
        model,
        &send_ctx,
        VENDOR_OP_SENSOR_STATUS,
        sizeof(data),
        (uint8_t *)&data
    );

    ESP_LOGI(TAG, "Sent: Temp=%d.%02d°C, Humidity=%d.%02d%%",
             data.temperature / 100, data.temperature % 100,
             data.humidity / 100, data.humidity % 100);
}

// Define vendor model operations
static esp_ble_mesh_model_op_t vendor_model_ops[] = {
    {VENDOR_OP_SENSOR_GET, 0, vendor_model_get_handler},
    ESP_BLE_MESH_MODEL_OP_END,
};
```

### Step 3: Send Data Periodically (Without Being Asked)

```c
// In your main loop or a timer callback
void send_periodic_sensor_data(void)
{
    esp_ble_mesh_model_t *model = &vendor_models[0];

    // Send without a specific context (publication)
    send_sensor_status(model, NULL);
}

// Call this every 10 seconds
while(1) {
    M5.update();

    if (M5.BtnA.wasPressed()) {
        // Manual trigger: send sensor data NOW
        send_periodic_sensor_data();
    }

    vTaskDelay(pdMS_TO_TICKS(10));
}
```

---

## Example: Sending ANY Data (General Purpose)

```c
// Send arbitrary binary data
void send_custom_packet(uint8_t *data, uint16_t length)
{
    esp_ble_mesh_msg_ctx_t ctx = {0};
    ctx.net_idx = 0;
    ctx.app_idx = 0;
    ctx.addr = 0x0001;  // Provisioner address
    ctx.send_ttl = 3;

    esp_ble_mesh_server_model_send_msg(
        &vendor_models[0],  // Your vendor model
        &ctx,
        VENDOR_OP_CUSTOM_DATA,  // Your custom opcode
        length,
        data
    );
}

// Usage:
uint8_t my_data[] = {0x12, 0x34, 0x56, 0x78, 0xAB, 0xCD};
send_custom_packet(my_data, sizeof(my_data));
```

---

## Option 3: Use Generic Level Model (Simpler than Vendor)

If you just need to send **one number** (0-65535), use Generic Level:

```c
// Add Generic Level Server to your models
static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_SRV(&onoff_pub, &onoff_server),
    ESP_BLE_MESH_MODEL_GEN_LEVEL_SRV(&level_pub, &level_server),  // Add this
};

// Define level server state
static esp_ble_mesh_gen_level_srv_t level_server = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    },
    .state = {
        .level = 0,  // -32768 to +32767
    },
};

// Send a value
void send_level_value(int16_t value)
{
    level_server.state.level = value;

    // Publish or send via esp_ble_mesh_server_model_send_msg()
}

// Examples:
send_level_value(2550);   // Temperature 25.50°C
send_level_value(6542);   // Humidity 65.42%
send_level_value(-1250);  // Temperature -12.50°C
```

---

## Comparison: Which Should You Use?

| **Use Case** | **Model to Use** | **Data Size** | **Complexity** |
|-------------|------------------|---------------|----------------|
| Simple ON/OFF (LED, relay) | Generic OnOff | 1 byte | Easiest |
| Single number (temperature, battery %) | Generic Level | 2 bytes | Easy |
| Multiple sensors (temp+humidity+pressure) | Vendor Model | Unlimited | Medium |
| Complex data (JSON, structs, arrays) | Vendor Model | Unlimited | Medium |
| Interoperability with other brands | SIG Models (Sensor, etc.) | Defined | Hard |

---

## Receiving Custom Data on Provisioner Side

On the provisioner (smartphone app or gateway):

**For Vendor Models:**
- You'll need a **custom app** that understands your vendor model
- nRF Connect **can't** decode custom vendor models automatically
- You need to write code to parse the opcodes and data

**For Standard Models (Sensor, Level):**
- nRF Connect **can** display these
- Any BLE Mesh app that supports the model can read it
- Fully interoperable

---

## Quick Implementation Guide

Want me to show you how to:
1. **Add sensor model** to your current code (send temp/humidity)?
2. **Add vendor model** for completely custom data?
3. **Modify Generic OnOff** to include extra data in messages?

Let me know which approach you prefer! 🚀
