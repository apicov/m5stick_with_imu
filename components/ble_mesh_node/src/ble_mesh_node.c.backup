/*
 * ============================================================================
 *                      BLE MESH NODE - COMPREHENSIVE GUIDE
 * ============================================================================
 *
 * This file implements a complete BLE Mesh node that demonstrates the "server"
 * side of BLE Mesh networking. This educational implementation includes
 * extensive documentation about BLE Mesh theory and practice.
 *
 * ============================================================================
 *                         BLUETOOTH LOW ENERGY (BLE) BASICS
 * ============================================================================
 *
 * WHAT IS BLE?
 * ------------
 * Bluetooth Low Energy (BLE) is a wireless personal area network technology
 * designed for low power consumption. Key characteristics:
 *
 * - Frequency: 2.4 GHz ISM band (same as Wi-Fi, but different channels)
 * - Range: Typically 10-100 meters (depending on power class)
 * - Data Rate: 1-2 Mbps (much slower than classic Bluetooth)
 * - Power: Optimized for battery-operated devices (can run years on coin cell)
 * - Topology: Originally star (central-peripheral), now also mesh
 *
 * BLE PROTOCOL STACK:
 * -------------------
 * ┌─────────────────────────────────────────────────────────────┐
 * │  APPLICATION LAYER                                          │
 * │  (Your code - controls LEDs, reads sensors, etc.)           │
 * ├─────────────────────────────────────────────────────────────┤
 * │  BLE MESH STACK (Optional - adds mesh networking)           │
 * │  - Provisioning, Configuration, Foundation Models           │
 * ├─────────────────────────────────────────────────────────────┤
 * │  GENERIC ACCESS PROFILE (GAP)                               │
 * │  - Device discovery, connection management                  │
 * │  - Advertising, scanning                                    │
 * ├─────────────────────────────────────────────────────────────┤
 * │  GENERIC ATTRIBUTE PROFILE (GATT)                           │
 * │  - Data organization (services, characteristics)            │
 * │  - Read/write/notify operations                             │
 * ├─────────────────────────────────────────────────────────────┤
 * │  ATTRIBUTE PROTOCOL (ATT)                                   │
 * │  - Low-level data access                                    │
 * ├─────────────────────────────────────────────────────────────┤
 * │  SECURITY MANAGER (SM)                                      │
 * │  - Pairing, bonding, encryption                             │
 * ├─────────────────────────────────────────────────────────────┤
 * │  LOGICAL LINK CONTROL & ADAPTATION PROTOCOL (L2CAP)         │
 * │  - Packet segmentation, reassembly                          │
 * │  - Multiplexing between different protocols                 │
 * ├─────────────────────────────────────────────────────────────┤
 * │  HOST CONTROLLER INTERFACE (HCI)                            │
 * │  - Standardized interface between host and controller       │
 * ├═════════════════════════════════════════════════════════════┤ ← Software/Hardware boundary
 * │  LINK LAYER (LL)                                            │
 * │  - Connection management, packet transmission               │
 * │  - Frequency hopping, error correction                      │
 * ├─────────────────────────────────────────────────────────────┤
 * │  PHYSICAL LAYER (PHY)                                       │
 * │  - Radio modulation, 40 channels (3 advertising, 37 data)   │
 * └─────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 *                    WHAT IS BLE MESH? (HIGH-LEVEL OVERVIEW)
 * ============================================================================
 *
 * BLE Mesh is a networking layer built ON TOP of standard Bluetooth Low Energy.
 * It transforms BLE from a simple point-to-point technology into a many-to-many
 * mesh network, perfect for smart home, industrial IoT, and building automation.
 *
 * WHY BLE MESH?
 * -------------
 * Traditional BLE has limitations:
 * ❌ Star topology only (one central, multiple peripherals)
 * ❌ Limited range (one hop only)
 * ❌ Central device is single point of failure
 * ❌ No device-to-device communication
 *
 * BLE Mesh solves these:
 * ✅ Mesh topology (many-to-many communication)
 * ✅ Extended range via message relay (multi-hop)
 * ✅ No single point of failure (distributed network)
 * ✅ Direct device-to-device messaging
 * ✅ Standardized by Bluetooth SIG (interoperable)
 *
 * MESH NETWORK EXAMPLE:
 * ---------------------
 *     [Phone/Provisioner]          (Controls the network)
 *            |
 *    ┌───────┴────────┬────────────┐
 *    │                │            │
 * [Light 1]       [Light 2]    [Switch]
 *    │                │            │
 *    └────────┬───────┴────────────┘
 *             │
 *         [Relay]
 *             │
 *       [Light 3] (Extended range via relay)
 *
 * Messages can travel multiple hops, finding alternate paths if needed.
 *
 * ============================================================================
 *                        BLE MESH NETWORK ARCHITECTURE
 * ============================================================================
 *
 * NETWORK ROLES:
 * --------------
 * 1. PROVISIONER (Network Administrator):
 *    - Creates and manages the mesh network
 *    - Discovers unprovisioned devices
 *    - Provisions devices (assigns addresses, distributes keys)
 *    - Configures devices (binds keys to models, sets publish/subscribe)
 *    - Usually a smartphone app or gateway
 *    - Example: nRF Connect for Mobile app
 *
 * 2. NODE (Network Device):
 *    - Joins an existing network via provisioning
 *    - Receives unicast address from provisioner
 *    - Implements Server models (responds to commands)
 *    - Can relay messages for other nodes (optional)
 *    - Examples: Smart lights, sensors, switches
 *    - THIS FILE IMPLEMENTS A NODE ←
 *
 * 3. RELAY (Message Forwarder):
 *    - Any node can act as a relay
 *    - Forwards messages to extend network range
 *    - Increases coverage but adds latency
 *    - Can be enabled/disabled per node
 *
 * 4. FRIEND (Helper for Low-Power Nodes):
 *    - Stores messages for low-power nodes (LPNs)
 *    - LPNs sleep most of the time, wake periodically to check for messages
 *    - Friend node queues messages while LPN sleeps
 *    - Reduces LPN power consumption dramatically
 *
 * 5. LOW POWER NODE (LPN):
 *    - Sleeps most of the time (99%+)
 *    - Wakes periodically to poll its Friend node
 *    - Cannot relay messages (to save power)
 *    - Perfect for battery-powered sensors
 *
 * ============================================================================
 *                    PROVISIONING: JOINING THE NETWORK
 * ============================================================================
 *
 * Provisioning is the secure process of adding a new device to the mesh network.
 * It's analogous to Wi-Fi pairing but more sophisticated.
 *
 * PROVISIONING FLOW (STEP BY STEP):
 * ----------------------------------
 *
 * STEP 1: UNPROVISIONED STATE
 * ┌──────────────────┐
 * │  Unprovisioned   │  ← Device powers on
 * │      Node        │  ← Broadcasts "Unprovisioned Device Beacons"
 * │   [No Address]   │  ← Contains: UUID, OOB info, URI (optional)
 * └──────────────────┘  ← Not part of any network yet
 *         ↓
 *    Broadcasting...
 *         ↓
 * STEP 2: DISCOVERY
 * ┌──────────────────┐       ┌──────────────────┐
 * │  Provisioner     │ Scan→ │ Unprovisioned    │
 * │  (Phone/Gateway) │ ←─────│ Node             │
 * │                  │  UUID │ UUID: dd dd 00.. │
 * └──────────────────┘       └──────────────────┘
 *         ↓
 * User selects device to provision
 *         ↓
 * STEP 3: SECURE CHANNEL ESTABLISHMENT
 * ┌──────────────────┐       ┌──────────────────┐
 * │  Provisioner     │←─────→│ Node             │
 * │                  │ ECDH  │                  │
 * │  Public Key      │←─────→│ Public Key       │
 * │                  │       │                  │
 * └──────────────────┘       └──────────────────┘
 *    Elliptic Curve Diffie-Hellman key exchange
 *    Creates shared secret without transmitting it
 *         ↓
 * STEP 4: AUTHENTICATION (Optional OOB)
 * ┌──────────────────┐       ┌──────────────────┐
 * │  Provisioner     │       │ Node             │
 * │                  │       │  Displays: 749281│ ← Output OOB
 * │  User enters:    │       │                  │
 * │  749281          │       │                  │
 * └──────────────────┘       └──────────────────┘
 *    Or: Static OOB (pre-shared key/PIN)
 *    Or: No OOB (just works - less secure)
 *         ↓
 * STEP 5: PROVISIONING DATA TRANSFER (Encrypted)
 * ┌──────────────────┐       ┌──────────────────┐
 * │  Provisioner     │─────→ │ Node             │
 * │  Sends:          │Crypto │ Receives:        │
 * │  • NetKey        │─────→ │ • NetKey         │
 * │  • Unicast Addr  │─────→ │ • Unicast Addr   │
 * │  • IV Index      │─────→ │ • IV Index       │
 * │  • Flags         │─────→ │ • Flags          │
 * └──────────────────┘       └──────────────────┘
 *         ↓
 * STEP 6: PROVISIONED! (Node is now part of network)
 * ┌──────────────────┐
 * │   Provisioned    │
 * │      Node        │
 * │  Addr: 0x0005    │  ← Now has unicast address
 * │  NetKey: ✓       │  ← Can decrypt network layer
 * │  DevKey: ✓       │  ← Unique key for configuration
 * └──────────────────┘
 *         ↓
 * STEP 7: CONFIGURATION (Using DevKey encryption)
 * ┌──────────────────┐       ┌──────────────────┐
 * │  Provisioner     │       │ Node             │
 * │  1. Add AppKey   │─────→ │ ✓ Stored         │
 * │  2. Bind AppKey  │─────→ │ ✓ Model bound    │
 * │  3. Set Publish  │─────→ │ ✓ Configured     │
 * │  4. Add Subscribe│─────→ │ ✓ Subscribed     │
 * └──────────────────┘       └──────────────────┘
 *         ↓
 * STEP 8: OPERATIONAL (Ready for application messages)
 * ┌──────────────────┐       ┌──────────────────┐
 * │  Provisioner     │       │ Node             │
 * │  "Turn ON"       │─────→ │ ✓ LED ON         │
 * │                  │←───── │ "Status: ON"     │
 * └──────────────────┘       └──────────────────┘
 *
 * KEY CONCEPTS:
 * -------------
 * - UUID: 16-byte unique identifier (used before provisioning)
 * - NetKey: Network encryption key (shared by all nodes in network)
 * - DevKey: Device-specific key (for configuration messages)
 * - AppKey: Application key (shared by nodes in same application)
 * - Unicast Address: Unique 16-bit address (0x0001-0x7FFF)
 * - IV Index: 32-bit counter for replay protection
 *
 * ============================================================================
 *                         ADDRESSING IN BLE MESH
 * ============================================================================
 *
 * BLE Mesh uses 16-bit addresses (compared to 48-bit MAC addresses in BLE).
 *
 * ADDRESS TYPES:
 * --------------
 * 1. UNICAST (0x0001 - 0x7FFF):
 *    - Unique address for each element
 *    - Assigned by provisioner during provisioning
 *    - Cannot be changed after provisioning
 *    - Example: Light at 0x0003, Switch at 0x0005
 *    - Each element in a multi-element node gets consecutive addresses
 *
 * 2. GROUP (0xC000 - 0xFEFF):
 *    - Shared by multiple nodes
 *    - Used for one-to-many communication
 *    - Example: "All Lights" = 0xC001, "Kitchen Lights" = 0xC002
 *    - Nodes subscribe to group addresses
 *    - One message reaches all subscribers
 *
 * 3. VIRTUAL (128-bit UUID hashed to 16-bit):
 *    - Like group addresses but with more unique combinations
 *    - Created from 128-bit Label UUID
 *    - Range: 0x8000 - 0xBFFF
 *    - Used when 16K group addresses aren't enough
 *
 * 4. SPECIAL ADDRESSES:
 *    - 0x0000: Unassigned (invalid)
 *    - 0xFFFF: All-Proxies (nodes with GATT Proxy enabled)
 *    - 0xFFFE: All-Friends (nodes with Friend feature enabled)
 *    - 0xFFFD: All-Relays (nodes with Relay feature enabled)
 *    - 0xFFFC: All-Nodes (broadcast to all nodes)
 *
 * ELEMENT ADDRESSING EXAMPLE:
 * ---------------------------
 * Node with 3 elements (e.g., triple switch):
 * - Element 0 (Primary): 0x0005  ← Base address from provisioner
 * - Element 1:            0x0006  ← Automatically base + 1
 * - Element 2:            0x0007  ← Automatically base + 2
 *
 * ============================================================================
 *                          MODELS: FUNCTIONALITY BLOCKS
 * ============================================================================
 *
 * Models define WHAT a node can do. They are standardized building blocks.
 *
 * MODEL HIERARCHY:
 * ----------------
 * ┌────────────────────────────────────────────────────────────┐
 * │                       ALL MODELS                           │
 * └────────────────┬───────────────────────────┬───────────────┘
 *                  │                           │
 *      ┌───────────▼──────────┐    ┌──────────▼──────────────┐
 *      │   SIG MODELS         │    │  VENDOR MODELS          │
 *      │ (Bluetooth SIG std)  │    │ (Company-specific)      │
 *      └───────────┬──────────┘    └─────────────────────────┘
 *                  │
 *      ┌───────────┴────────────┬──────────────────┐
 *      │                        │                  │
 * ┌────▼─────────┐   ┌─────────▼────────┐  ┌─────▼──────────┐
 * │ Foundation   │   │ Generic Models   │  │ Sensor Models  │
 * │ Models       │   │                  │  │ Lighting Models│
 * │              │   │                  │  │ Time/Scene...  │
 * └──────────────┘   └──────────────────┘  └────────────────┘
 *
 * MODEL TYPES:
 * ------------
 * 1. SERVER MODELS (Provide functionality):
 *    - Maintain state (e.g., on/off state)
 *    - Respond to Get/Set commands
 *    - Publish state changes
 *    - Example: Generic OnOff Server (this node uses this)
 *
 * 2. CLIENT MODELS (Control functionality):
 *    - Send Get/Set commands
 *    - Receive status responses
 *    - Example: Generic OnOff Client (provisioner uses this)
 *
 * COMMON MODELS:
 * --------------
 * Foundation Models (Mandatory for all nodes):
 * - Configuration Server: Remote configuration
 * - Health Server: Diagnostics, self-test
 *
 * Generic Models:
 * - Generic OnOff: Simple on/off control (lights, relays)
 * - Generic Level: Dimming, volume control (0-65535)
 * - Generic Power: Power management
 * - Generic Battery: Battery status reporting
 *
 * Lighting Models:
 * - Light Lightness: Brightness control
 * - Light CTL: Color temperature and lightness
 * - Light HSL: Hue, saturation, lightness
 *
 * Sensor Models:
 * - Sensor Server: Reports sensor data (temperature, humidity, etc.)
 * - Sensor Client: Requests sensor data
 *
 * Time and Scene Models:
 * - Time Server: Network time synchronization
 * - Scene Server: Store and recall lighting scenes
 *
 * THIS NODE'S MODELS:
 * -------------------
 * 1. Configuration Server (Mandatory)
 *    - Allows provisioner to configure this node
 *    - Handles AppKey addition, model binding, publication setup
 *
 * 2. Generic OnOff Server (This node's main functionality)
 *    - Maintains on/off state (0 or 1)
 *    - Responds to Get commands (return current state)
 *    - Responds to Set commands (change state)
 *    - Publishes state changes to subscribers
 *
 * ============================================================================
 *                      MESSAGE FLOW AND COMMUNICATION
 * ============================================================================
 *
 * PUBLISH-SUBSCRIBE MODEL:
 * ------------------------
 * BLE Mesh uses a pub-sub pattern (like MQTT, not like request-response).
 *
 * EXAMPLE: Light Control
 * ----------------------
 *
 * SETUP (Done by provisioner during configuration):
 * ┌─────────────┐                           ┌─────────────┐
 * │  Switch     │                           │  Light      │
 * │  (Client)   │                           │  (Server)   │
 * │             │                           │             │
 * │ Publish to: │                           │Subscribe to:│
 * │  0xC001     │──────────────────────────→│  0xC001     │
 * │ "All Lights"│    (Group Address)        │ "All Lights"│
 * └─────────────┘                           └─────────────┘
 *
 * OPERATION (When user presses switch):
 * ┌─────────────┐         Mesh Network         ┌─────────────┐
 * │  Switch     │                               │  Light 1    │
 * │             │ ─┐                         ┌→ │  LED ON ✓   │
 * │ Publishes:  │  │  OnOff Set: ON         │  └─────────────┘
 * │  Dst:0xC001 │  │  Dst: 0xC001           │  ┌─────────────┐
 * │  Data: ON   │ ─┤  (Flooded through      ├→ │  Light 2    │
 * └─────────────┘  │   entire network)      │  │  LED ON ✓   │
 *                  │                         │  └─────────────┘
 *                  │                         │  ┌─────────────┐
 *                   └────────────────────────┼→ │  Light 3    │
 *                                            │  │  LED ON ✓   │
 *                                            │  └─────────────┘
 *                                            │  ┌─────────────┐
 *                                             → │  Fan        │
 *                                               │ (Ignores -  │
 *                                               │  not sub'd) │
 *                                               └─────────────┘
 *
 * All lights subscribed to 0xC001 turn on with ONE message!
 *
 * ============================================================================
 *                         SECURITY AND ENCRYPTION
 * ============================================================================
 *
 * BLE Mesh has multi-layer security (stronger than Wi-Fi WPA2!):
 *
 * ENCRYPTION LAYERS:
 * ------------------
 *
 * 1. NETWORK LAYER (NetKey Encryption):
 *    ┌──────────────────────────────────────────────┐
 *    │  Encrypted with NetKey                       │
 *    │  • Prevents outsiders from reading messages  │
 *    │  • All nodes in network share NetKey         │
 *    │  • A network can have multiple NetKeys       │
 *    │    (for sub-networks or key refresh)         │
 *    └──────────────────────────────────────────────┘
 *
 * 2. APPLICATION LAYER (AppKey Encryption):
 *    ┌──────────────────────────────────────────────┐
 *    │  Encrypted with AppKey                       │
 *    │  • Separates applications within network     │
 *    │  • Example: Lighting app vs. HVAC app        │
 *    │  • Nodes can have multiple AppKeys           │
 *    │  • Relay nodes forward without decrypting    │
 *    └──────────────────────────────────────────────┘
 *
 * 3. DEVICE LAYER (DevKey Encryption):
 *    ┌──────────────────────────────────────────────┐
 *    │  Encrypted with DevKey (unique per device)   │
 *    │  • Used ONLY for configuration messages      │
 *    │  • Provisioner ↔ Node communication          │
 *    │  • Never shared with other nodes             │
 *    └──────────────────────────────────────────────┘
 *
 * SECURITY FEATURES:
 * ------------------
 * - AES-CCM encryption (128-bit keys)
 * - ECDH key exchange (FIPS P-256 curve)
 * - Replay protection (sequence numbers + IV index)
 * - Message obfuscation (hides source address)
 * - Privacy key (randomizes advertisement addresses)
 * - Secure provisioning (ECDH + optional OOB authentication)
 *
 * KEY REFRESH PROCEDURE:
 * ----------------------
 * Keys can be updated network-wide without disruption:
 * 1. Distribute new key alongside old key
 * 2. All nodes have both keys (can receive with both)
 * 3. Switch to transmitting with new key
 * 4. Remove old key from all nodes
 *
 * ============================================================================
 *                    NODE LIFECYCLE (IMPLEMENTED IN THIS FILE)
 * ============================================================================
 *
 * STATE MACHINE:
 * --------------
 *
 *     [POWER ON]
 *         ↓
 *    ┌────────────────┐
 *    │ INITIALIZATION │  ← node_init() called
 *    │ • Init NVS     │  ← Check for stored credentials
 *    │ • Init BT      │
 *    │ • Init Mesh    │
 *    └────┬───────────┘
 *         ↓
 *   Already provisioned? (Check NVS)
 *         ├─ YES ────────────────────→ ┌──────────────────┐
 *         │                            │ PROVISIONED      │
 *         │                            │ • Rejoin network │
 *         │                            │ • Use stored     │
 *         │                            │   NetKey & Addr  │
 *         │                            └────────┬─────────┘
 *         │                                     ↓
 *         │                            ┌──────────────────┐
 *         │                            │ CONFIGURED       │
 *         │                            │ • AppKey bound   │
 *         │                            │ • Models ready   │
 *         │                            └────────┬─────────┘
 *         │                                     ↓
 *         │                            ┌──────────────────┐
 *         └─ NO ─→ ┌──────────────┐   │  OPERATIONAL     │
 *                  │UNPROVISIONED │   │ • Rx/Tx messages │
 *                  │• Broadcast   │   │ • Respond to cmds│
 *                  │  beacons     │   │ • Publish state  │
 *                  │• Wait for    │   └──────────────────┘
 *                  │  provisioner │            ↑
 *                  └──────┬───────┘            │
 *                         ↓                    │
 *                  (Provisioner discovers)     │
 *                         ↓                    │
 *                  ┌──────────────┐            │
 *                  │ PROVISIONING │            │
 *                  │ • ECDH       │            │
 *                  │ • Receive    │            │
 *                  │   NetKey     │            │
 *                  │ • Receive    │            │
 *                  │   Address    │            │
 *                  └──────┬───────┘            │
 *                         ↓                    │
 *                  ┌──────────────┐            │
 *                  │CONFIGURATION │            │
 *                  │• Receive     │            │
 *                  │  AppKey      │            │
 *                  │• Bind models │            │
 *                  │• Setup pub/  │            │
 *                  │  subscribe   │            │
 *                  └──────┬───────┘            │
 *                         └────────────────────┘
 *
 * ============================================================================
 *                         IMPLEMENTATION NOTES
 * ============================================================================
 *
 * KEY DIFFERENCES: NODE vs PROVISIONER
 * -------------------------------------
 * ┌─────────────────────────┬──────────────────────────┐
 * │ PROVISIONER             │ NODE (THIS FILE)         │
 * ├─────────────────────────┼──────────────────────────┤
 * │ Creates network         │ Joins network            │
 * │ Assigns addresses       │ Receives address         │
 * │ Sends config commands   │ Receives config commands │
 * │ Uses Client models      │ Uses Server models       │
 * │ Sends OnOff Set         │ Receives OnOff Set       │
 * │ Usually phone/gateway   │ Usually embedded device  │
 * │ Controls network        │ Controlled by network    │
 * └─────────────────────────┴──────────────────────────┘
 */

