# BLE Mesh Extensible Models - Complete Guide

## 🎯 Overview

The new extensible architecture lets you configure **ANY combination of models** like building blocks!

Instead of being limited to a hardcoded Generic OnOff model, you can now easily add:
- ✅ **OnOff** - Simple on/off control
- ✅ **Level** - Dimming, positioning (0-65535)
- ✅ **Sensor** - Temperature, humidity, motion, etc.
- ✅ **Battery** - Battery status reporting
- ✅ **Vendor** - Your own custom protocol

---

## 📋 Table of Contents

1. [Quick Start](#quick-start)
2. [Example 1: Simple LED (OnOff)](#example-1-simple-led-onoff)
3. [Example 2: Dimmable Light (OnOff + Level)](#example-2-dimmable-light-onoff--level)
4. [Example 3: Temperature Sensor](#example-3-temperature-sensor)
5. [Example 4: Multi-Sensor Node](#example-4-multi-sensor-node)
6. [Example 5: Custom Vendor Model](#example-5-custom-vendor-model)
7. [Example 6: Battery-Powered Sensor](#example-6-battery-powered-sensor)
8. [API Reference](#api-reference)

---

## Quick Start

### Step 1: Include the headers

```c
#include "ble_mesh_node.h"
#include "ble_mesh_models.h"
```

### Step 2: Define your callbacks

```c
void led_changed(uint8_t onoff, void *user_data) {
    gpio_set_level(LED_GPIO, onoff);
    printf("LED is now %s\n", onoff ? "ON" : "OFF");
}
```

### Step 3: Configure models

```c
mesh_model_config_t models[] = {
    MESH_MODEL_ONOFF(led_changed, 0, NULL),  // OnOff model
};
```

### Step 4: Initialize node

```c
node_config_t config = {
    .device_uuid_prefix = {0xdd, 0xdd},
    .models = models,
    .model_count = 1,
    .device_name = "My LED",
};

node_init(&config);
node_start();
```

That's it! Your node is now ready to be provisioned and controlled.

---

## Example 1: Simple LED (OnOff)

**Use case:** Control a single LED remotely

```c
#include "ble_mesh_node.h"
#include "ble_mesh_models.h"
#include "driver/gpio.h"

#define LED_GPIO GPIO_NUM_2

// Callback when LED state changes (from mesh or local button)
void led_callback(uint8_t onoff, void *user_data) {
    gpio_set_level(LED_GPIO, onoff);
    printf("LED: %s\n", onoff ? "ON" : "OFF");
}

void app_main(void) {
    // Configure GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);

    // Configure mesh node with OnOff model
    mesh_model_config_t models[] = {
        MESH_MODEL_ONOFF(led_callback, 0, NULL),
    };

    node_config_t config = {
        .device_uuid_prefix = {0xdd, 0xdd},
        .models = models,
        .model_count = 1,
        .device_name = "Simple LED",
    };

    ESP_ERROR_CHECK(node_init(&config));
    ESP_ERROR_CHECK(node_start());

    // Main loop
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

**What you can do:**
- Remote control via nRF Connect app
- Query current state (Get command)
- Turn ON/OFF (Set command)

---

## Example 2: Dimmable Light (OnOff + Level)

**Use case:** LED that can be turned on/off AND dimmed

```c
#include "ble_mesh_node.h"
#include "ble_mesh_models.h"
#include "driver/ledc.h"

#define LED_GPIO GPIO_NUM_2
#define LEDC_CHANNEL LEDC_CHANNEL_0

// Current state
static uint8_t led_onoff = 0;
static int16_t led_level = 0;  // -32768 to +32767

// OnOff callback
void onoff_callback(uint8_t onoff, void *user_data) {
    led_onoff = onoff;
    update_led();
}

// Level callback (dimming)
void level_callback(int16_t level, void *user_data) {
    led_level = level;
    update_led();
}

// Update physical LED based on state
void update_led(void) {
    if (!led_onoff) {
        // LED is off
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL, 0);
    } else {
        // LED is on - map level to duty cycle
        // Level: -32768 to +32767 → Duty: 0 to 8191 (13-bit)
        uint32_t duty = (led_level + 32768) * 8191 / 65535;
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL, duty);
    }
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL);
}

void app_main(void) {
    // Configure PWM (for dimming)
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .gpio_num = LED_GPIO,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    ledc_channel_config(&ledc_channel);

    // Configure mesh node with OnOff AND Level models
    mesh_model_config_t models[] = {
        MESH_MODEL_ONOFF(onoff_callback, 0, NULL),
        MESH_MODEL_LEVEL(level_callback, 0, NULL),
    };

    node_config_t config = {
        .device_uuid_prefix = {0xdd, 0xdd},
        .models = models,
        .model_count = 2,  // Two models!
        .device_name = "Dimmable Light",
    };

    ESP_ERROR_CHECK(node_init(&config));
    ESP_ERROR_CHECK(node_start());

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

**What you can do:**
- Turn light ON/OFF (OnOff model)
- Adjust brightness 0-100% (Level model)
- Both controls work together

---

## Example 3: Temperature Sensor

**Use case:** Report temperature readings to the mesh network

```c
#include "ble_mesh_node.h"
#include "ble_mesh_models.h"
#include "dht.h"  // DHT sensor library

#define DHT_GPIO GPIO_NUM_4

// Sensor read callback
esp_err_t read_temperature(uint16_t sensor_type, int32_t *value_out, void *user_data) {
    float temperature, humidity;

    if (dht_read_float_data(DHT_TYPE_DHT22, DHT_GPIO, &humidity, &temperature) == ESP_OK) {
        // Temperature in 0.01°C (e.g., 2550 = 25.50°C)
        *value_out = (int32_t)(temperature * 100);
        return ESP_OK;
    }

    return ESP_FAIL;
}

void app_main(void) {
    // Configure sensor
    mesh_sensor_config_t sensors[] = {
        {
            .type = SENSOR_TEMPERATURE,
            .read = read_temperature,
            .publish_period_ms = 10000,  // Publish every 10 seconds
            .user_data = NULL,
        },
    };

    // Configure mesh node with Sensor model
    mesh_model_config_t models[] = {
        MESH_MODEL_SENSOR(sensors, 1),
    };

    node_config_t config = {
        .device_uuid_prefix = {0xdd, 0xdd},
        .models = models,
        .model_count = 1,
        .device_name = "Temp Sensor",
    };

    ESP_ERROR_CHECK(node_init(&config));
    ESP_ERROR_CHECK(node_start());

    // Sensor will automatically publish every 10 seconds
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

**What you can do:**
- Automatic temperature reporting every 10 seconds
- Manual query (Sensor Get command)
- Standard Bluetooth SIG format (works with any mesh app)

---

## Example 4: Multi-Sensor Node

**Use case:** Report temperature AND humidity from one device

```c
#include "ble_mesh_node.h"
#include "ble_mesh_models.h"
#include "dht.h"

#define DHT_GPIO GPIO_NUM_4

// Temperature sensor callback
esp_err_t read_temperature(uint16_t sensor_type, int32_t *value_out, void *user_data) {
    float temperature, humidity;
    if (dht_read_float_data(DHT_TYPE_DHT22, DHT_GPIO, &humidity, &temperature) == ESP_OK) {
        *value_out = (int32_t)(temperature * 100);  // In 0.01°C
        return ESP_OK;
    }
    return ESP_FAIL;
}

// Humidity sensor callback
esp_err_t read_humidity(uint16_t sensor_type, int32_t *value_out, void *user_data) {
    float temperature, humidity;
    if (dht_read_float_data(DHT_TYPE_DHT22, DHT_GPIO, &humidity, &temperature) == ESP_OK) {
        *value_out = (int32_t)(humidity * 100);  // In 0.01%
        return ESP_OK;
    }
    return ESP_FAIL;
}

void app_main(void) {
    // Configure MULTIPLE sensors
    mesh_sensor_config_t sensors[] = {
        {
            .type = SENSOR_TEMPERATURE,
            .read = read_temperature,
            .publish_period_ms = 10000,
        },
        {
            .type = SENSOR_HUMIDITY,
            .read = read_humidity,
            .publish_period_ms = 10000,
        },
    };

    mesh_model_config_t models[] = {
        MESH_MODEL_SENSOR(sensors, 2),  // 2 sensors!
    };

    node_config_t config = {
        .device_uuid_prefix = {0xdd, 0xdd},
        .models = models,
        .model_count = 1,
        .device_name = "Climate Sensor",
    };

    ESP_ERROR_CHECK(node_init(&config));
    ESP_ERROR_CHECK(node_start());

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

---

## Example 5: Custom Vendor Model

**Use case:** Send custom data packets (your own protocol)

```c
#include "ble_mesh_node.h"
#include "ble_mesh_models.h"

#define MY_COMPANY_ID 0x1234  // Register your own or use 0xFFFF for testing
#define MY_MODEL_ID   0x0001

// Define custom opcodes
#define VENDOR_OP_CUSTOM_DATA ESP_BLE_MESH_MODEL_OP_3(0x01, MY_COMPANY_ID)

// Vendor message handler
void vendor_handler(uint32_t opcode, uint8_t *data, uint16_t length,
                   esp_ble_mesh_msg_ctx_t *ctx, void *user_data) {
    printf("Received vendor message:\n");
    printf("  Opcode: 0x%06X\n", opcode);
    printf("  From: 0x%04X\n", ctx->addr);
    printf("  Data length: %d\n", length);
    printf("  Data: ");
    for (int i = 0; i < length; i++) {
        printf("%02X ", data[i]);
    }
    printf("\n");

    // Process your custom data here...
}

void app_main(void) {
    // Configure vendor model
    mesh_model_config_t models[] = {
        MESH_MODEL_VENDOR(MY_COMPANY_ID, MY_MODEL_ID, vendor_handler, NULL),
    };

    node_config_t config = {
        .device_uuid_prefix = {0xdd, 0xdd},
        .models = models,
        .model_count = 1,
        .device_name = "Custom Device",
    };

    ESP_ERROR_CHECK(node_init(&config));
    ESP_ERROR_CHECK(node_start());

    // Send custom data every 5 seconds
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        // Example: Send sensor readings as custom packet
        struct {
            float temperature;
            float humidity;
            uint8_t battery;
        } custom_data = {
            .temperature = 25.5,
            .humidity = 65.2,
            .battery = 85,
        };

        mesh_model_send_vendor(
            VENDOR_OP_CUSTOM_DATA,
            (uint8_t *)&custom_data,
            sizeof(custom_data),
            0x0001  // Send to provisioner
        );
    }
}
```

**What you can do:**
- Send ANY data format (structs, arrays, JSON, etc.)
- Define your own commands/responses
- Full control over protocol
- **Note:** Needs custom provisioner app (nRF Connect won't understand it)

---

## Example 6: Battery-Powered Sensor

**Use case:** Report battery level + temperature, conserve power

```c
#include "ble_mesh_node.h"
#include "ble_mesh_models.h"
#include "driver/adc.h"

// Battery read callback
esp_err_t read_battery(uint8_t *battery_level_out, void *user_data) {
    // Read ADC for battery voltage
    int adc_value = adc1_get_raw(ADC1_CHANNEL_0);

    // Convert to percentage (adjust for your battery)
    // Example: 3.0V = 0%, 4.2V = 100%
    *battery_level_out = (adc_value - 2480) * 100 / (3472 - 2480);

    return ESP_OK;
}

// Temperature read callback
esp_err_t read_temperature(uint16_t sensor_type, int32_t *value_out, void *user_data) {
    // Read your temperature sensor
    *value_out = 2550;  // Example: 25.50°C
    return ESP_OK;
}

void app_main(void) {
    // Configure ADC for battery monitoring
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);

    // Configure sensors
    mesh_sensor_config_t sensors[] = {
        {.type = SENSOR_TEMPERATURE, .read = read_temperature, .publish_period_ms = 60000},
    };

    // Configure models
    mesh_model_config_t models[] = {
        MESH_MODEL_SENSOR(sensors, 1),
        MESH_MODEL_BATTERY(read_battery, 300000, NULL),  // Report battery every 5 minutes
    };

    node_config_t config = {
        .device_uuid_prefix = {0xdd, 0xdd},
        .models = models,
        .model_count = 2,
        .device_name = "Battery Sensor",
    };

    ESP_ERROR_CHECK(node_init(&config));
    ESP_ERROR_CHECK(node_start());

    while(1) {
        // Deep sleep to save power (optional)
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
```

---

## API Reference

### Model Configuration Macros

```c
// Configure OnOff model
MESH_MODEL_ONOFF(callback, initial_state, user_data)

// Configure Level model
MESH_MODEL_LEVEL(callback, initial_level, user_data)

// Configure Sensor model
MESH_MODEL_SENSOR(sensor_array, sensor_count)

// Configure Battery model
MESH_MODEL_BATTERY(callback, publish_period_ms, user_data)

// Configure Vendor model
MESH_MODEL_VENDOR(company_id, model_id, handler, user_data)
```

### Runtime API

```c
// Get/Set OnOff
int state = mesh_model_get_onoff(0);
mesh_model_set_onoff(0, 1, true);  // Turn ON and publish

// Get/Set Level
int16_t level = mesh_model_get_level(0);
mesh_model_set_level(0, 16384, true);  // Set to 50% and publish

// Publish sensor data manually
mesh_model_publish_sensor(0, 2550);  // 25.50°C

// Send vendor message
mesh_model_send_vendor(opcode, data, length, dest_addr);
```

---

## 🎓 Next Steps

Now that you understand the extensible architecture:

1. **Try the examples** above
2. **Combine models** to create complex devices
3. **Read the source code** in `ble_mesh_models.h` for all options
4. **Experiment** with different configurations

Happy meshing! 🚀
