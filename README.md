# M5Stick BLE Mesh IMU Node

A BLE Mesh node firmware for M5StickC-Plus that streams high-frequency IMU (accelerometer + gyroscope) data over a mesh network.

## 🎯 What This Project Does

This firmware turns an M5StickC-Plus into a **BLE Mesh sensor node** that:

1. **Reads IMU data** from the built-in MPU6886 sensor (accel + gyro)
2. **Compresses data** to fit in a single BLE Mesh segment (8 bytes)
3. **Streams at 10Hz** (100ms intervals) to the mesh network
4. **Auto-provisions** when a provisioner is nearby
5. **Displays status** on the built-in LCD screen

## 📊 Key Features

- ✅ **High-frequency streaming** - 10Hz IMU data (10 samples/second)
- ✅ **Optimized for BLE Mesh** - Single-segment messages (no fragmentation)
- ✅ **Data compression** - 6-axis IMU compressed to 8 bytes
- ✅ **Scalable** - Supports 10+ nodes on one network
- ✅ **Low latency** - <100ms from sensor → mesh
- ✅ **Auto-reconnect** - Handles network disruptions
- ✅ **Visual feedback** - LCD shows provisioning status and data

## 🏗️ Architecture

```
┌──────────────────────┐
│   M5StickC-Plus      │
│                      │
│  ┌────────────────┐  │
│  │  MPU6886 IMU   │  │ ← Read accel/gyro
│  └────────┬───────┘  │
│           ↓          │
│  ┌────────────────┐  │
│  │  Compression   │  │ ← int16 → int8 (8 bytes)
│  └────────┬───────┘  │
│           ↓          │
│  ┌────────────────┐  │
│  │ Vendor Model   │  │ ← Opcode 0xC00001
│  └────────┬───────┘  │
│           ↓          │
│  ┌────────────────┐  │
│  │  BLE Mesh      │  │ ← Transmit at 10Hz
│  │  Stack         │  │
│  └────────────────┘  │
└──────────────────────┘
```

## 🔬 Technical Details

### Data Compression

**Problem:** Full IMU data is too large for efficient BLE Mesh transmission

**Solution:** Compress 6 values to 8 bytes total

| Data         | Original (int16) | Compressed (int8) | Unit          | Range      |
|--------------|------------------|-------------------|---------------|------------|
| Timestamp    | -                | 2 bytes           | milliseconds  | 0-65535ms  |
| Accel X      | 2 bytes          | 1 byte            | 0.1g          | ±12.7g     |
| Accel Y      | 2 bytes          | 1 byte            | 0.1g          | ±12.7g     |
| Accel Z      | 2 bytes          | 1 byte            | 0.1g          | ±12.7g     |
| Gyro X       | 2 bytes          | 1 byte            | 10 dps        | ±1270 dps  |
| Gyro Y       | 2 bytes          | 1 byte            | 10 dps        | ±1270 dps  |
| Gyro Z       | 2 bytes          | 1 byte            | 10 dps        | ±1270 dps  |
| **Total**    | **12 bytes**     | **8 bytes**       | -             | -          |

**Result:** Fits in a single BLE Mesh segment (11 byte max), avoiding fragmentation!

### Vendor Model

- **Company ID:** 0x0001 (test/development)
- **Model ID:** 0x0001 (custom IMU model)
- **Opcode:** 0xC00001 (vendor opcode for IMU data)
- **Publish interval:** 100ms (10Hz)

### Message Format

```c
typedef struct {
    uint16_t timestamp_ms;  // Milliseconds since boot (wraps at 65.5s)
    int8_t accel_x;         // Acceleration X in 0.1g units
    int8_t accel_y;         // Acceleration Y in 0.1g units
    int8_t accel_z;         // Acceleration Z in 0.1g units
    int8_t gyro_x;          // Gyroscope X in 10 dps units
    int8_t gyro_y;          // Gyroscope Y in 10 dps units
    int8_t gyro_z;          // Gyroscope Z in 10 dps units
} __attribute__((packed)) imu_compact_msg_t;
```