#include "ble_mesh_node.h"
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
#include <string.h>

#define TAG "BLE_MESH_NODE"

/*
 * DEVICE UUID GENERATION
 * ======================
 * The 16-byte UUID uniquely identifies this device before provisioning.
 * Structure: [2-byte prefix][6-byte MAC][8 bytes padding/random]
 *
 * The prefix (e.g., 0xdddd) is used by the provisioner to filter which
 * devices to provision. Only nodes with matching prefix will be provisioned.
 *
 * Initialize with zeros - will be filled during node_init()
 */
static uint8_t dev_uuid[16] = {0};

/*
 * NODE STATE
 * ==========
 * Current Generic OnOff state (0 = OFF, 1 = ON)
 * This represents the state of whatever this node controls (LED, relay, etc.)
 */
static uint8_t onoff_state = 0;

/*
 * APPLICATION CALLBACKS
 * =====================
 * Callbacks registered by the application to handle mesh events
 */
static node_callbacks_t app_callbacks = {0};

/*
 * FORWARD DECLARATIONS
 * ====================
 * Callback functions defined later in this file
 */
static void mesh_prov_cb(esp_ble_mesh_prov_cb_event_t event,
                        esp_ble_mesh_prov_cb_param_t *param);
static void mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                                   esp_ble_mesh_cfg_server_cb_param_t *param);
static void mesh_generic_server_cb(esp_ble_mesh_generic_server_cb_event_t event,
                                    esp_ble_mesh_generic_server_cb_param_t *param);

