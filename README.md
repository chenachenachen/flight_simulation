## Overview

This project connects **X‑Plane 12**, **BlueSky** (ATM simulator) and a **Qt overlay**:

- **X‑Plane 12**: renders the outside world and your ownship cockpit.
- **BlueSky**: runs traffic/scenario logic (including the 4 G1–G4 conflict geometries).
- **Bridge** (`bluesky_bridge.py`): mirrors BlueSky traffic to X‑Plane AI aircraft (via XPC / UDP) and to the Qt overlay.
- **Qt overlay**: draws HUD / alerting symbology on top of X‑Plane.

You switch scenarios **only in BlueSky** using `IC scenario/...`, and X‑Plane + Qt follow automatically.

The repository layout (top level):

- `main.cpp`, `TrafficDisplayWidget.cpp`, … – Qt overlay sources.
- `Bluesky/` – vendored BlueSky simulator (Python).
- `bluesky_bridge.py` – X‑Plane ↔ BlueSky ↔ Qt bridge.
- `run_collision_test_full.sh` – macOS launcher.
- `run_collision_test_full.bat` – Windows launcher.

---

## 1. Prerequisites

### 1.1 Install X‑Plane 12 and XPlaneConnect (XPC) plugin

1. Install **X‑Plane 12** normally.
2. Download the **XPlaneConnect** plugin for your platform (Windows or macOS) from the official repository.
3. Unzip and copy the plugin folder to:
   - **Windows / macOS**:  
     `X-Plane 12/Resources/plugins/XPlaneConnect`
4. Start X‑Plane 12 and verify:
   - Menu: **Plugins → Plugin Admin → Enabled** – confirm **XPlaneConnect** is listed and checked.
   - (Optional) In `X-Plane 12/Log.txt` search for `XPlaneConnect` – you should see a line indicating it loaded successfully.

If the plugin is missing or disabled, `xpc.XPlaneConnect()` in `bluesky_bridge.py` will fail and no traffic will appear in X‑Plane.

### 1.2 Python environment for BlueSky + bridge

We run BlueSky and the bridge from a dedicated virtualenv inside this repo.

1. Install **Python 3.10+** (from python.org or your package manager).
2. Create a virtualenv:
   - **macOS / Linux**:
     ```bash
     cd /path/to/Qt/Bluesky
     python3 -m venv venv
     source venv/bin/activate
     ```
   - **Windows**:
     ```bat
     cd \path\to\Qt\Bluesky
     py -3 -m venv venv
     call venv\Scripts\activate.bat
     ```
3. Install BlueSky and Python dependencies:
   - This repo vendors BlueSky under `Bluesky/`.  
     Follow `Bluesky/README.md`. If a `requirements.txt` is present, you can run:
     ```bash
     pip install -r requirements.txt
     ```
4. Quick sanity check:
   ```bash
   cd /path/to/Qt/Bluesky
   source venv/bin/activate       # or venv\Scripts\activate.bat on Windows
   python BlueSky.py --help
   ```
   If this prints BlueSky’s help text instead of errors, your Python side is OK.

### 1.3 Build the Qt overlay (`QtBlueSkyDemo`)

You need a Qt‑based executable that runs `main.cpp`, `TrafficDisplayWidget.cpp`, etc. We’ll call it `QtBlueSkyDemo`.

#### Option A – Qt Creator (recommended)

1. Start **Qt Creator**.
2. `File → Open File or Project…` and open your Qt project file in this folder:
   - `CMakeLists.txt` (CMake project), or
   - `*.pro` (qmake project).
3. Configure a **Desktop kit**:
   - Windows: MSVC or MinGW + Qt.
   - macOS: Clang + Qt.
4. Ensure the target name is set to something like:
   - **CMake**:
     ```cmake
     add_executable(QtBlueSkyDemo
         main.cpp
         TrafficDisplayWidget.cpp
         ...
     )
     ```
   - **qmake**:
     ```pro
     TARGET = QtBlueSkyDemo
     ```
5. Select the **Release** build configuration.
6. Click **Build** (hammer icon).
7. After build finishes, locate the executable:
   - **Windows**: `build-.../release/QtBlueSkyDemo.exe`
   - **macOS**: `build-.../release/QtBlueSkyDemo.app`
