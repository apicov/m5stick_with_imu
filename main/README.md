# M5Stick IMU Sensor Main Files

## Active Main File

**`m5stick_mesh_imu.cpp`** - This is the ONLY file compiled by CMakeLists.txt

### Purpose
M5Stick IMU Sensor Node - reads IMU data and publishes to mesh network via vendor model.

### Build Configuration
See `CMakeLists.txt`:
```cmake
idf_component_register(SRCS "m5stick_mesh_imu.cpp" ...)
```

### Data Flow
```
MPU6886 IMU → This Device (Sensor Server) → Mesh Network (Vendor Model)
```

### Sensor Data Published
- Accelerometer (X, Y, Z)
- Gyroscope (X, Y, Z)
- Timestamp