/*
 * CONFIGURATION SERVER SETTINGS
 * ==============================
 * These settings control how this node behaves in the mesh network:
 *
 * - relay: Whether this node forwards messages for other nodes
 *   DISABLED = don't relay (saves power, simpler)
 *   ENABLED = act as relay (extends network range)
 *
 * - beacon: Whether to broadcast secure network beacons
 *   ENABLED = broadcast beacons (helps other nodes discover network)
 *   DISABLED = silent (saves power)
 *
 * - friend: Whether to help low-power nodes (Friendship feature)
 *   NOT_SUPPORTED = don't act as Friend (simpler, saves power)
 *   SUPPORTED = can befriend Low Power Nodes
 *
 * - gatt_proxy: Whether to provide GATT proxy service
 *   NOT_SUPPORTED = mesh-only communication
 *   SUPPORTED = allows GATT devices to access mesh
 *
 * - default_ttl: Default Time-To-Live for messages (max hops)
 *   7 = messages can hop through up to 7 relay nodes
 *
 * - net_transmit: How to transmit network layer messages
 *   ESP_BLE_MESH_TRANSMIT(count, interval_steps)
 *   count = 2: Send each message 3 times (original + 2 retransmits)
 *   interval = 20ms: Wait 20ms between transmissions
 */