8. Copy/move the executable to the project root (same directory as this `README.md`), or note its absolute path.

#### Option B – CMake from the command line

If you maintain a `CMakeLists.txt`:

```bash
cd /path/to/Qt
mkdir build
cd build

# macOS / Linux
cmake ..
cmake --build . --config Release

# Windows (from a VS or MinGW shell)
cmake .. -G "Ninja"        # or a Visual Studio generator
cmake --build . --config Release
```

The executable will typically be in:

- Windows: `build/Release/QtBlueSkyDemo.exe`
- macOS: `build/QtBlueSkyDemo.app`

We will point the launch scripts to this executable in the next steps.

---

## 2. macOS: End‑to‑end startup (X‑Plane + BlueSky + Bridge + Qt)

### 2.1 One‑time configuration

1. Ensure the BlueSky virtualenv exists and works (section 1.2).
2. Ensure the Qt overlay app exists (section 1.3).
3. Edit `run_collision_test_full.sh` in the project root:
   - Set `QT_EXEC` to your Qt overlay executable, for example:
     ```bash
     QT_EXEC="${SCRIPT_DIR}/QtBlueSkyDemo.app/Contents/MacOS/QtBlueSkyDemo"
     ```
   - Make it executable:
     ```bash
     chmod +x QtBlueSkyDemo.app/Contents/MacOS/QtBlueSkyDemo
     chmod +x run_collision_test_full.sh
     ```

### 2.2 Per‑session run sequence (from zero)

1. **Start X‑Plane 12 manually** and load any flight so that you are in the cockpit.
2. In the X‑Plane menu, confirm the **XPlaneConnect** plugin is enabled:
   - `Plugins → Plugin Admin → Enabled` → **XPlaneConnect** must be checked.
3. Open a terminal and go to the project root:
   ```bash
   cd /path/to/Qt
   ./run_collision_test_full.sh
   ```
4. The script will:
   - Start **BlueSky** (using `Bluesky/venv`).
   - Start **bluesky_bridge.py** with `BS_DISABLE_AUTOSPAWN=1` (no extra test traffic).
   - Start the **Qt overlay** (`QT_EXEC`) if the path is valid.
5. Switch to the **BlueSky** window:
   - Load and run a test scenario, for example:
     ```text
     IC scenario/collision_G2_collision
     OP
     ```
   - The bridge mirrors traffic from this scenario into X‑Plane AI slots and to the Qt overlay.
6. To switch test geometries (G1–G4), run a different `IC` command in BlueSky:
   ```text
   IC scenario/collision_G1_no_collision
   OP
   ```

X‑Plane and the Qt overlay will automatically show the corresponding test scenario; **no need to restart the bridge or scripts** when switching scenarios.

---

## 3. Windows: End‑to‑end startup

### 3.1 One‑time configuration

1. Ensure the BlueSky virtualenv exists and works (section 1.2).
2. Build the Qt overlay `.exe` (section 1.3).
3. Edit `run_collision_test_full.bat` in the project root:
   - Set `QT_EXEC` to your Qt overlay executable, for example:
     ```bat
     set QT_EXEC=%SCRIPT_DIR%QtBlueSkyDemo.exe
     ```

### 3.2 Per‑session run sequence (from zero)

1. **Start X‑Plane 12 manually** and enter the cockpit.
2. Confirm the **XPlaneConnect** plugin is enabled:
   - `Plugins → Plugin Admin → Enabled` → **XPlaneConnect** checked.
3. In File Explorer, go to the project root (`Qt`) and double‑click:
   - `run_collision_test_full.bat`
4. The batch script will:
   - Start **BlueSky** in a new window using `Bluesky\venv\Scripts\python.exe`.
   - Start **bluesky_bridge.py** in another window with `BS_DISABLE_AUTOSPAWN=1`.
   - Start the **Qt overlay** (`QT_EXEC`) if the path exists.
5. In the **BlueSky** window:
   - Load and run a scenario, for example:
     ```text
     IC scenario/collision_G2_collision
     OP
     ```
6. To test other geometries, change the scenario name:
   ```text
   IC scenario/collision_G3_collision
   OP
   ```

The bridge keeps running and always mirrors the **current** BlueSky traffic into X‑Plane AI and the Qt overlay.

