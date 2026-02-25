# Qt + X-Plane + BlueSky Overlay System

A transparent Qt HUD overlay for macOS that displays BlueSky traffic aircraft on top of X-Plane, enabling real-time air traffic visualization during flight simulation.

## Overview

This system creates a three-way communication architecture:
- **X-Plane** → Sends ownship position data via UDP
- **Qt Overlay** → Receives X-Plane data, forwards to BlueSky, displays traffic
- **BlueSky** → Generates AI traffic aircraft, sends back to Qt for display

## Quick Start

### Prerequisites

- **X-Plane** installed and configured for UDP data output
- **BlueSky** simulator installed
- **Qt 5.15+** (macOS)
- **Python 3** with virtual environment

### Installation

1. **Clone/Navigate to project directory**
   ```bash
   cd /Users/spaceodyssey/Desktop/Studyy/Thesis/Qt
   ```

2. **Set up Python virtual environment** (if not already done)
   ```bash
   python3 -m venv venv
   source venv/bin/activate
   pip install numpy bluesky-simulator
   ```

3. **Build Qt application**
   ```bash
   qmake QtBlueSkyDemo.pro
   make
   ```

### Startup Sequence

**Important**: Start components in this order:

1. **Start BlueSky**
   ```bash
   # In BlueSky directory
   python BlueSky.py
   ```
   Ensure BlueSky simulation is running (not paused).

2. **Start Bridge** (in project directory)
   ```bash
   ./run_bridge.sh --qt-port 49004 --command-port 49003
   ```
   Wait for: `✓ Connected to BlueSky` and `✓ Command socket bound to port 49003`

3. **Configure X-Plane**
   - Go to **Settings → Data Output → UDP**
   - Add new output:
     - IP: `127.0.0.1`
     - Port: `49002` (or port configured in Qt app)
     - Enable indices: **17, 18, 19, 20, 21, 22, 3, 4**
   - Start a flight (preferably airborne)

4. **Start Qt Overlay**
   ```bash
   ./QtBlueSkyDemo.app/Contents/MacOS/QtBlueSkyDemo
   ```
   The overlay should appear full-screen on top of X-Plane.

5. **Start Scenario Creator** (optional, for automatic traffic generation)
   ```bash
   ./run_scenario_creator.sh
   ```
   This will automatically create two traffic aircraft near the ownship.

### Verification

- **Qt Overlay**:
  - Top-left should show: `XP: OK` (X-Plane connected)
  - Top-left should show: `BS: OK` (BlueSky connected)
  - Red squares should appear for traffic aircraft
  - Crosshair lines at screen center

- **Bridge Output**:
  - `[OWN] Received ownship position: ...`
  - `[OWN] Ownship created` or `[OWN] Updating ownship position`
  - `✓ Sending X aircraft in Y batch(es) to Qt`

- **Scenario Creator Output**:
  - `[SCENARIO] Detected ownship in BlueSky: OWN001`
  - `✓ Scenario created successfully!`

## Port Configuration

| Component | Port | Direction | Purpose |
|-----------|------|-----------|---------|
| X-Plane | 49002 | → Qt | Ownship data output |
| Qt | 49003 | → Bridge | Ownship position forwarding |
| Bridge | 49004 | → Qt | Traffic data forwarding |
| Bridge | 11000/11001 | ↔ BlueSky | BlueSky communication |

## Troubleshooting

### "XP: --" (X-Plane not connected)
- Check X-Plane Data Output settings
- Verify port matches Qt configuration (default: 49002)
- Ensure X-Plane is sending data (not listening)

### "BS: --" (BlueSky not connected)
- Ensure BlueSky is running
- Check Bridge is running and connected
- Verify Bridge output shows `✓ Connected to BlueSky`

### No traffic aircraft displayed
- Check Bridge output for traffic data
- Ensure Scenario Creator has run (if using automatic scenario)
- Verify BlueSky has aircraft in simulation

### Overlay not visible
- Check `Info.plist` has `LSUIElement = true`
- Verify window is not minimized
- Try clicking on X-Plane window to bring it forward

### Keyboard/Mouse not working in X-Plane
- This is expected - overlay is transparent to input
- Use Quit button (bottom-left) to exit overlay
- If input is blocked, check `MainWindow.cpp` event filter settings

## Exit

Click the **Quit** button in the bottom-left corner of the overlay, or use `Ctrl+C` in terminal if running from command line.

## Project Structure

- **C++ Files**: Qt application (MainWindow, XPlaneReceiver, BlueSkyCommunicator, etc.)
- **Python Files**: Bridge (`bluesky_bridge.py`) and Scenario Creator (`bluesky_scenario_creator.py`)
- **Shell Scripts**: Convenience wrappers for running Python components

For detailed file descriptions and architecture, see `PROJECT_OVERVIEW_EN.md`.