static esp_ble_mesh_cfg_srv_t config_server = {
    .relay = ESP_BLE_MESH_RELAY_DISABLED,           // Don't relay messages
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,          // Broadcast beacons
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,  // Not a Friend node
#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER)
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,  // GATT proxy enabled (needed for phone provisioning)
#else
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif
    .default_ttl = 7,                                // Max 7 hops
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),   // 3 transmissions, 20ms apart
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
};

/*
 * MODEL PUBLICATION CONTEXT
 * =========================
 * Publication allows the server to send status updates to the network.
 * The provisioner configures where to publish (destination address).
 */
ESP_BLE_MESH_MODEL_PUB_DEFINE(onoff_pub, 2 + 3, ROLE_NODE);

/*
 * GENERIC ONOFF SERVER STATE
 * ===========================
 * This structure holds the state for the Generic OnOff Server model.
 * The model spec requires tracking both current and target state for
 * transitions (gradual changes from one state to another).
 *
 * For a simple on/off device, current and target are usually the same.
 */
static esp_ble_mesh_gen_onoff_srv_t onoff_server = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,  // Automatically respond to Get
        .set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,  // Automatically respond to Set
    },
    .state = {
        .onoff = 0,          // Current state (0 = OFF)
        .target_onoff = 0,   // Target state (for transitions)
    },
};