---

## 4. Test scenarios (G1–G4)

The four main test scenarios live in `Bluesky/scenario/`:

- `collision_G1_no_collision.scn`
- `collision_G2_collision.scn`
- `collision_G3_collision.scn`
- `collision_G4_no_collision.scn`

They are defined near a fixed reference point (around 48°N, 12°E) with:

- `OWN001`: ownship flying straight north.
- `TFC001`: intruder arranged to create either:
  - A **non‑collision** geometry (G1, G4), or
  - A **collision** geometry (G2, G3).

BlueSky is the authoritative source for these geometries; X‑Plane + Qt are visual mirrors used to evaluate HUD / alerting design under the same scenarios.

To load a scenario in BlueSky:

```text
IC scenario/collision_G2_collision
OP
```

To switch, simply issue another `IC scenario/...` followed by `OP`.

---

## 5. Verifying XPC plugin in a new X‑Plane 12 install

To ensure a fresh X‑Plane 12 install is correctly configured for this system:

1. Copy the **XPlaneConnect** plugin folder into:
   - `X-Plane 12/Resources/plugins/XPlaneConnect`
2. Start X‑Plane and open:
   - **Plugins → Plugin Admin → Enabled**  
   - Confirm **XPlaneConnect** is listed and checked.
3. (Optional) In X‑Plane’s log file (`X-Plane 12/Log.txt`), search for `XPlaneConnect`:
   - You should see a line indicating it was loaded successfully.
4. From a terminal (macOS) or command prompt (Windows), run the bridge alone to sanity‑check connectivity:
   - **macOS / Linux**:
     ```bash
     cd /path/to/Qt
     ./Bluesky/venv/bin/python bluesky_bridge.py
     ```
   - **Windows**:
     ```bat
     cd \path\to\Qt
     Bluesky\venv\Scripts\python.exe bluesky_bridge.py
     ```
   - In the console you should see `✓ XPC loaded` and no “cannot connect” errors.

If any of these checks fail, fix the plugin installation (path, permissions) before troubleshooting the rest of the system.

## Overview

This project connects **X‑Plane 12**, **BlueSky** (ATM simulator) and a **Qt overlay**:

- X‑Plane 12: renders the outside world and your ownship cockpit.
- BlueSky: runs traffic/scenario logic (including the 4 G1–G4 conflict geometries).
- Bridge (`bluesky_bridge.py`): mirrors BlueSky traffic to X‑Plane AI aircraft (via XPC / UDP) and to the Qt overlay.
- Qt overlay: draws HUD / alerting symbology on top of X‑Plane.

You can switch scenarios **only in BlueSky** using `IC scenario/...`, and X‑Plane + Qt will follow automatically.

---

## 1. Prerequisites

### 1.1 X‑Plane 12 + XPC plugin

1. Download the **XPlaneConnect** (XPC) plugin for your platform (Windows or macOS) from the official repository.
2. Unzip and copy the plugin folder to:
   - **Windows / macOS**:  
     `X-Plane 12/Resources/plugins/XPlaneConnect`
3. Start X‑Plane 12 and verify:
   - In the top menu, open **Plugins → Plugin Admin → Enabled** and confirm **XPlaneConnect** is listed and enabled.
   - (Optional) In **Plugins → XPlaneConnect** menu, check that it reports “connected” when the bridge is running.

If the plugin is missing or disabled, `xpc.XPlaneConnect()` in the bridge will fail and no traffic will appear in X‑Plane.

### 1.2 Python environment for BlueSky + bridge

- A Python 3 environment with BlueSky installed (this repo already includes a `Bluesky/venv` virtualenv).
- Qt overlay built as:
  - macOS `.app` bundle (e.g. `QtBlueSkyDemo.app`), or
  - Windows `.exe` (e.g. `QtBlueSkyDemo.exe`).

Update the paths in the launch scripts to point to your actual Qt executable.

---

## 2. macOS: How to start everything

### 2.1 One‑time setup

1. Ensure `Bluesky/venv` exists and has all Python dependencies installed.
2. Edit `run_collision_test_full.sh`:
   - Set `QT_EXEC` to your Qt overlay executable, for example:
     ```bash
     QT_EXEC="${SCRIPT_DIR}/QtBlueSkyDemo.app/Contents/MacOS/QtBlueSkyDemo"
     ```
   - Make sure it is executable:
     ```bash
     chmod +x QtBlueSkyDemo.app/Contents/MacOS/QtBlueSkyDemo
     ```

