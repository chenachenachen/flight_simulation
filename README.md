# Flight Simulation Collision Alert System
**(Joint-Simulation: X-Plane 12 + BlueSky + Qt Interface)**

This project is an advanced, cross-platform joint-simulation environment. It connects **X-Plane 12** (physics & visuals), **BlueSky** (traffic scenario logic), and a **Qt-based HUD Overlay** (ecological interface for collision alerting). 

It is designed to evaluate predictive conflict detection (CPA), 3D volumetric tunnel alerts, and VFR right-of-way priority cues using dynamic, dual-target geometries.

---

## Phase 1: Global Setup (Start Here)

To run this system from scratch, you need to configure three main components: X-Plane, Python, and Qt.

### 1.1 X-Plane 12 & XPlaneConnect (XPC) Plugin
1. Install **X-Plane 12**.
2. Download the **XPlaneConnect (XPC)** plugin from its official GitHub repository.
3. Unzip and copy the `XPlaneConnect` folder into your X-Plane plugins directory:
   - Path: `X-Plane 12/Resources/plugins/XPlaneConnect`
4. Launch X-Plane 12. Go to `Plugins → Plugin Admin → Enabled` and ensure **XPlaneConnect** is checked.
5. **[CRITICAL] Configure UDP Data Output**:
   - Go to `Settings → Network → Data Output`.
   - Check the box for **"Send network data output to IP address"**.
   - Enter IP: `127.0.0.1`. Enter Port: `49003` (or the port defined in your C++ `XPlaneReceiver`).
   - Switch to the main Data Output table. Check the **Network** checkbox for:
     - **Row 17** (Pitch, Roll, Heading)
     - **Row 20** (Lat, Lon, Alt)

### 1.2 X-Plane AI Aircraft Slots (Crucial for 3D Visuals)
To ensure the 3D planes outside your window match the Qt radar symbols, you **must** configure your AI aircraft in this exact order in X-Plane's Flight Configuration menu (`AI Aircraft` button):
1. **Slot 1**: Sikorsky S-76C (Rotorcraft)
2. **Slot 2**: F-4 Phantom II (UAV / Fighter)
3. **Slot 3**: Airbus A330-300 (Heavy)
4. **Slot 4, 5, 6**: Boeing 737-800 (Default Fixed-Wing)

### 1.3 Python Environment (BlueSky & Bridge)
You need Python 3.10+ installed on your system.
1. Open a Terminal (macOS) or Command Prompt (Windows) and navigate to the `Bluesky/` folder inside this project.
2. Create a virtual environment:
   - **macOS**: `python3 -m venv venv`
   - **Windows**: `python -m venv venv`
3. Activate the virtual environment:
   - **macOS**: `source venv/bin/activate`
   - **Windows**: `venv\Scripts\activate`
4. Install dependencies:
   - `pip install -r requirements.txt`
   - `pip install xpc`

### 1.4 Qt Overlay Compilation
You must compile the C++ UI code for your specific operating system.
1. Install **Qt Creator** (Open Source version is fine) with the appropriate compiler (MinGW/MSVC for Windows, Clang for macOS).
2. Open the project file (`.pro` or `CMakeLists.txt`) in Qt Creator.
3. Select the **Release** build configuration (bottom left icon).
4. Click the **Build (Hammer icon)** to compile the project.
5. Note the path to the generated executable:
   - **macOS**: Usually inside `build-.../Release/QtBlueSkyDemo.app/Contents/MacOS/QtBlueSkyDemo`
   - **Windows**: Usually inside `build-.../release/QtBlueSkyDemo.exe`
6. Open `run_collision_test_full.sh` (Mac) or `run_collision_test_full.bat` (Win) and update the `QT_EXEC` variable to point to this exact path.

---

## Phase 2: How to Run the Simulation

Always follow this exact sequence to avoid port conflicts and ghost processes.

### Step 1: Prepare X-Plane
Launch X-Plane 12, select an aircraft and airport, and **enter the cockpit**. 
*Make sure the game is NOT paused (do not press 'P'). The physics engine must be running to send UDP data.*

### Step 2: Launch the System
Navigate to the project root folder and run the launcher for your OS:

**For macOS / Linux:**
1. Open Terminal.
2. Run: `chmod +x run_collision_test_full.sh` (only needed the first time).
3. Run: `./run_collision_test_full.sh`

**For Windows:**
1. Open File Explorer.
2. Double-click `run_collision_test_full.bat`.
*(If Windows Firewall pops up, you MUST click "Allow access" for both private and public networks, otherwise UDP sockets will timeout).*

### Step 3: Trigger Scenarios
Once the launcher runs, the **BlueSky Console** (a black command window) will appear.
Because the **Live Scenario Engine** is running in the background, it continuously tracks your X-Plane position.

To spawn a test scenario perfectly in front of your windshield, type the following into the BlueSky console and press Enter:
```text
IC scenario/collision_G2_collision.scn