/*
 * MODEL DEFINITIONS
 * =================
 * Models define what functionality this node provides.
 * Each model is either a Server (responds to commands) or Client (sends commands).
 *
 * This node has:
 * 1. Configuration Server - Mandatory for all nodes, handles configuration
 * 2. Generic OnOff Server - Provides on/off functionality
 *
 * MODEL MACROS EXPLAINED:
 * - ESP_BLE_MESH_MODEL_CFG_SRV: Configuration Server model
 *   Parameter: pointer to config_server struct
 *
 * - ESP_BLE_MESH_MODEL_GEN_ONOFF_SRV: Generic OnOff Server model
 *   Parameter 1: publication context (onoff_pub)
 *   Parameter 2: pointer to onoff_server struct
 */
static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_SRV(&onoff_pub, &onoff_server),
};

/*
 * ELEMENT DEFINITION
 * ==================
 * An element is an addressable entity within a node.
 * Simple devices have 1 element, complex devices can have multiple
 * (e.g., a dual-switch has 2 elements, one per switch).
 *
 * Each element has:
 * - Location: Physical location descriptor (0x0000 = unknown/not used)
 * - Models: Array of models this element supports
 * - Model count: Number of models in the array
 *
 * ELEMENT vs NODE:
 * - A node is the physical device (e.g., ESP32 board)
 * - An element is an addressable part of that device
 * - Each element gets its own unicast address
 * - For a node with 1 element: node address = element address
 */