### 2.2 Run sequence (per session)

1. **Start X‑Plane 12 manually** and enter the cockpit.
2. From this project root (`Qt`), run:
   ```bash
   ./run_collision_test_full.sh
   ```
3. The script will:
   - Start **BlueSky** using `Bluesky/venv`.
   - Start **bluesky_bridge.py** with `BS_DISABLE_AUTOSPAWN=1` (no extra test traffic).
   - Start the **Qt overlay** (`QT_EXEC`) if the path is valid.
4. In the **BlueSky window**:
   - Load a scenario, for example:
     ```text
     IC scenario/collision_G2_collision
     OP
     ```
   - The bridge mirrors traffic from this scenario into X‑Plane AI slots and to the Qt overlay.
5. To switch test geometries (G1–G4), just run a different `IC` command in BlueSky:
   ```text
   IC scenario/collision_G1_no_collision
   OP
   ```

X‑Plane and the Qt overlay will automatically show the corresponding test scenario; no restart of the bridge or scripts is required.

---

## 3. Windows: How to start everything

### 3.1 One‑time setup

1. Ensure `Bluesky\venv` exists and has all Python dependencies installed.
2. Edit `run_collision_test_full.bat`:
   - Set `QT_EXEC` to your Qt overlay executable, for example:
     ```bat
     set QT_EXEC=%SCRIPT_DIR%QtBlueSkyDemo.exe
     ```

### 3.2 Run sequence (per session)

1. **Start X‑Plane 12 manually** and enter the cockpit.
2. From this project root (`Qt`), double‑click:
   - `run_collision_test_full.bat`
3. The batch script will:
   - Start **BlueSky** in a new window using `Bluesky\venv\Scripts\python.exe`.
   - Start **bluesky_bridge.py** in another window with `BS_DISABLE_AUTOSPAWN=1`.
   - Start the **Qt overlay** (`QT_EXEC`) if the path exists.
4. In the **BlueSky window**:
   - Load and run a scenario:
     ```text
     IC scenario/collision_G2_collision
     OP
     ```
5. To test other geometries, simply change the scenario name:
   ```text
   IC scenario/collision_G3_collision
   OP
   ```

The bridge keeps running and will always mirror the current BlueSky traffic into X‑Plane AI and the Qt overlay.

---

## 4. Test scenarios (G1–G4)

The four main test scenarios live in `Bluesky/scenario/`:

- `collision_G1_no_collision.scn`
- `collision_G2_collision.scn`
- `collision_G3_collision.scn`
- `collision_G4_no_collision.scn`

They are defined in BlueSky’s coordinates near a fixed reference point (around 48°N, 12°E) with:

- `OWN001`: ownship flying straight north.
- `TFC001`: intruder arranged to create either:
  - A **non‑collision** geometry (G1, G4), or
  - A **collision** geometry (G2, G3).

BlueSky is the authoritative source for these geometries; X‑Plane + Qt are visual mirrors used to evaluate HUD / alerting design under the same scenarios.

---

## 5. Verifying XPC plugin in a new X‑Plane 12 install

To ensure a fresh X‑Plane 12 install is correctly configured for this system:

1. Copy the **XPlaneConnect** plugin folder into:
   - `X-Plane 12/Resources/plugins/XPlaneConnect`
2. Start X‑Plane and open:
   - **Plugins → Plugin Admin → Enabled**  
   - Confirm **XPlaneConnect** is listed and checked.
3. (Optional) In X‑Plane’s log file (`X-Plane 12/Log.txt`), search for `XPlaneConnect`:
   - You should see a line indicating it was loaded successfully.
4. Run the bridge alone to sanity‑check connectivity:
   ```bash
   cd /path/to/Qt
   ./Bluesky/venv/bin/python bluesky_bridge.py
   ```
   - In the terminal you should see `✓ XPC loaded`.
   - No “cannot connect” errors should be printed.

If any of these checks fail, fix the plugin installation (path, permissions) before troubleshooting the rest of the system.

