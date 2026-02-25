# Project Overview - Qt + X-Plane + BlueSky Integration

## Table of Contents

1. [Project Architecture](#project-architecture)
2. [Source Files and Their Roles](#source-files-and-their-roles)
3. [Data Flow](#data-flow)
4. [Port Configuration](#port-configuration)
5. [Startup Steps](#startup-steps)

---

## Project Architecture

### High-Level System Diagram

```
┌─────────────────────────────────────────────────────────┐
│                    X-Plane Simulator                     │
│  ┌──────────────────────────────────────────────────┐  │
│  │  Ownship Aircraft (User Controlled)                │  │
│  │  UDP DATA Protocol → Port 49002                    │  │
│  └──────────────────────────────────────────────────┘  │
└───────────────────────┬─────────────────────────────────┘
                        │ UDP DATA Packets
                        │ (Binary: lat, lon, alt, hdg, etc.)
                        ↓
┌─────────────────────────────────────────────────────────┐
│              Qt Overlay Application (macOS)              │
│  ┌──────────────────────────────────────────────────┐  │
│  │  XPlaneReceiver                                  │  │
│  │  - Listens on port 49002                         │  │
│  │  - Parses X-Plane DATA protocol                   │  │
│  │  - Updates AircraftManager                        │  │
│  └──────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────┐  │
│  │  BlueSkyCommunicator                              │  │
│  │  - Sends ownship to Bridge (port 49003)          │  │
│  │  - Receives traffic from Bridge (port 49004)     │  │
│  └──────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────┐  │
│  │  TrafficDisplayWidget                            │  │
│  │  - Transparent overlay rendering                  │  │
│  │  - Red squares for traffic                        │  │
│  │  - Crosshair at center                            │  │
│  └──────────────────────────────────────────────────┘  │
└───────────────────────┬─────────────────────────────────┘
                        │ JSON (ownship position)
                        │ Port 49003
                        ↓
┌─────────────────────────────────────────────────────────┐
│              Python Bridge (bluesky_bridge.py)            │
│  ┌──────────────────────────────────────────────────┐  │
│  │  - Receives ownship from Qt (port 49003)          │  │
│  │  - Creates/updates OWN001 in BlueSky              │  │
│  │  - Subscribes to BlueSky ACDATA                   │  │
│  │  - Sends traffic to Qt (port 49004)               │  │
│  └──────────────────────────────────────────────────┘  │
└───────────────────────┬─────────────────────────────────┘
                        │ BlueSky Client API
                        │ Ports 11000/11001
                        ↓
┌─────────────────────────────────────────────────────────┐
│              BlueSky Simulator                           │
│  ┌──────────────────────────────────────────────────┐  │
│  │  - OWN001 (ownship, synced with X-Plane)          │  │
│  │  - TFC001, TFC002 (AI traffic aircraft)           │  │
│  │  - Sends ACDATA updates to Bridge                  │  │
│  └──────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

---

## Source Files and Their Roles

### Core Data Structures

#### `AircraftData.h`
**Purpose**: Defines the fundamental data structure for aircraft (both ownship and traffic).

**Key Fields**:
- Position: `latitude`, `longitude`, `altitude` (feet)
- Kinematics: `heading` (degrees), `speed` (knots), `verticalSpeed`
- Attitude: `pitch`, `roll`, `yaw` (degrees)
- Display: `screenX`, `screenY` (calculated screen coordinates)
- Identity: `callsign`, `isOwnship` flag

**Usage**: Used throughout the application to represent aircraft state.

---

#### `AircraftManager.h` / `AircraftManager.cpp`
**Purpose**: Central data management class for all aircraft in the system.

**Key Responsibilities**:
- Maintains `QMap<QString, AircraftData>` for all traffic aircraft
- Maintains dedicated `AircraftData m_ownship` for the ownship
- Provides accessors: `getOwnship()`, `getAllAircraft()`, `getOrCreateAircraft(callsign)`
- **`updateOwnshipPosition(...)`**: Called by `XPlaneReceiver` when X-Plane data arrives
  - Converts units (meters → feet, m/s → knots)
  - Updates ownship data structure
  - Emits `ownshipUpdated()` signal
- **`updateAircraft(...)`**: Called by `BlueSkyCommunicator` when traffic data arrives
  - Updates or creates traffic aircraft entries
  - Emits `aircraftUpdated(callsign)` signal

**Signals**:
- `ownshipUpdated()` - Emitted when ownship data changes
- `aircraftUpdated(QString)` - Emitted when a traffic aircraft is updated
- `newAircraftAdded(QString)` - Emitted when a new aircraft is detected

---

### Main Application Window

#### `MainWindow.h` / `MainWindow.cpp`
**Purpose**: Main application window and integration hub for all components.

**Key Responsibilities**:

1. **Component Management**:
   - Creates and manages `AircraftManager`, `XPlaneReceiver`, `BlueSkyCommunicator`, `TrafficDisplayWidget`
   - Connects signals and slots between components

2. **Overlay Window Configuration**:
   - **`setupOverlayWindow()`**: Configures window as transparent, frameless, always-on-top overlay
     - Sets window flags: `FramelessWindowHint`, `WindowStaysOnTopHint`, `WindowDoesNotAcceptFocus`
     - Sets attributes: `WA_TranslucentBackground`, `WA_ShowWithoutActivating`, `WA_NoSystemBackground`
     - Uses macOS native APIs (`NSWindow`) for advanced window control:
       - `setLevel: 2002` (ScreenSaver level, highest)
       - `setCollectionBehavior: 17` (CanJoinAllSpaces | FullScreenAuxiliary)
       - `setHidesOnDeactivate: false`
       - `orderFront:` (brings window forward without stealing focus)
   - **`ensureOnTop()`**: Timer-based function to keep window visible and on top
   - **`onKeepOnTop()`**: Slot called periodically (every 500ms) to maintain window visibility

3. **Event Handling**:
   - **`eventFilter(...)`**: Custom event filter for selective mouse/keyboard penetration
     - Keyboard events: Returns `false` to allow pass-through to underlying applications
     - Mouse events: Checks if mouse is over Quit button; if yes, forwards to button; otherwise ignores (penetration)
   - **`event(QEvent*)`**: Override to intercept keyboard events at highest level, returns `false` to allow pass-through
   - **`changeEvent(...)`**: Handles window activation, calls `resignKeyWindow` via macOS API to prevent focus stealing

4. **UI Setup**:
   - **`setupUI()`**: Creates and arranges UI elements
     - Status labels: `XP: OK/--` and `BS: OK/--` (top-left)
     - Quit button (bottom-left)
     - Central widget: `TrafficDisplayWidget` (full-screen transparent area)
   - Layout: Uses `QVBoxLayout` and `QHBoxLayout` for positioning

5. **Data Flow Coordination**:
   - **`onXPlaneDataReceived()`**: Slot connected to `XPlaneReceiver::ownshipDataReceived()`
     - Reads ownship data from `AircraftManager`
     - Calls `BlueSkyCommunicator::sendOwnshipPosition(...)` to forward to Bridge
   - Connects status signals: `connectionStatusChanged` from receivers to update status labels

6. **Initialization**:
   - **Constructor**: Creates all components, calls `setupOverlayWindow()`, `setupUI()`, `connectSignals()`
   - Starts `XPlaneReceiver` listening on port 49002
   - Connects `BlueSkyCommunicator` to port 49004 (receives traffic from Bridge)

**Port Configuration** (in constructor):
- `m_xplaneReceiver->startListening(49002)` - Listen for X-Plane data
- `m_blueSkyComm->connectToBlueSky("127.0.0.1", 49004)` - Listen for Bridge traffic

---

#### `TrafficDisplayWidget.h` / `TrafficDisplayWidget.cpp`
**Purpose**: Custom QWidget that renders the HUD overlay graphics.

**Key Responsibilities**:

1. **Transparent Rendering**:
   - **`paintEvent(QPaintEvent*)`**: Main rendering function
     - Calls `drawBackground()` - Draws crosshair lines only
     - Calls `drawAircrafts()` - Draws traffic aircraft as red squares
     - Calls `drawInfo()` - Draws ownship info panel
   - Uses `QPainter` with antialiasing for smooth graphics

2. **Background Drawing** (`drawBackground()`):
   - Draws only solid crosshair lines (vertical and horizontal) at screen center
   - No grid lines, no background fill (fully transparent)
   - Semi-transparent gray lines (`QColor(100, 100, 100, 150)`)

3. **Aircraft Rendering** (`drawAircrafts()`):
   - Iterates through `AircraftManager::getAllAircraft()`
   - **Skips ownship** (`if (ac.isOwnship) continue`) - Ownship is represented by X-Plane's own display
   - Calculates relative position from ownship to traffic:
     - `deltaLon = ac.longitude - ownship->longitude`
     - `deltaLat = ac.latitude - ownship->latitude`
     - Converts to screen coordinates using latitude factor: `cos(latitude)` for longitude scaling
   - Draws red squares (`QColor(255, 0, 0)`) for each traffic aircraft
   - Draws callsign labels in red
   - Optionally draws distance text (if within 50km)

4. **Info Panel** (`drawInfo()`):
   - Displays ownship data: HDG, SPD, ALT, PITCH, ROLL
   - Semi-transparent black background panel
   - White text, positioned at top-right

5. **Event Handling**:
   - **`setFocusPolicy(Qt::NoFocus)`**: Prevents widget from receiving keyboard focus
   - **`setAttribute(Qt::WA_TransparentForMouseEvents, true)`**: Makes widget transparent to mouse clicks
   - **`keyPressEvent(...)`**: Override that does nothing (allows keyboard pass-through)

6. **Refresh Timer**:
   - `QTimer` triggers `update()` every 100ms (10Hz refresh rate)
   - Connected to `AircraftManager` signals for immediate updates on data changes

---

### X-Plane Communication

#### `XPlaneReceiver.h` / `XPlaneReceiver.cpp`
**Purpose**: Receives and parses binary UDP DATA protocol packets from X-Plane.

**Key Responsibilities**:

1. **UDP Socket Management**:
   - **`startListening(port)`**: Binds `QUdpSocket` to specified port (default: 49002)
     - Tries `AnyIPv4`, then `AnyIPv6`, then `Any` for compatibility
     - Sets `ShareAddress` and `ReuseAddressHint` options
     - Emits `connectionStatusChanged(true)` on success
   - **`onDataReceived()`**: Slot connected to `QUdpSocket::readyRead`
     - Reads complete datagrams
     - Calls `parseDataPacket(...)` for processing

2. **X-Plane DATA Protocol Parsing** (`parseDataPacket()`):
   - **Header Validation**: Checks for `"DATA"` (4 bytes) + `0x3c` (1 byte) header
   - **Record Parsing**: Each record is 36 bytes:
     - Bytes 0-3: Index (int, little-endian)
     - Bytes 4-35: 8 float values (4 bytes each, little-endian)
   - Uses `memcpy` for robust byte-level parsing (avoids alignment issues)
   - Processes multiple records per packet

3. **Data Index Mapping** (`parseDataItem()`):
   - **Index 17**: Attitude - Pitch, Roll, Heading (values[0], values[1], values[2])
   - **Index 19**: Altitude (meters, MSL) - values[0]
   - **Index 20**: Position - Latitude, Longitude, Altitude (values[0], values[1], values[2])
   - **Index 3**: Ground Speed (m/s) - values[0]
   - **Index 4**: Vertical Speed (m/s) - values[0]
   - Stores parsed values in `m_currentPacket` structure

4. **Unit Conversion and Update**:
   - When lat/lon are available, converts units:
     - Altitude: meters → feet (`* 3.28084`)
     - Speed: m/s → knots (`* 1.94384`)
     - Vertical Speed: m/s → ft/min (`* 196.85`)
   - Calls `AircraftManager::updateOwnshipPosition(...)` with converted values
   - Emits `ownshipDataReceived()` signal

5. **Error Handling**:
   - Validates packet size: Must be `5 + N*36` bytes
   - Validates header format
   - Emits `errorOccurred(QString)` on parsing errors
   - Emits `connectionStatusChanged(false)` on socket errors

**Data Structure** (`CurrentPacket`):
```cpp
struct CurrentPacket {
    double latitude, longitude, altitude_m;
    double heading, pitch, roll;
    double speed_ms, verticalSpeed_ms;
    bool hasLat, hasLon, hasAlt, hasHdg, hasPitch, hasRoll, hasSpeed, hasVS;
};
```

---

### BlueSky Communication

#### `BlueSkyCommunicator.h` / `BlueSkyCommunicator.cpp`
**Purpose**: Manages bidirectional UDP communication with BlueSky via the Python bridge.

**Key Responsibilities**:

1. **Socket Management**:
   - **`m_receiveSocket`**: `QUdpSocket` for receiving traffic data from Bridge (port 49004)
   - **`m_sendSocket`**: `QUdpSocket` for sending ownship data to Bridge (port 49003)
   - **`connectToBlueSky(host, port)`**: Binds `m_receiveSocket` to specified port
     - Emits `connectionStatusChanged(true)` on success
     - Used by `MainWindow` to connect to Bridge's traffic port

2. **Sending Ownship Data** (`sendOwnshipPosition(...)`):
   - Constructs JSON object:
     ```json
     {
       "type": "ownship_position",
       "latitude": lat,
       "longitude": lon,
       "altitude": alt_ft,
       "heading": heading,
       "speed": speed_kts
     }
     ```
   - Sends to Bridge's command port (49003) via UDP
   - Called by `MainWindow::onXPlaneDataReceived()` when X-Plane data arrives
   - Logs every 100th send to reduce console spam

3. **Receiving Traffic Data** (`onDataReceived()`):
   - Reads UDP datagrams from Bridge
   - Calls `parseTrafficData(...)` for processing

4. **Traffic Data Parsing** (`parseJsonTrafficData()`):
   - Parses JSON format:
     ```json
     {
       "type": "traffic_data",
       "data": [
         {
           "callsign": "TFC001",
           "latitude": lat,
           "longitude": lon,
           "altitude": alt_ft,
           "heading": hdg,
           "speed": speed_kts,
           "isOwnship": false
         },
         ...
       ]
     }
     ```
   - For each aircraft in array:
     - Skips if `isOwnship == true` (ownship data comes from X-Plane)
     - Calls `AircraftManager::updateAircraft(callsign, data)` for traffic
   - Emits `trafficDataReceived(aircraftCount)` signal

5. **Error Handling**:
   - Emits `errorOccurred(QString)` on JSON parse errors
   - Emits `connectionStatusChanged(false)` on socket errors

---

### Legacy Network Receiver

#### `NetworkReceiver.h` / `NetworkReceiver.cpp`
**Purpose**: Legacy generic UDP receiver for JSON aircraft data (kept for backward compatibility).

**Current Status**: Not actively used in main data flow. The system now uses:
- `XPlaneReceiver` for X-Plane data
- `BlueSkyCommunicator` for BlueSky data

**Functionality**: Can receive generic JSON aircraft data packets, but primary flow goes through specialized receivers.

---

### Python Bridge Component

#### `bluesky_bridge.py`
**Purpose**: Python bridge script that connects Qt application with BlueSky simulator.

**Key Responsibilities**:

1. **Initialization** (`__init__`):
   - Creates UDP sockets:
     - `self.sock`: Sends traffic data to Qt (port 49004)
     - `self.command_sock`: Receives ownship commands from Qt (port 49003)
   - Handles port conflicts: Tries alternative ports if default is in use
   - Creates BlueSky `Client` instance
   - Sets `ownship_callsign = "OWN001"`

2. **BlueSky Connection** (`connect_to_bluesky()`):
   - Connects to BlueSky via `Client` (ports 11000/11001)
   - Activates first available node
   - Subscribes to ACDATA topic: `subscribe('ACDATA', actonly=True)`
   - Connects ACDATA callback: `on_acdata_update()`
   - Checks initial traffic count

3. **ACDATA Processing** (`on_acdata_update()`):
   - Receives ACDATA updates from BlueSky
   - Converts to aircraft list format via `convert_acdata_to_list()`
   - Caches traffic data in `self.traffic_data`

4. **Traffic Data Conversion** (`convert_acdata_to_list()`):
   - Handles multiple ACDATA formats:
     - Dict with numpy arrays: `{'id': [...], 'lat': [...], ...}`
     - Object with attributes: `acdata.id`, `acdata.lat`, ...
     - None/empty: Falls back to direct `bs.traf` access
   - Converts units: meters → feet, m/s → knots
   - Returns list of aircraft dictionaries

5. **Traffic Data Retrieval** (`get_traffic_data()`):
   - Tries direct access to `bs.traf` first (most reliable)
   - Falls back to cached `self.traffic_data` if direct access fails
   - Returns empty list if no data available

6. **Command Handling** (`handle_control_command()`):
   - Receives JSON commands from Qt on command port (49003)
   - **Ownship Position** (`type: "ownship_position"`):
     - Extracts lat, lon, alt (ft), heading, speed (kts)
     - Converts units: feet → meters, knots → m/s
     - Checks if OWN001 exists in BlueSky
     - If not exists: Creates via `stack.CRE` command
     - If exists: Updates via `stack.MOVE` command
   - **Control Commands** (`type: "control_command"`): (Legacy, not currently used)
     - Can send heading/speed/altitude commands to BlueSky

7. **Main Loop** (`run()`):
   - Continuously:
     - Updates BlueSky client: `self.client.update()`
     - Checks for commands on command socket (non-blocking, 0.1s timeout)
     - Processes commands via `handle_control_command()`
     - Every 0.1s (10Hz): Gets traffic data and sends to Qt via `send_to_qt()`

8. **Traffic Forwarding** (`send_to_qt()`):
   - Batches aircraft into groups of 50
   - Sends JSON packets to Qt on port 49004:
     ```json
     {
       "type": "traffic_data",
       "data": [...],
       "batch_index": 0,
       "total_batches": 2
     }
     ```

**Port Configuration**:
- Command port (receive): 49003 (default, configurable via `--command-port`)
- Traffic port (send): 49004 (default, configurable via `--qt-port`)
- BlueSky: 11000 (receive), 11001 (send)

---

### Scenario Creator Component

#### `bluesky_scenario_creator.py`
**Purpose**: Optional helper script that automatically creates a test scenario in BlueSky.

**Key Responsibilities**:

1. **Initialization** (`__init__`):
   - Connects to BlueSky via `Client` (ports 11000/11001)
   - Activates first available node
   - Subscribes to ACDATA topic for traffic detection
   - Sets `ownship_callsign = "OWN001"`, `traffic_callsigns = ["TFC001", "TFC002"]`

2. **Ownship Detection** (`check_ownship_in_bluesky()`):
   - Updates BlueSky client: `self.client.update()`
   - Tries multiple methods to detect aircraft:
     - **Method 1**: ACDATA subscription cache (`self.cached_traffic_data`)
     - **Method 2**: Direct `bs.traf` access (if available)
   - Searches for OWN001 in detected aircraft
   - If found: Extracts position data and calls `create_scenario(...)`
   - If not found but aircraft exist: Uses first aircraft as ownship

3. **ACDATA Callback** (`on_acdata_update()`):
   - Receives ACDATA updates from BlueSky
   - Converts to aircraft list format
   - Updates `self.cached_traffic_data` cache

4. **Scenario Creation** (`create_scenario()`):
   - **Input**: Ownship position (lat, lon, alt_ft, hdg, speed_kts)
   - **Calculations**:
     - Converts units: feet → meters, knots → m/s
     - Calculates traffic positions:
       - TFC001: Ahead-right (15° offset, ~5.5km distance)
       - TFC002: Ahead-right (45° offset, ~5.5km distance)
     - Sets traffic speeds: 80% and 90% of ownship speed
   - **Execution**:
     - Clears existing traffic (except OWN001 if it exists)
     - Creates/updates OWN001 if needed
     - Creates TFC001 and TFC002 via `stack.CRE` commands
     - Sends `OP` commands to ensure aircraft are operational
   - **Verification**: Checks BlueSky traffic count after creation

5. **Main Loop** (`run()`):
   - Continuously:
     - Updates BlueSky client: `self.client.update()`
     - Every 0.5s: Calls `check_ownship_in_bluesky()`
   - Runs until interrupted (Ctrl+C)

**Usage**: Run after Bridge has created OWN001 in BlueSky. Will automatically detect ownship and create traffic scenario.

---

### Utility Scripts

#### `check_xplane_data.py`
**Purpose**: Diagnostic tool to verify X-Plane UDP data output independently.

**Functionality**:
- Binds UDP socket to specified port (default: 49001)
- Receives and displays raw X-Plane DATA packets
- Parses and displays data indices and values
- Helps troubleshoot X-Plane configuration issues

**Usage**:
```bash
python3 check_xplane_data.py [port]
# Example: python3 check_xplane_data.py 49002
```

---

#### `run_bridge.sh`
**Purpose**: Convenience script to run `bluesky_bridge.py` using virtual environment Python.

**Functionality**:
- Checks for `venv/bin/python3`
- Uses venv Python if available, falls back to system Python
- Passes all arguments to `bluesky_bridge.py`

**Usage**:
```bash
./run_bridge.sh [--qt-port 49004] [--command-port 49003]
```

---

#### `run_scenario_creator.sh`
**Purpose**: Convenience script to run `bluesky_scenario_creator.py` using virtual environment Python.

**Functionality**: Same as `run_bridge.sh` but for scenario creator.

**Usage**:
```bash
./run_scenario_creator.sh
```

---

#### `test_three_way_connection.sh`
**Purpose**: Automated test script that launches all components in correct order.

**Functionality**:
- Checks port status for X-Plane and BlueSky
- Launches Qt application
- Launches Bridge
- Launches Scenario Creator
- Provides status updates

**Usage**:
```bash
./test_three_way_connection.sh
```

---

#### `diagnose_xplane_connection.sh`
**Purpose**: Diagnostic script to troubleshoot X-Plane connection issues.

**Functionality**:
- Checks port 49000 status
- Checks if Qt app is running
- Checks if X-Plane is running
- Tests UDP connectivity
- Provides X-Plane configuration checklist

**Usage**:
```bash
./diagnose_xplane_connection.sh
```

---

### Build and Configuration Files

#### `QtBlueSkyDemo.pro`
**Purpose**: qmake project file that defines the Qt application build configuration.

**Key Settings**:
- **Qt Modules**: `core`, `widgets`, `network`
- **C++ Standard**: C++11
- **macOS Specific**:
  - Links frameworks: `Cocoa`, `Carbon`
  - Uses custom `Info.plist` via `QMAKE_INFO_PLIST = Info.plist`
- **Source Files**: Lists all `.cpp` files
- **Header Files**: Lists all `.h` files

**Build Command**:
```bash
qmake QtBlueSkyDemo.pro
make
```

---

#### `main.cpp`
**Purpose**: Application entry point.

**Functionality**:
- Creates `QApplication` instance
- Creates `MainWindow` instance
- Calls `window.show()` to display overlay
- Enters Qt event loop: `app.exec()`

**Minimal code**: Standard Qt application structure.

---

#### `Info.plist`
**Purpose**: macOS application property list file.

**Key Settings**:
- **`LSUIElement = true`**: Critical setting that makes app a UI Element
  - App does not appear in Dock
  - App does not hide when losing focus
  - Essential for overlay behavior on macOS
- **`CFBundleExecutable = QtBlueSkyDemo`**: Application executable name
- **`LSMinimumSystemVersion = 10.13`**: Minimum macOS version

**Location**: Root directory (copied to app bundle during build).

---

## Data Flow

### 1. X-Plane → Qt Application

**Protocol**: X-Plane UDP DATA (binary)

**Process**:
1. X-Plane sends DATA packets to configured port (49002)
2. `XPlaneReceiver::onDataReceived()` receives datagram
3. `parseDataPacket()` validates header and parses 36-byte records
4. `parseDataItem()` extracts values for indices 17, 18, 19, 20, 21, 22, 3, 4
5. Units converted: meters → feet, m/s → knots
6. `AircraftManager::updateOwnshipPosition()` updates ownship data
7. `ownshipDataReceived()` signal emitted
8. `MainWindow::onXPlaneDataReceived()` slot triggered

**Data Extracted**:
- Latitude, Longitude (degrees)
- Altitude (feet, MSL)
- Heading (degrees)
- Pitch, Roll (degrees)
- Ground Speed (knots)
- Vertical Speed (ft/min)

---

### 2. Qt Application → Bridge

**Protocol**: UDP JSON

**Process**:
1. `MainWindow::onXPlaneDataReceived()` reads ownship from `AircraftManager`
2. `BlueSkyCommunicator::sendOwnshipPosition(...)` constructs JSON:
   ```json
   {
     "type": "ownship_position",
     "latitude": 47.837498,
     "longitude": 12.982253,
     "altitude": 8540,
     "heading": 258.5,
     "speed": 190.8
   }
   ```
3. JSON sent via UDP to Bridge command port (49003)
4. Bridge receives on `command_sock` and calls `handle_control_command()`

**Frequency**: Every time X-Plane data arrives (typically 20-30 Hz)

---

### 3. Bridge → BlueSky

**Protocol**: BlueSky Client API (`bluesky.network.client.Client`)

**Process**:
1. Bridge receives ownship JSON from Qt
2. `handle_control_command()` parses JSON
3. Checks if OWN001 exists in BlueSky (`bs.traf.id`)
4. **If not exists**: Creates via `stack.CRE` command:
   ```
   CRE OWN001 B744 lat lon hdg alt_m speed_ms
   ```
5. **If exists**: Updates via `stack.MOVE` command:
   ```
   MOVE OWN001,lat,lon,alt_m,hdg,speed_ms
   ```
6. BlueSky updates ownship position in simulation

**Frequency**: Every time Bridge receives ownship update from Qt

---

### 4. BlueSky → Bridge

**Protocol**: BlueSky ACDATA subscription

**Process**:
1. Bridge subscribes to ACDATA topic: `subscribe('ACDATA', actonly=True)`
2. BlueSky sends ACDATA updates automatically
3. `on_acdata_update()` callback receives data
4. `convert_acdata_to_list()` converts to aircraft list format
5. Traffic data cached in `self.traffic_data`

**Frequency**: BlueSky's update rate (typically 10 Hz)

---

### 5. Bridge → Qt Application

**Protocol**: UDP JSON

**Process**:
1. Bridge's main loop calls `get_traffic_data()` every 0.1s (10Hz)
2. `send_to_qt()` batches aircraft into groups of 50
3. Sends JSON packets to Qt traffic port (49004):
   ```json
   {
     "type": "traffic_data",
     "data": [
       {
         "callsign": "TFC001",
         "latitude": 47.850000,
         "longitude": 12.990000,
         "altitude": 8540,
         "heading": 258.5,
         "speed": 152.6,
         "isOwnship": false
       },
       ...
     ],
     "batch_index": 0,
     "total_batches": 1
   }
   ```
4. `BlueSkyCommunicator::onDataReceived()` receives datagram
5. `parseJsonTrafficData()` parses JSON
6. For each aircraft (excluding `isOwnship`):
   - `AircraftManager::updateAircraft(callsign, data)` updates traffic
7. `trafficDataReceived(count)` signal emitted

**Frequency**: 10 Hz (every 0.1 seconds)

---

### 6. Qt Application → Display

**Process**:
1. `TrafficDisplayWidget` receives `aircraftUpdated()` signals from `AircraftManager`
2. `paintEvent()` called periodically (10Hz) or on signal
3. `drawAircrafts()` iterates through `AircraftManager::getAllAircraft()`
4. Calculates relative positions from ownship to traffic
5. Converts lat/lon differences to screen coordinates
6. Draws red squares and labels for traffic aircraft
7. `drawInfo()` displays ownship data panel

**Rendering**:
- Ownship: Not drawn (represented by X-Plane's own display)
- Traffic: Red squares with callsigns
- Background: Transparent with crosshair lines only

---

## Port Configuration

### Final Port Mapping

| Data Flow | Source Port | Destination Port | Protocol | Component |
|-----------|------------|-----------------|----------|-----------|
| **X-Plane → Qt** | X-Plane sends | Qt listens **49002** | UDP DATA (binary) | `XPlaneReceiver` |
| **Qt → Bridge** | Qt sends | Bridge listens **49003** | UDP JSON | `BlueSkyCommunicator` → `bluesky_bridge.py` |
| **Bridge → Qt** | Bridge sends | Qt listens **49004** | UDP JSON | `bluesky_bridge.py` → `BlueSkyCommunicator` |
| **Bridge ↔ BlueSky** | Bridge ↔ | BlueSky **11000/11001** | BlueSky Client API | `bluesky_bridge.py` ↔ BlueSky |

### Port Usage Details

- **Port 49002**: X-Plane data output (configured in X-Plane Settings → Data Output)
- **Port 49003**: Bridge command port (receives ownship data from Qt)
- **Port 49004**: Bridge traffic port (sends traffic data to Qt)
- **Ports 11000/11001**: BlueSky default ports (receive/send)

### Configuration in Code

**MainWindow.cpp** (constructor):
```cpp
m_xplaneReceiver->startListening(49002);  // Listen for X-Plane
m_blueSkyComm->connectToBlueSky("127.0.0.1", 49004);  // Listen for Bridge traffic
```

**BlueSkyCommunicator.cpp** (`sendOwnshipPosition`):
```cpp
int commandPort = 49003;  // Send to Bridge command port
m_sendSocket->writeDatagram(data, QHostAddress("127.0.0.1"), commandPort);
```

**bluesky_bridge.py** (`__init__`):
```python
def __init__(self, qt_host='127.0.0.1', qt_port=49004, command_port=49003):
    # qt_port: Send traffic to Qt
    # command_port: Receive ownship from Qt
```

---

## Startup Steps

### Prerequisites Checklist

- [ ] X-Plane installed and configured for UDP data output
- [ ] BlueSky simulator installed and accessible
- [ ] Qt 5.15+ installed (macOS)
- [ ] Python 3 with virtual environment set up
- [ ] Project compiled (`qmake` and `make` completed)

---

### Step-by-Step Startup

#### Step 1: Start BlueSky

```bash
# Navigate to BlueSky directory
cd /path/to/Bluesky
python BlueSky.py
```

**Verification**:
- BlueSky window appears
- Simulation is running (not paused)
- Check BlueSky console for any errors

---

#### Step 2: Start Bridge

```bash
# Navigate to project directory
cd /Users/spaceodyssey/Desktop/Studyy/Thesis/Qt

# Start Bridge
./run_bridge.sh --qt-port 49004 --command-port 49003
```

**Expected Output**:
```
✓ BlueSky imported successfully
✓ Connected to BlueSky at 127.0.0.1:11000/11001
✓ Activated node: ...
✓ Command socket bound to port 49003
✓ Subscribed to ACDATA topic
Forwarding to Qt at 127.0.0.1:49004
```

**Keep this terminal running** - Bridge must stay active.

---

#### Step 3: Configure X-Plane

1. **Launch X-Plane**
2. **Go to Settings → Data Output**
3. **Find "UDP Output" section**
4. **Add new data output**:
   - **IP Address**: `127.0.0.1`
   - **Port**: `49002`
   - **Data Items** (enable these indices):
     - **Index 17**: Attitude (Pitch, Roll, Heading)
     - **Index 19**: Altitude (MSL)
     - **Index 20**: Position (Latitude, Longitude, Altitude)
     - **Index 3**: Ground Speed
     - **Index 4**: Vertical Speed
5. **Start a flight** (preferably airborne for testing)

**Verification**:
- X-Plane should be sending data (check X-Plane's data output window if available)
- Use `check_xplane_data.py 49002` to verify data is being sent

---

#### Step 4: Start Qt Overlay Application

```bash
# In project directory
./QtBlueSkyDemo.app/Contents/MacOS/QtBlueSkyDemo
```

**Expected Behavior**:
- Overlay window appears full-screen
- Transparent background (X-Plane visible underneath)
- Crosshair lines at screen center
- Status labels in top-left: `XP: --` and `BS: --`

**Verification**:
- **XP: OK** should appear when X-Plane data is received
- **BS: OK** should appear when Bridge traffic connection is established
- Mouse and keyboard should still control X-Plane (overlay is transparent to input)

---

#### Step 5: (Optional) Start Scenario Creator

```bash
# In another terminal, project directory
./run_scenario_creator.sh
```

**Expected Output**:
```
✓ Connected to BlueSky at 127.0.0.1:11000/11001
✓ Activated node: ...
✓ Subscribed to ACDATA topic (for client mode)
[DEBUG] Checking BlueSky traffic: 1 aircraft
[SCENARIO] Detected ownship in BlueSky: OWN001
Creating BlueSky Scenario
...
✓ Scenario created successfully!
```

**Verification**:
- Bridge should show ownship creation: `[OWN] Ownship created`
- Scenario Creator should detect ownship and create traffic
- Qt overlay should display red squares for traffic aircraft

---

### Verification Checklist

After all components are running:

- [ ] **X-Plane**: Flight is active, data output configured
- [ ] **Bridge**: Shows `✓ Connected to BlueSky` and `[OWN] Received ownship position`
- [ ] **Qt Overlay**: Shows `XP: OK` and `BS: OK`
- [ ] **Qt Overlay**: Displays ownship info (HDG, SPD, ALT, etc.)
- [ ] **Qt Overlay**: Shows red squares for traffic aircraft (if scenario created)
- [ ] **BlueSky UI**: Shows OWN001 aircraft (synced with X-Plane)
- [ ] **BlueSky UI**: Shows TFC001 and TFC002 (if scenario creator ran)
- [ ] **Input**: Mouse and keyboard still control X-Plane (overlay transparent)

---

### Troubleshooting

#### Qt Overlay shows "XP: --"

**Possible Causes**:
1. X-Plane not sending data
2. Wrong port configured
3. X-Plane configured to listen instead of send

**Solutions**:
- Verify X-Plane Data Output settings (IP: 127.0.0.1, Port: 49002)
- Check X-Plane is sending (not listening) on port 49002
- Use `check_xplane_data.py 49002` to test independently
- Verify Qt app is listening on port 49002 (`lsof -i :49002`)

---

#### Qt Overlay shows "BS: --"

**Possible Causes**:
1. Bridge not running
2. Bridge not connected to BlueSky
3. Port conflict

**Solutions**:
- Verify Bridge is running and shows `✓ Connected to BlueSky`
- Check Bridge output for errors
- Verify Qt app is listening on port 49004 (`lsof -i :49004`)
- Check Bridge is sending to port 49004 (check Bridge output)

---

#### No Traffic Aircraft Displayed

**Possible Causes**:
1. Scenario Creator not run
2. BlueSky has no aircraft
3. Traffic aircraft too far away

**Solutions**:
- Run Scenario Creator: `./run_scenario_creator.sh`
- Check BlueSky UI for aircraft (OWN001, TFC001, TFC002)
- Verify Bridge output shows traffic data: `✓ Sending X aircraft`
- Check Qt overlay zoom/scale settings (traffic may be off-screen)

---

#### Overlay Not Visible

**Possible Causes**:
1. Window hidden or minimized
2. `Info.plist` not configured correctly
3. macOS window management issue

**Solutions**:
- Check `Info.plist` has `LSUIElement = true`
- Rebuild application: `qmake && make`
- Try clicking on X-Plane window to bring overlay forward
- Check macOS Mission Control (overlay may be in different space)

---

#### Keyboard/Mouse Input Blocked

**Possible Causes**:
1. Overlay window has focus
2. Event filter not working correctly

**Solutions**:
- Verify `MainWindow` has `setFocusPolicy(Qt::NoFocus)`
- Check `TrafficDisplayWidget` has `WA_TransparentForMouseEvents`
- Verify `eventFilter` returns `false` for keyboard events
- Check macOS window level settings (may need adjustment)

---

## Exit Procedure

### Normal Exit

1. **Click Quit Button**: Bottom-left corner of overlay
2. **Or**: Press `Ctrl+C` in terminal if running from command line

### Force Exit

If overlay becomes unresponsive:

1. **macOS**: Use Activity Monitor to force quit `QtBlueSkyDemo`
2. **Terminal**: `pkill -f QtBlueSkyDemo`
3. **Bridge**: `Ctrl+C` in Bridge terminal
4. **Scenario Creator**: `Ctrl+C` in Scenario Creator terminal

---

## Additional Notes

### macOS-Specific Considerations

- **`Info.plist`**: The `LSUIElement` setting is critical for overlay behavior
- **Window Level**: Uses macOS native APIs to set window level to ScreenSaver level (2002)
- **Focus Management**: Multiple mechanisms prevent overlay from stealing focus
- **Event Penetration**: Custom event filters ensure mouse/keyboard pass-through

### Performance Considerations

- **Refresh Rates**:
  - X-Plane data: ~20-30 Hz (depends on X-Plane settings)
  - Bridge traffic updates: 10 Hz
  - Qt overlay rendering: 10 Hz
- **Network**: All communication is localhost UDP (very low latency)
- **Rendering**: Minimal graphics (crosshair + red squares) for low CPU usage

### Future Enhancements

Potential improvements:
- Configurable traffic display distance/scale
- Additional HUD elements (altitude tape, speed tape, etc.)
- Traffic conflict detection and alerts
- Historical track display
- Multiple ownship support

---

## File Summary

| File | Type | Purpose |
|------|------|---------|
| `AircraftData.h` | Header | Aircraft data structure definition |
| `AircraftManager.h/cpp` | Class | Central aircraft data management |
| `MainWindow.h/cpp` | Class | Main application window and integration |
| `TrafficDisplayWidget.h/cpp` | Widget | HUD overlay rendering |
| `XPlaneReceiver.h/cpp` | Class | X-Plane UDP DATA protocol receiver |
| `BlueSkyCommunicator.h/cpp` | Class | BlueSky bridge communication |
| `NetworkReceiver.h/cpp` | Class | Legacy generic UDP receiver |
| `bluesky_bridge.py` | Script | Python bridge between Qt and BlueSky |
| `bluesky_scenario_creator.py` | Script | Automatic scenario creation |
| `check_xplane_data.py` | Script | X-Plane data diagnostic tool |
| `main.cpp` | Source | Application entry point |
| `QtBlueSkyDemo.pro` | Project | qmake project file |
| `Info.plist` | Config | macOS application properties |
| `run_bridge.sh` | Script | Bridge launcher |
| `run_scenario_creator.sh` | Script | Scenario creator launcher |
| `test_three_way_connection.sh` | Script | Automated test script |

---

## Conclusion

This system provides a seamless integration between X-Plane, BlueSky, and a Qt overlay, enabling real-time air traffic visualization during flight simulation. The architecture is modular, with clear separation of concerns between data reception, processing, communication, and rendering.

For questions or issues, refer to the troubleshooting section or check component-specific debug output.