static esp_ble_mesh_elem_t elements[] = {
    {
        .location = 0x0000,                             // No specific location
        .sig_model_count = ARRAY_SIZE(root_models),     // Number of SIG models
        .sig_models = root_models,                      // SIG models array
        .vnd_model_count = 0,                           // No vendor models
        .vnd_models = NULL,                             // No vendor models
    },
};

/*
 * COMPOSITION DATA
 * ================
 * Composition data describes the node's capabilities. The provisioner
 * requests this after provisioning to understand what the node can do.
 *
 * Contains:
 * - cid: Company ID (0xFFFF = not assigned, for prototypes/testing)
 * - pid: Product ID (identifies product type, 0 = not specified)
 * - vid: Version ID (firmware version, 0 = not specified)
 * - elements: Array of elements this node has
 * - element_count: Number of elements (usually 1 for simple devices)
 *
 * WHY THIS MATTERS:
 * The provisioner uses composition data to know:
 * - How many addresses to allocate (one per element)
 * - Which models to configure (Generic OnOff Server in this case)
 * - What configuration is needed (AppKey binding, etc.)
 */
static esp_ble_mesh_comp_t composition = {
    .cid = 0xFFFF,                          // Company ID: unassigned (test/prototype)
    .pid = 0x0000,                          // Product ID: not specified
    .vid = 0x0000,                          // Version ID: not specified
    .elements = elements,                    // Elements array
    .element_count = ARRAY_SIZE(elements),   // Number of elements (1)
};

/*
 * PROVISIONING DATA
 * =================
 * Configuration for the provisioning process.
 *
 * For a node (not provisioner), we use the same structure as the provisioner
 * but with node-specific values. This struct supports both node and provisioner roles.
 *
 * The struct is not initialized statically because we need to set dev_uuid first.
 * We'll initialize it dynamically in node_init() using the same pattern as the provisioner.
 */
static esp_ble_mesh_prov_t provision;

/*
 * GENERATE DEVICE UUID
 * ====================
 *
 * Creates a unique 16-byte UUID for this device.
 *
 * UUID STRUCTURE:
 * Bytes 0-1:   User-defined prefix (e.g., 0xdddd)
 *              Used by provisioner to filter devices
 * Bytes 2-7:   ESP32 Bluetooth MAC address (unique per device)
 * Bytes 8-15:  Padding (zeros)
 *
 * WHY THIS STRUCTURE:
 * - Prefix allows selective provisioning (only provision devices with matching prefix)
 * - MAC ensures uniqueness (no two ESP32s have same MAC)
 * - Simple and deterministic (same device always has same UUID)
 *
 * PRODUCTION NOTE:
 * For production, consider adding:
 * - Product ID in bytes 8-9
 * - Firmware version in bytes 10-11
 * - Random or sequential serial number in bytes 12-15
 */
static void generate_dev_uuid(const uint8_t prefix[2])
{
    const uint8_t *mac = esp_bt_dev_get_address();

    // Clear the entire UUID first
    memset(dev_uuid, 0, 16);

    // Bytes 0-1: User-defined prefix for filtering
    dev_uuid[0] = prefix[0];
    dev_uuid[1] = prefix[1];

    // Bytes 2-7: Bluetooth MAC address (6 bytes)
    memcpy(dev_uuid + 2, mac, 6);

    // Bytes 8-15: Already zero from memset (padding)

    ESP_LOGI(TAG, "Generated UUID with prefix [0x%02x 0x%02x]", prefix[0], prefix[1]);
}

/*
 * INITIALIZE BLUETOOTH STACK
 * ===========================
 *
 * Sets up the ESP32 Bluetooth subsystem for BLE Mesh.
 *
 * BLUETOOTH STACK ARCHITECTURE:
 * ┌─────────────────────────────┐
 * │  BLE Mesh Stack             │  ← We're initializing this
 * ├─────────────────────────────┤
 * │  Bluedroid Host             │  ← Bluetooth host stack
 * ├─────────────────────────────┤
 * │  Controller (HCI)           │  ← Low-level BLE radio
 * └─────────────────────────────┘
 *
 * STEPS:
 * 1. Release classic Bluetooth memory (we only need BLE)
 * 2. Initialize Bluedroid host stack
 * 3. Enable Bluedroid stack
 *
 * WHY RELEASE CLASSIC BT MEMORY:
 * ESP32 supports both Classic Bluetooth and BLE. Since we only need BLE,
 * we release Classic BT memory to save RAM (~60KB saved).
 */
static esp_err_t bluetooth_init(void)
{
    esp_err_t ret;

    // Release Classic Bluetooth controller memory (we only use BLE)
    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller release classic bt memory failed");
        return ret;
    }

    // Initialize Bluetooth controller with default configuration
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller initialize failed");
        return ret;
    }

    // Enable Bluetooth controller in BLE mode only
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller enable failed");
        return ret;
    }

    // Initialize Bluedroid host stack with configuration
    // Using explicit config (same as official example) instead of esp_bluedroid_init()
    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ret = esp_bluedroid_init_with_cfg(&bluedroid_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth bluedroid init failed");
        return ret;
    }

    // Enable Bluedroid stack
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth bluedroid enable failed");
        return ret;
    }

    ESP_LOGI(TAG, "Bluetooth initialized");
    return ESP_OK;
}