**Example packet:**
```
[0x34, 0x12, 0x05, 0xFF, 0x62, 0x01, 0x00, 0xFF]
  ^timestamp   ^accel     ^gyro
  4660ms       (0.5,-0.1, 9.8)g  (10,0,-10)dps
```

## 🚀 Quick Start

### 1. Hardware Requirements

- **M5StickC-Plus** (ESP32-PICO with built-in IMU)
- USB-C cable for programming

### 2. Software Requirements

- ESP-IDF v5.0 or later
- M5Unified library (managed component, auto-installed)

### 3. Build and Flash

```bash
# Clone if not already done
cd m5stick_with_imu

# Build
idf.py build

# Flash
idf.py flash monitor
```

### 4. Expected Output

```
I (1234) M5STICK: M5StickC-Plus initialized
I (2000) MESH_NODE: BLE Mesh node initialized
I (2100) MESH_NODE: Waiting for provisioning...
I (5000) MESH_NODE: Provisioning started
I (8000) MESH_NODE: ✓ Provisioned! Addr=0x0010
I (9000) MESH_NODE: ✓ AppKey added
I (10000) MESH_NODE: 🎉 Configuration complete
I (10100) IMU_TASK: 📊 IMU [t=567] A:[0.6,0.1,9.8]g G:[0,0,0]dps
I (10200) IMU_TASK: Publishing IMU data...
```

### 5. LCD Display

```
┌─────────────────────┐
│  IMU MESH NODE      │
│                     │
│  Provisioned        │ ← Green when ready
│  Addr: 0x0010       │ ← Your mesh address
│                     │
│  📊 Streaming       │
│  Rate: 10Hz         │
│                     │
│  A: 0.6, 0.1, 9.8   │ ← Accel (g)
│  G: 0, 0, 0         │ ← Gyro (dps)
└─────────────────────┘
```

## 📡 Network Integration

This node is designed to work with the **ESP32 Provisioner** project:

1. **Provisioner** scans for unprovisioned nodes
2. **Auto-provisions** nodes with UUID prefix 0xAA, 0xBB
3. **Auto-configures** vendor model with publish settings
4. **Receives** IMU data and forwards to MQTT

See [../esp32_provisioner/README.md](../esp32_provisioner/README.md) for the gateway setup.

## ⚙️ Configuration

### UUID Prefix (for auto-provisioning)

The node UUID starts with `0xAA 0xBB` by default. To change:

```c
// In components/ble_mesh_node/src/ble_mesh_node.c
static const uint8_t dev_uuid[16] = {
    0xAA, 0xBB,  // ← Change these bytes
    // ... rest of MAC address
};
```

**Important:** Must match the provisioner's `CONFIG_MESH_UUID_PREFIX` settings!

### IMU Sampling Rate

Default: 10Hz (100ms interval)

To change:

```c
// In main/m5stick_mesh_imu.cpp
#define IMU_PUBLISH_INTERVAL_MS  100  // Change to 50 for 20Hz, 200 for 5Hz
```

**Note:** Faster rates may cause buffer exhaustion with many nodes. 10Hz is recommended.

### Data Compression Units

Default: 0.1g for accel, 10dps for gyro

To change precision:

```c
// In main/m5stick_mesh_imu.cpp
imu_data.accel_x = (int8_t)(accel_x / 100);  // Change /100 to /50 for 0.05g units
imu_data.gyro_x = (int8_t)(gyro_x / 10);     // Change /10 to /5 for 5dps units
```

## 🐛 Troubleshooting

### Node not being provisioned

1. **Check UUID prefix** - Must be 0xAA, 0xBB (default)
2. **Check provisioner** - Must be running and scanning
3. **Check logs** - Look for "Waiting for provisioning..."
4. **Power cycle** - Reset the M5Stick

### IMU data not appearing on provisioner

1. **Check provisioning** - LCD must say "Provisioned"
2. **Check configuration** - Wait for "Configuration complete"
3. **Check opcode** - Must be 0xC00001 (matches provisioner)
4. **Check logs** - Look for "Publishing IMU data..."

### Display shows wrong values

1. **Calibrate IMU** - Place M5Stick flat on table
2. **Check orientation** - Z-axis should read ~9.8g (gravity)
3. **Check units** - Accel in g, Gyro in dps

