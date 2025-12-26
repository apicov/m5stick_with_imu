# BLE Mesh Complete Guide

A comprehensive guide to understanding Bluetooth Mesh technology and the concepts used in this codebase.

---

## Table of Contents

1. [What is BLE Mesh?](#what-is-ble-mesh)
2. [Core Concepts](#core-concepts)
3. [Network Architecture](#network-architecture)
4. [Provisioning](#provisioning)
5. [Addressing](#addressing)
6. [Security](#security)
7. [Models](#models)
8. [Messages and Opcodes](#messages-and-opcodes)
9. [Publication and Subscription](#publication-and-subscription)
10. [Understanding the Code](#understanding-the-code)

---

## What is BLE Mesh?

**BLE Mesh** is a networking protocol built on top of Bluetooth Low Energy (BLE) that enables **many-to-many** device communication. Unlike traditional Bluetooth's one-to-one pairing, BLE Mesh creates a self-healing network where messages can hop between devices to reach their destination.

### Key Characteristics

- **Mesh Topology**: Every device can relay messages
- **Self-Healing**: If one path fails, messages find another route
- **Scalable**: Support for thousands of devices
- **Low Power**: Optimized for battery-operated devices
- **Standard Protocol**: Defined by Bluetooth SIG (not proprietary)

### Use Cases

- Smart lighting systems
- Sensor networks (temperature, motion, etc.)
- Building automation
- Asset tracking
- Industrial IoT

---

## Core Concepts

### 1. Nodes

A **node** is any device in the mesh network.

**Types of Nodes:**
- **Unprovisioned Node**: A device not yet part of the network
- **Provisioned Node**: A device that has been added to the network
- **Relay Node**: Can forward messages to extend range
- **Low Power Node (LPN)**: Battery-optimized, sleeps most of the time
- **Friend Node**: Stores messages for LPNs while they sleep

### 2. Elements

An **element** is an addressable entity within a node. A single device (node) can have multiple elements.

**Example:**
- A ceiling fan with a light
  - Element 0: Fan motor (speed control)
  - Element 1: Light bulb (on/off, brightness)

Each element has its own **unicast address**.

### 3. Models

A **model** defines the functionality and behavior of an element.

**Think of models as:**
- The "API" or "interface" for controlling a device
- Defines what messages can be sent/received
- Specifies the state and behavior

**Example Models:**
- Generic OnOff: Simple on/off control
- Generic Level: Numeric value (brightness, volume, position)
- Sensor: Report sensor data
- Light HSL: Color control (Hue, Saturation, Lightness)

### 4. States

A **state** is a value stored within a model.

**Examples:**
- OnOff State: 0 (OFF) or 1 (ON)
- Level State: -32768 to +32767
- Temperature: Current temperature reading
- Battery Level: 0-100%

---

## Network Architecture

### Layers

BLE Mesh operates in layers, each with specific responsibilities:

```
┌─────────────────────────────────────┐
│    Application Layer (Models)      │ ← Your code interacts here
├─────────────────────────────────────┤
│    Access Layer (Messages)         │ ← Encryption, formatting
├─────────────────────────────────────┤
│    Upper Transport Layer            │ ← Segmentation/reassembly
├─────────────────────────────────────┤
│    Lower Transport Layer            │ ← Encryption (AppKey)
├─────────────────────────────────────┤
│    Network Layer                    │ ← Routing, TTL, encryption (NetKey)
├─────────────────────────────────────┤
│    Bearer Layer (Advertising/GATT) │ ← Physical transmission
└─────────────────────────────────────┘
```

### Message Flow Example

When you send a message to turn on a light:

1. **Application**: `mesh_model_set_onoff(0, 1, true)`
2. **Access Layer**: Encrypt with AppKey, add opcode
3. **Transport Layer**: Segment if needed, add sequence number
4. **Network Layer**: Add source/destination address, encrypt with NetKey
5. **Bearer**: Broadcast as BLE advertising packet
6. **Relay Nodes**: Receive, decrypt, re-encrypt, forward
7. **Destination**: Receive, decrypt, execute command

---

## Provisioning

**Provisioning** is the process of adding an unprovisioned device to the mesh network.

### Provisioning Steps

1. **Beacon**: Unprovisioned device broadcasts a "Beacon" (advertising packet)
2. **Invitation**: Provisioner invites the device to join
3. **Exchange Public Keys**: ECDH key exchange for secure communication
4. **Authentication**: Verify device (via PIN, OOB, or static)
5. **Distribution**: Provisioner sends:
   - **NetKey**: Network encryption key
   - **Unicast Address**: Unique address for the device
   - **IV Index**: Initialization vector for encryption
6. **Complete**: Device is now a provisioned node

### Provisioning Bearers

**PB-ADV (Advertising Bearer)**
- Uses BLE advertising packets
- Works over longer distances
- Multiple provisioners can reach the device

**PB-GATT (GATT Bearer)**
- Uses BLE connection (GATT)
- More reliable, but requires connection
- One provisioner at a time

### In Our Code

```c
// Enable both bearers
CONFIG_BLE_MESH_PB_ADV=y
CONFIG_BLE_MESH_PB_GATT=y

// Provisioning callback
void provisioned_callback(uint16_t unicast_addr) {
    // Device is now part of the network!
    printf("Provisioned with address: 0x%04X\n", unicast_addr);
}
```

---

## Addressing

BLE Mesh uses **16-bit addresses** to identify nodes and groups.

### Address Types

#### 1. Unicast Address (0x0001 - 0x7FFF)

- **Unique** to each element
- Assigned during provisioning
- **Example**: `0x0005` = Element 0 of a specific node

**Multi-Element Example:**
```
Node with 3 elements:
- Element 0: 0x0010
- Element 1: 0x0011
- Element 2: 0x0012
```

#### 2. Group Address (0xC000 - 0xFEFF)

- **Multicast** to multiple nodes
- Nodes **subscribe** to group addresses
- **Example**: `0xC001` = "All lights in living room"

**Usage:**
```c
// Subscribe element to a group
esp_ble_mesh_model_subscribe_group_addr(0x0005, 0xC001);

// Send message to group
// All nodes subscribed to 0xC001 will receive it
send_message(0xC001, ONOFF_SET, 0x01);
```

#### 3. Virtual Address (0x8000 - 0xBFFF)

- **128-bit UUID** hashed to 16-bit address
- More secure than group addresses
- Less commonly used

#### 4. Special Addresses

- **0x0000**: Unassigned
- **0xFFFF**: All-nodes broadcast (use sparingly!)
- **0xFFFD**: All-proxies
- **0xFFFC**: All-friends
- **0xFFFB**: All-relays

---

## Security

BLE Mesh has **multiple layers of encryption** to ensure security.

### Security Keys

#### 1. Device Key (DevKey)

- **Unique** to each device
- Known only by the device and provisioner
- Used for **configuration messages** (adding AppKeys, setting publish address, etc.)
- Generated during provisioning

#### 2. Network Key (NetKey)

- **Shared** by all nodes in the same network
- Encrypts network layer (source/destination addresses)
- Allows nodes to recognize messages in their network
- A mesh can have multiple subnets with different NetKeys

#### 3. Application Key (AppKey)

- **Shared** by nodes that need to communicate
- Encrypts application data (model messages)
- Multiple AppKeys can exist (e.g., different rooms, different permissions)
- Bound to a NetKey

### Encryption Flow

```
Message: "Turn on light"
                ↓
1. Access Layer: Encrypt with AppKey
                ↓
   Encrypted payload: 0x8F3A21...
                ↓
2. Network Layer: Encrypt with NetKey
                ↓
   Encrypted network PDU: 0xA7B2C9...
                ↓
3. Broadcast over BLE
```

**Only devices with the correct keys can decrypt and process the message.**

### Replay Protection

- **Sequence Numbers**: Each message has a unique sequence number
- **IV Index**: Periodically updated to prevent replay attacks
- Nodes reject messages with old/duplicate sequence numbers

---

## Models

Models are the **heart of BLE Mesh functionality**. They define what a device can do.

### Model Categories

#### 1. Foundation Models

Built-in models for configuration and management.

- **Configuration Server**: Mandatory on all nodes, handles provisioning and setup
- **Configuration Client**: Provisioner uses this to configure nodes
- **Health Server/Client**: Monitor node health and faults

#### 2. Generic Models

Common, reusable models defined by Bluetooth SIG.

**Generic OnOff**
- States: `onoff` (0 or 1)
- Messages: `OnOff Get`, `OnOff Set`, `OnOff Status`
- Use: Lights, switches, power outlets

**Generic Level**
- States: `level` (-32768 to +32767)
- Messages: `Level Get`, `Level Set`, `Level Delta Set`, `Level Move Set`
- Use: Dimmers, volume control, position control

**Generic Battery**
- States: `battery_level` (0-100%), `time_to_discharge`, `time_to_charge`, `flags`
- Messages: `Battery Get`, `Battery Status`
- Use: Battery-powered devices

**Sensor Server/Client**
- States: Multiple sensor readings
- Messages: `Sensor Get`, `Sensor Status`, `Sensor Descriptor Get`
- Use: Environmental sensors, motion detectors

#### 3. Lighting Models

Specialized for lighting control.

- **Light Lightness**: Brightness control
- **Light CTL**: Color Temperature control
- **Light HSL**: Hue, Saturation, Lightness control
- **Light xyL**: CIE xyL color space

#### 4. Vendor Models

**Custom models** defined by companies for proprietary functionality.

- Company ID: Identifies the vendor
- Model ID: Identifies the specific model
- Opcodes: Custom message types
- **Use**: Custom protocols, proprietary devices

**Example:**
```c
// Espressif vendor model for custom data
MESH_MODEL_VENDOR(0x02E5, 0x0001, my_handler, NULL)
//                 ^^^^^ Espressif Company ID
//                       ^^^^^ Custom Model ID
```

### Model Structure

Every model has:

1. **Model ID**: Unique identifier
2. **States**: Values it maintains (e.g., onoff state, level)
3. **Messages**: Commands it can receive/send
4. **State Bindings**: How states relate to each other
5. **Publication**: How it publishes state changes
6. **Subscription**: Which addresses it listens to

---

## Messages and Opcodes

### Message Types

#### 1. Acknowledged Messages

- **Sender** waits for a response
- **Receiver** sends back a status message
- **Example**: `Generic OnOff Set` → `Generic OnOff Status`
- Ensures reliability but uses more bandwidth

#### 2. Unacknowledged Messages

- **Fire and forget**
- No response expected
- **Example**: `Generic OnOff Set Unacknowledged`
- Faster, lower power, but no confirmation

#### 3. Status Messages

- Response to Get or Set commands
- Contains current state

### Opcodes

An **opcode** is a unique identifier for a message type.

**Format:**
- **1 byte**: 0x00 - 0x7F (most common)
- **2 bytes**: 0x8000 - 0xBFFF
- **3 bytes**: 0xC00000 - 0xFFFFFF (vendor)

**Examples:**
```c
// SIG-defined opcodes (Generic OnOff model)
#define ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET         0x8201
#define ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET         0x8202
#define ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK   0x8203
#define ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_STATUS      0x8204

// Vendor opcodes (your custom protocol)
#define MY_VENDOR_OP_HEARTBEAT  0xC00001  // 3-byte vendor opcode
#define MY_VENDOR_OP_DATA       0xC00002
```

### Message Parameters

Messages can have **parameters** (payload data).

**Example: Generic OnOff Set**
```
Opcode: 0x8202
Parameters:
  - OnOff: 1 byte (0 = OFF, 1 = ON)
  - TID: 1 byte (Transaction ID)
  - Transition Time: 1 byte (optional)
  - Delay: 1 byte (optional)
```

**In Code:**
```c
void mesh_generic_server_cb(esp_ble_mesh_generic_server_cb_event_t event,
                             esp_ble_mesh_generic_server_cb_param_t *param)
{
    if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET) {
        uint8_t onoff = param->value.state_change.onoff_set.onoff;
        // Do something with the onoff value
    }
}
```

---

## Publication and Subscription

### Publication

**Publication** is how a model **sends** its state to the network.

**Configuration:**
- **Publish Address**: Where to send (unicast, group, or virtual)
- **AppKey**: Which key to encrypt with
- **TTL**: Time-to-live (how many hops)
- **Period**: How often to publish (0 = manual only)
- **Retransmit**: Number of retransmissions for reliability

**Example:**
```c
// Configure publication
esp_ble_mesh_cfg_client_set_state_t set_state = {
    .model_pub_set.element_addr = 0x0005,        // Which element
    .model_pub_set.publish_addr = 0xC001,        // Publish to group
    .model_pub_set.publish_app_idx = 0x0000,     // Use AppKey 0
    .model_pub_set.publish_ttl = 7,               // Max 7 hops
    .model_pub_set.publish_period = 0,            // Manual publish
};
```

**Manual Publishing:**
```c
// Publish current state
mesh_model_set_onoff(0, 1, true);  // Set to ON and publish
//                        ^^^^ publish = true
```

### Subscription

**Subscription** is how a model **receives** messages.

**A model can subscribe to:**
- Group addresses (e.g., 0xC001 = "Living room lights")
- Virtual addresses

**Example:**
```c
// Subscribe to a group
esp_ble_mesh_cfg_client_set_state_t set_state = {
    .model_sub_add.element_addr = 0x0005,     // Which element
    .model_sub_add.sub_addr = 0xC001,         // Subscribe to group 0xC001
    .model_sub_add.company_id = 0xFFFF,       // SIG model
    .model_sub_add.model_id = 0x1000,         // Generic OnOff model
};
```

**Result:**
- Any message sent to `0xC001` will be received by this model
- Useful for controlling multiple devices simultaneously

### Pub/Sub Example Scenario

**Scenario:** 3 lights in a room, 1 switch

```
Switch (Provisioner configures):
  - Element 0x0001
  - Generic OnOff Client model
  - Publishes to: 0xC001 (group "Room Lights")

Light 1:
  - Element 0x0010
  - Generic OnOff Server model
  - Subscribes to: 0xC001

Light 2:
  - Element 0x0020
  - Generic OnOff Server model
  - Subscribes to: 0xC001

Light 3:
  - Element 0x0030
  - Generic OnOff Server model
  - Subscribes to: 0xC001
```

**What happens when switch is pressed:**
1. Switch publishes `OnOff Set` to `0xC001`
2. All 3 lights receive the message (subscribed to `0xC001`)
3. All 3 lights turn on simultaneously

---

## Understanding the Code

Now that you understand BLE Mesh concepts, let's see how they map to our code.

### Architecture Overview

```
┌─────────────────────────────────────────────────────┐
│               Application Layer                     │
│  (m5stick_mesh_node.cpp, m5stick_mesh_sensor.cpp)  │
│                                                     │
│  - Button handlers                                  │
│  - Display updates                                  │
│  - Sensor reading                                   │
│  - Model callbacks                                  │
└─────────────────────────────────────────────────────┘
                        ▲
                        │ Uses API
                        ▼
┌─────────────────────────────────────────────────────┐
│            BLE Mesh Node Component                  │
│          (components/ble_mesh_node/)                │
│                                                     │
│  - Model registry (extensible architecture)         │
│  - Dynamic model building                           │
│  - API functions (mesh_model_*)                     │
│  - Callbacks (provisioning, config, models)         │
└─────────────────────────────────────────────────────┘
                        ▲
                        │ Uses
                        ▼
┌─────────────────────────────────────────────────────┐
│              ESP-IDF BLE Mesh Stack                 │
│                                                     │
│  - Network layer                                    │
│  - Transport layer                                  │
│  - Access layer                                     │
│  - Foundation models                                │
│  - Bluetooth controller                             │
└─────────────────────────────────────────────────────┘
```

### Key Files

#### 1. `ble_mesh_models.h` - Model Configuration

Defines model types and configuration macros:

```c
// Model types
typedef enum {
    MESH_MODEL_TYPE_ONOFF,
    MESH_MODEL_TYPE_LEVEL,
    MESH_MODEL_TYPE_SENSOR,
    MESH_MODEL_TYPE_VENDOR,
    MESH_MODEL_TYPE_BATTERY,
} mesh_model_type_t;

// Configuration macro
#define MESH_MODEL_ONOFF(cb, init, ctx) { \
    MESH_MODEL_TYPE_ONOFF, \
    true, \
    { .onoff = { (cb), (init), (ctx) } } \
}
```

#### 2. `ble_mesh_node.h` - Public API

Application-facing API for controlling models:

```c
// OnOff API
int mesh_model_get_onoff(uint8_t model_index);
esp_err_t mesh_model_set_onoff(uint8_t model_index, uint8_t onoff, bool publish);

// Level API
int16_t mesh_model_get_level(uint8_t model_index);
esp_err_t mesh_model_set_level(uint8_t model_index, int16_t level, bool publish);

// Sensor API
esp_err_t mesh_model_read_sensor(uint8_t model_index, uint16_t sensor_type, int32_t *value_out);

// Battery API
uint8_t mesh_model_get_battery(uint8_t model_index);
esp_err_t mesh_model_set_battery(uint8_t model_index, uint8_t battery_level);

// Vendor API
esp_err_t mesh_model_send_vendor(uint8_t model_index, uint32_t opcode,
                                 uint8_t *data, uint16_t length, uint16_t dest_addr);
```

#### 3. `ble_mesh_node.c` - Implementation

Core implementation with extensible architecture:

**Model Registry:**
```c
typedef struct {
    mesh_model_type_t type;
    esp_ble_mesh_model_t *esp_model;
    mesh_model_config_t user_config;
    void *runtime_state;
} model_registry_entry_t;

static model_registry_entry_t model_registry[MAX_MODELS];
```

**Dynamic Model Building:**
```c
static esp_err_t build_models(mesh_model_config_t *configs, uint8_t count)
{
    for (int i = 0; i < count; i++) {
        switch (configs[i].type) {
        case MESH_MODEL_TYPE_ONOFF:
            init_onoff_model(&configs[i], &registry[i]);
            // Build ESP-IDF model structure
            break;
        case MESH_MODEL_TYPE_LEVEL:
            init_level_model(&configs[i], &registry[i]);
            break;
        // ... other models
        }
    }
}
```

### Example: OnOff Model Flow

Let's trace what happens when you call `mesh_model_set_onoff(0, 1, true)`:

1. **Application calls API:**
   ```c
   mesh_model_set_onoff(0, 1, true);
   ```

2. **API function finds model:**
   ```c
   onoff_model_state_t *state = find_onoff_model(0);
   ```

3. **Updates local state:**
   ```c
   state->onoff = 1;
   state->server.state.onoff = 1;
   ```

4. **Calls user callback:**
   ```c
   if (state->callback) {
       state->callback(1, state->user_data);
   }
   ```

5. **If publish=true, publishes to network:**
   ```c
   // (Publication not yet implemented, but would call ESP-IDF publish function)
   esp_ble_mesh_server_model_send_msg(...);
   ```

6. **User callback executes:**
   ```c
   void onoff_changed_callback(uint8_t onoff, void *user_data) {
       current_onoff = onoff;
       M5.Display.fillScreen(onoff ? TFT_GREEN : TFT_RED);
   }
   ```

### Configuration Example

Here's how all the concepts come together in application code:

```c
// 1. Define callbacks
void onoff_callback(uint8_t onoff, void *user_data) {
    // Handle OnOff state change
}

void level_callback(int16_t level, void *user_data) {
    // Handle Level state change
}

esp_err_t read_temp(uint16_t sensor_type, int32_t *value_out, void *user_data) {
    *value_out = read_temperature_sensor();
    return ESP_OK;
}

// 2. Configure sensors (for Sensor model)
mesh_sensor_config_t sensors[] = {
    {
        .type = SENSOR_TEMPERATURE,
        .read = read_temp,
        .publish_period_ms = 10000,  // Publish every 10 seconds
        .user_data = NULL
    },
};

// 3. Configure all models
mesh_model_config_t models[] = {
    MESH_MODEL_ONOFF(onoff_callback, 0, NULL),      // Phase 1
    MESH_MODEL_LEVEL(level_callback, 0, NULL),      // Phase 2
    MESH_MODEL_SENSOR(sensors, 1),                  // Phase 3
    MESH_MODEL_VENDOR(0x02E5, 0x0001, vendor_handler, NULL),  // Phase 4
    MESH_MODEL_BATTERY(battery_callback, 60000, NULL),        // Phase 5
};

// 4. Configure node
node_config_t config = {
    .device_uuid_prefix = {0xdd, 0xdd},
    .models = models,
    .model_count = 5,
    .callbacks.provisioned = provisioned_callback,
    .callbacks.reset = reset_callback,
    .device_name = "M5Stick-Node"
};

// 5. Initialize and start
node_init(&config);
node_start();
```

### What Happens During Initialization

```
node_init(&config)
    ↓
1. Build element structure
    - Create element with models array
    ↓
2. Build models dynamically
    - For each model in config:
      • Allocate state structure
      • Initialize ESP-IDF model
      • Store in registry
    ↓
3. Register callbacks
    - Provisioning callback
    - Config server callback
    - Generic server callback
    - Sensor server callback
    ↓
4. Initialize BLE Mesh stack
    - esp_ble_mesh_init()
    ↓
node_start()
    ↓
5. Enable provisioning
    - esp_ble_mesh_node_prov_enable()
    ↓
6. Start advertising beacon
    - Device is now discoverable
```

### Message Flow Example

**Scenario:** Provisioner sends OnOff Set to turn on light

```
Provisioner                    Mesh Network                    Your Device
    │                                │                               │
    │  OnOff Set (dest=0x0005)      │                               │
    ├───────────────────────────────>│                               │
    │                                │  Encrypted packet             │
    │                                ├──────────────────────────────>│
    │                                │                               │
    │                                │              1. Network layer decrypts (NetKey)
    │                                │              2. Transport layer processes
    │                                │              3. Access layer decrypts (AppKey)
    │                                │              4. Calls model callback
    │                                │                               │
    │                                │        mesh_generic_server_cb()
    │                                │        ┌─────────────────────┐
    │                                │        │ onoff = param->...  │
    │                                │        │ state->onoff = 1    │
    │                                │        │ callback(1, ctx)    │
    │                                │        └─────────────────────┘
    │                                │                               │
    │                                │              User callback executes:
    │                                │              onoff_changed_callback(1, NULL)
    │                                │              ┌─────────────────────┐
    │                                │              │ Turn on LED         │
    │                                │              │ Update display      │
    │                                │              └─────────────────────┘
    │                                │                               │
    │                                │  OnOff Status (if ack)        │
    │                                │<──────────────────────────────┤
    │  OnOff Status                  │                               │
    │<───────────────────────────────┤                               │
    │                                │                               │
```

---

## Summary

### Key Takeaways

1. **BLE Mesh = Many-to-Many Communication**
   - Devices relay messages
   - Self-healing network
   - Scalable to thousands of nodes

2. **Provisioning = Joining the Network**
   - Adds device to mesh
   - Assigns unicast address
   - Distributes encryption keys

3. **Addressing = Who Receives Messages**
   - Unicast: Single element
   - Group: Multiple nodes
   - Broadcast: All nodes (use sparingly)

4. **Security = Multi-Layer Encryption**
   - NetKey: Network layer
   - AppKey: Application layer
   - DevKey: Configuration only

5. **Models = Device Functionality**
   - OnOff: Binary control
   - Level: Numeric values
   - Sensor: Data reporting
   - Vendor: Custom protocols
   - Battery: Power status

6. **Pub/Sub = Message Routing**
   - Publish: Where to send
   - Subscribe: What to receive
   - Configured by provisioner

7. **Our Code = Extensible Architecture**
   - Plugin-based models
   - Easy configuration
   - Type-safe API
   - Multiple examples

### Next Steps

1. **Try the examples**
   - Start with `m5stick_mesh_node.cpp` (OnOff only)
   - Progress to `m5stick_mesh_complete.cpp` (all 5 models)

2. **Use a provisioner app**
   - nRF Mesh (Android/iOS)
   - Silicon Labs Bluetooth Mesh app

3. **Experiment with configurations**
   - Change device UUID
   - Add/remove models
   - Modify callbacks

4. **Build your own models**
   - Extend the architecture
   - Create custom vendor models
   - Implement your own protocol

---

## References

- [Bluetooth SIG Mesh Specification](https://www.bluetooth.com/specifications/specs/mesh-protocol/)
- [Bluetooth Mesh Networking](https://www.bluetooth.com/bluetooth-resources/bluetooth-mesh-networking-an-introduction-for-developers/)
- [ESP-IDF BLE Mesh Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/esp-ble-mesh/)
- [nRF Connect for Mobile](https://www.nordicsemi.com/Products/Development-tools/nrf-connect-for-mobile) - Great for testing

---

**Happy Meshing!** 🎉