/*
 * INITIALIZE BLE MESH NODE
 * =========================
 *
 * Main initialization function - sets up everything needed for mesh operation.
 *
 * INITIALIZATION SEQUENCE:
 * 1. Initialize NVS (Non-Volatile Storage)
 *    - BLE Mesh stores provisioning data in NVS
 *    - Allows node to remember it's provisioned after reboot
 *
 * 2. Generate Device UUID
 *    - Creates unique identifier with user's prefix
 *    - Used during provisioning discovery
 *
 * 3. Initialize Bluetooth Stack
 *    - Sets up controller and host layers
 *
 * 4. Initialize BLE Mesh
 *    - Registers provisioning callback
 *    - Sets composition data
 *    - Initializes mesh stack
 *
 * 5. Register Model Callbacks
 *    - Config Server callback (handles configuration commands)
 *    - Generic Server callback (handles OnOff Get/Set)
 *
 * 6. Store Application Callbacks
 *    - Saves user's callbacks for LED control, etc.
 */
esp_err_t node_init(const node_config_t *config)
{
    esp_err_t ret;

    if (!config) {
        ESP_LOGE(TAG, "Config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Step 1: Initialize NVS for persistent storage
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated or has wrong version
        // Erase and reinitialize
        ESP_LOGW(TAG, "NVS partition needs erase, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS flash init failed");
        return ret;
    }

    // Step 2: Initialize Bluetooth stack
    ret = bluetooth_init();
    if (ret != ESP_OK) {
        return ret;
    }

    // Step 3: Generate device UUID with user's prefix
    // Must be AFTER bluetooth_init() because we need the BT MAC address
    generate_dev_uuid(config->device_uuid_prefix);

    // Debug: Log the generated UUID
    ESP_LOGI(TAG, "Device UUID: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
             dev_uuid[0], dev_uuid[1], dev_uuid[2], dev_uuid[3],
             dev_uuid[4], dev_uuid[5], dev_uuid[6], dev_uuid[7],
             dev_uuid[8], dev_uuid[9], dev_uuid[10], dev_uuid[11],
             dev_uuid[12], dev_uuid[13], dev_uuid[14], dev_uuid[15]);

    // Step 3.5: Register callbacks BEFORE esp_ble_mesh_init
    // Note: Official example doesn't check return values - callbacks queue internally
    esp_ble_mesh_register_prov_callback(mesh_prov_cb);
    esp_ble_mesh_register_config_server_callback(mesh_config_server_cb);
    esp_ble_mesh_register_generic_server_callback(mesh_generic_server_cb);

    // Step 4: Initialize provision structure
    // The struct fields depend on whether PROVISIONER role is also enabled
    esp_ble_mesh_prov_t temp_prov = {
        .uuid = dev_uuid,                 // Our UUID (for being discovered/provisioned)
#if CONFIG_BLE_MESH_PROVISIONER
        // Dual role (node + provisioner): use provisioner-style fields
        .prov_uuid = dev_uuid,            // Same UUID (for dual role support)
        .prov_unicast_addr = 0,           // Will be assigned during provisioning
        .prov_start_address = 0,          // Not used by pure node
        .prov_attention = 0x00,           // Attention timer
        .prov_algorithm = 0x00,           // FIPS P-256 Elliptic Curve
        .prov_pub_key_oob = 0x00,         // No OOB public key
        .prov_static_oob_val = NULL,      // No static OOB
        .prov_static_oob_len = 0x00,      // No static OOB length
        .flags = 0x00,                    // No special flags
        .iv_index = 0x00,                 // Will be set by provisioner
#else
        // Node-only mode: use node-specific OOB fields
        .output_size = 0,                 // No output OOB capability
        .output_actions = 0,              // No output actions
#endif
    };
    memcpy(&provision, &temp_prov, sizeof(provision));

    // Step 5: Initialize BLE Mesh (after callbacks are registered)
    ret = esp_ble_mesh_init(&provision, &composition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE Mesh init failed (err %d)", ret);
        return ret;
    }

    // Step 6: Set unprovisioned device name (for easier identification in nRF Connect)
    // Must be called AFTER esp_ble_mesh_init()
    ret = esp_ble_mesh_set_unprovisioned_device_name("M5Stick-Node");
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set device name (err %d)", ret);
        // Non-fatal, continue anyway
    }

    // Step 7: Store application callbacks
    memcpy(&app_callbacks, &config->callbacks, sizeof(node_callbacks_t));

    ESP_LOGI(TAG, "BLE Mesh Node initialized successfully");
    return ESP_OK;
}

/*
 * START BLE MESH NODE
 * ===================
 *
 * Begins mesh operation. What happens depends on provisioning state:
 *
 * IF NOT PROVISIONED (first boot):
 * - Starts broadcasting Unprovisioned Device Beacons
 * - Beacons contain the device UUID
 * - Provisioner can discover and provision the node
 *
 * IF ALREADY PROVISIONED (stored in NVS):
 * - Rejoins the mesh network with stored credentials
 * - Uses stored NetKey and unicast address
 * - Ready to receive commands immediately
 *
 * This is why NVS is important - it provides persistent storage
 * so the node doesn't need to be reprovisioned after every reboot.
 */