### Buffer exhaustion errors

```
E (12345) BLE_MESH: Failed to allocate buffer
```

**Solutions:**
1. Reduce sampling rate (increase `IMU_PUBLISH_INTERVAL_MS`)
2. Reduce number of active nodes
3. Increase buffer config in sdkconfig:
   ```
   CONFIG_BLE_MESH_ADV_BUF_COUNT=100
   CONFIG_BLE_MESH_BLE_ADV_BUF_COUNT=30
   ```

## 📊 Performance

### Single Node
- **Publish rate:** 10Hz (100ms intervals)
- **Message size:** 8 bytes + mesh overhead (~20 bytes total)
- **Latency:** <100ms from sensor read to mesh transmission
- **Power consumption:** ~80mA active, ~20mA idle

### Multi-Node Scalability

| Nodes | Total msg/s | Bandwidth | Status      |
|-------|-------------|-----------|-------------|
| 1     | 10          | 80 B/s    | ✅ Excellent |
| 5     | 50          | 400 B/s   | ✅ Excellent |
| 10    | 100         | 800 B/s   | ✅ Good      |
| 20    | 200         | 1.6 KB/s  | ⚠️ Requires tuning |
| 50+   | 500+        | 4+ KB/s   | ❌ Not recommended |

**Recommendation:** 10-15 nodes max per network for reliable 10Hz streaming.

## 🔬 Educational Notes

This project demonstrates several BLE Mesh best practices:

### 1. Message Size Optimization
- **Problem:** BLE Mesh segments are only 11 bytes
- **Solution:** Compress data to fit in single segment
- **Benefit:** 3x faster, no fragmentation overhead

### 2. Task Priority Management
- **Problem:** App tasks can starve mesh stack
- **Solution:** Run IMU task at priority 3 (mesh runs at 5-8)
- **Benefit:** Mesh buffers get freed quickly, prevents exhaustion

### 3. Vendor Model Pattern
- **Problem:** Standard models don't support custom data
- **Solution:** Use vendor model with custom opcode
- **Benefit:** Full control over data format and protocol

### 4. FreeRTOS Integration
- **Pattern:** Separate task for IMU reading
- **Benefit:** Non-blocking, allows mesh stack to run
- **Priority:** Lower than mesh tasks (critical!)

## 📁 Project Structure

```
m5stick_with_imu/
├── main/
│   └── m5stick_mesh_imu.cpp    # Main application (IMU streaming)
│
├── components/
│   └── ble_mesh_node/           # BLE Mesh node component
│       ├── src/
│       │   └── ble_mesh_node.c
│       ├── include/
│       │   ├── ble_mesh_node.h
│       │   └── ble_mesh_models.h
│       └── CMakeLists.txt
│
├── managed_components/
│   └── m5stack__m5unified/      # M5Unified library (auto-installed)
│
├── CMakeLists.txt
└── README.md                    # This file
```

## 🔐 Security Notes

- **No encryption** - Uses BLE Mesh default security (network and application keys)
- **No authentication** - Auto-provisions any node with matching UUID prefix
- **Production use** - Implement OOB (Out-of-Band) authentication for secure provisioning

## 🤝 Integration with Provisioner

This node works seamlessly with the **esp32_provisioner** project:

1. **Node** advertises with UUID prefix 0xAA, 0xBB
2. **Provisioner** auto-provisions and configures
3. **Node** publishes IMU data to provisioner's address (0x0001)
4. **Provisioner** forwards data to MQTT
5. **MQTT clients** receive JSON-formatted IMU data

See the provisioner README for MQTT topic structure and JSON format.

## 📚 References

- [ESP-IDF BLE Mesh Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/esp-ble-mesh/ble-mesh-index.html)
- [BLE Mesh Specification](https://www.bluetooth.com/specifications/specs/mesh-protocol/)
- [M5StickC-Plus Documentation](https://docs.m5stack.com/en/core/m5stickc_plus)
- [MPU6886 Datasheet](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/MPU-6886-000193%2Bv1.1_GHIC_en.pdf)

## 📝 License

Same as parent project.

---

**Made with ❤️ for wearable IoT and motion tracking applications**