esp_err_t node_start(void)
{
    esp_err_t ret;

    // Small delay to allow mesh stack to fully initialize
    // This gives time for internal queues and tasks to be created
    vTaskDelay(pdMS_TO_TICKS(100));

    // Enable BLE Mesh node functionality
    // If node is already provisioned (stored in NVS), it will rejoin the network
    // If not provisioned, it will start broadcasting beacons
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
 * GET ONOFF STATE
 * ===============
 *
 * Returns the current Generic OnOff state.
 * Useful for:
 * - Displaying state on LCD/OLED
 * - Syncing hardware with mesh state
 * - Implementing toggle functionality
 */
uint8_t node_get_onoff_state(void)
{
    return onoff_state;
}

/*
 * SET ONOFF STATE LOCALLY
 * ========================
 *
 * Changes the OnOff state and publishes it to the network.
 *
 * USE CASES:
 * - User presses physical button on device
 * - Timer expires and changes state
 * - Sensor triggers state change
 *
 * WHAT THIS DOES:
 * 1. Updates local state
 * 2. Updates model state
 * 3. Calls application callback (to control LED)
 * 4. Publishes state change to network (if publication configured)
 *
 * NOTE: Publishing requires the provisioner to configure publication
 * (destination address, publish period, etc.)
 */
esp_err_t node_set_onoff_state(uint8_t onoff)
{
    onoff_state = onoff;
    onoff_server.state.onoff = onoff;
    onoff_server.state.target_onoff = onoff;

    // Notify application
    if (app_callbacks.onoff_changed) {
        app_callbacks.onoff_changed(onoff);
    }

    ESP_LOGI(TAG, "OnOff state changed to: %d", onoff);

    // TODO: Publish state change to network
    // Requires: esp_ble_mesh_server_model_send_msg()

    return ESP_OK;
}

/*
 * PROVISIONING CALLBACK
 * =====================
 *
 * Handles provisioning-related events.
 *
 * KEY EVENTS FOR NODE:
 * - PROV_REGISTER_COMP: BLE Mesh stack initialized successfully
 * - NODE_PROV_LINK_OPEN: Provisioning started (bearer established)
 * - NODE_PROV_LINK_CLOSE: Provisioning ended (success or failure)
 * - NODE_PROV_COMPLETE: Provisioning successful! Node is now part of network
 * - NODE_PROV_RESET: Node unprovisioned (factory reset)
 */
static void mesh_prov_cb(esp_ble_mesh_prov_cb_event_t event,
                        esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "BLE Mesh provisioning registered, err_code %d", param->prov_register_comp.err_code);
        break;

    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "Node provisioning enabled, err_code %d", param->node_prov_enable_comp.err_code);
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
        // Note: element count is in our composition data, not in the event

        // Notify application
        if (app_callbacks.provisioned) {
            app_callbacks.provisioned(param->node_prov_complete.addr);
        }
        break;

    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        ESP_LOGI(TAG, "Node reset - returning to unprovisioned state");

        // Notify application to clear state
        if (app_callbacks.reset) {
            app_callbacks.reset();
        }
        break;

    default:
        break;
    }
}

/*
 * CONFIGURATION SERVER CALLBACK
 * ==============================
 *
 * Handles configuration-related events.
 * The provisioner uses Configuration Client to send configuration commands,
 * and this Configuration Server handles them.
 *
 * KEY EVENTS:
 * - APP_KEY_ADD: Provisioner added an application key
 * - MODEL_APP_BIND: Provisioner bound AppKey to a model
 * - MODEL_SUB_ADD: Provisioner added subscription (for group messages)
 * - MODEL_PUB_SET: Provisioner configured publication settings
 */
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
            break;

        case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
            ESP_LOGI(TAG, "Model app bind: ElementAddr=0x%04x, AppKeyIndex=0x%04x, ModelID=0x%04x",
                     param->value.state_change.mod_app_bind.element_addr,
                     param->value.state_change.mod_app_bind.app_idx,
                     param->value.state_change.mod_app_bind.model_id);
            break;

        default:
            break;
        }
        break;

    default:
        break;
    }
}

/*
 * GENERIC SERVER CALLBACK
 * ========================
 *
 * Handles Generic OnOff Server events.
 *
 * KEY EVENTS:
 * - GEN_ONOFF_GET: Provisioner/client queries current state
 *   Response is automatic (we configured ESP_BLE_MESH_SERVER_AUTO_RSP)
 *
 * - GEN_ONOFF_SET/SET_UNACK: Provisioner/client changes state
 *   We update our state and notify the application (to control LED)
 *
 * SET vs SET_UNACK:
 * - SET: Requires acknowledgment (more reliable)
 * - SET_UNACK: No acknowledgment (faster, less overhead)
 */
static void mesh_generic_server_cb(esp_ble_mesh_generic_server_cb_event_t event,
                                    esp_ble_mesh_generic_server_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_GENERIC_SERVER_STATE_CHANGE_EVT:
        switch (param->ctx.recv_op) {
        case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET:
        case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK:
            onoff_state = param->value.state_change.onoff_set.onoff;
            ESP_LOGI(TAG, "OnOff state changed to: %d", onoff_state);

            // Notify application (e.g., to control LED)
            if (app_callbacks.onoff_changed) {
                app_callbacks.onoff_changed(onoff_state);
            }
            break;

        default:
            break;
        }
        break;

    case ESP_BLE_MESH_GENERIC_SERVER_RECV_GET_MSG_EVT:
        ESP_LOGI(TAG, "Received Generic OnOff Get");
        // Response is automatic due to ESP_BLE_MESH_SERVER_AUTO_RSP
        break;

    case ESP_BLE_MESH_GENERIC_SERVER_RECV_SET_MSG_EVT:
        ESP_LOGI(TAG, "Received Generic OnOff Set");
        // State change already handled in STATE_CHANGE_EVT above
        break;

    default:
        break;
    }
}
