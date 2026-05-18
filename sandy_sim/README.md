# Sandy buoy sensor sim — full bring-up guide

Target stack: **ROS 2 Jazzy + Gazebo Harmonic** on Ubuntu 24.04, with
MATLAB R2024a+ and ROS Toolbox for the Simulink side.

This is the **sensor-functionality** sim.The buoy is kinematic and its pose is driven by sliders. Two
coloured-sphere phantoms (one red, one blue) are visible to the buoy's
sensors so you can validate the detection pipeline end-to-end. A helper
node publishes synthetic pressure and temperature derived from the buoy's
commanded state, and a Simulink library lets you swap your constant
sensor blocks for live ROS 2 subscriptions.

## Folder layout

```
sandy_sim/
├── worlds/
│   └── antarctic_test.sdf            <- world: fog, ambient, phantoms, buoy
├── models/
│   └── sandy_buoy/
│       ├── model.config
│       └── model.sdf                 <- buoy: kinematic, with sensors
├── ros2_helper/
│   └── depth_publisher.py            <- pose+temp slider GUI; pressure +
│                                        temperature publishers
├── config/
│   └── sandy_bridge.yaml             <- ros_gz_bridge YAML
├── matlab/
│   ├── build_sandy_sensor_library.m  <- builds sandy_sensors.slx
│   ├── configure_sandy_model.m       <- fixes solver + Image limits
│   └── SIMULINK_INTEGRATION.md       <- per-block integration details
└── test_revised.m                    <- standalone MATLAB analyser (no Simulink)
```

## What gets published, by whom

| Topic                       | Type                        | Source                  | Rate    |
|-----------------------------|-----------------------------|-------------------------|---------|
| `/sandy/as7341_down`        | `sensor_msgs/Image` 8×8 RGB | Gazebo (bridged)        | 10 Hz   |
| `/sandy/photodiode_side`    | `sensor_msgs/Image` 4×4 RGB | Gazebo (bridged)        | 10 Hz   |
| `/sandy/pressure_raw`       | `sensor_msgs/FluidPressure` | Gazebo (bridged, unused)| 10 Hz   |
| `/sandy/pressure`           | `sensor_msgs/FluidPressure` | helper node (Python)    | 10 Hz   |
| `/sandy/temperature`        | `sensor_msgs/Temperature`   | helper node (Python)    | 10 Hz   |
| `/clock`                    | `rosgraph_msgs/Clock`       | Gazebo (bridged)        | sim-time|

The "real" pressure and temperature you wire into Simulink come from the
**helper node**, not Gazebo. The reasons are documented inline in
`depth_publisher.py` but in short: Gazebo's `air_pressure` sensor is
barometric (not water-pressure), there is no native temperature sensor,
and bridging pose for kinematic models is unreliable.

## Prerequisites

```bash
sudo apt install ros-jazzy-ros-gz ros-jazzy-ros-gz-bridge \
                 ros-jazzy-ros-gz-image ros-jazzy-ros-gz-sim
sudo apt install gz-harmonic
sudo apt install python3-tk
```

Verify:

```bash
gz sim --versions       # should show Harmonic
ros2 pkg list | grep ros_gz
```

For Simulink integration: **MATLAB R2024a or later** with **ROS Toolbox**
installed. Verify in MATLAB:

```matlab
ver -support           % ROS Toolbox should appear
ros2 node list         % should run without error
```

---

## Step 1 — Run Gazebo alone, no ROS yet

This is the single most important step. **Do not** wire in ROS or MATLAB
until this works.

```bash
cd /path/to/sandy_sim
export GZ_SIM_RESOURCE_PATH=$GZ_SIM_RESOURCE_PATH:$PWD/models
gz sim -r worlds/antarctic_test.sdf
```

In the GUI you should see:

- A dark, blue-tinted scene (the Antarctic ambient).
- The orange cylindrical buoy at z = -50 m.
- Two glowing spheres just below it (red at +X, blue at +Y).
- Fog that thickens with distance.

In a second terminal, confirm the sensor topics are alive:

```bash
gz topic -l | grep sandy
# Expected:
#   /sandy/as7341_down
#   /sandy/photodiode_side
#   /sandy/pressure_raw

gz topic -e -t /sandy/as7341_down -n 1   # should print one Image message
```

**If the AS7341 image is all black**: the downward camera is looking
into empty water. Verify in the GUI that the camera frustum (enabled
with `<visualize>true</visualize>`) points at the phantoms. If not, flip
the pitch sign in `models/sandy_buoy/model.sdf`: `0 1.5708 0` ↔
`0 -1.5708 0`.

**If you get a plugin-not-found error**: the gz-sim binary can't see the
plugins. Try `sudo apt install libgz-sim8-dev` (Harmonic's libgz-sim is
v8). Confirm `gz sim --versions` shows Harmonic and not a stray Garden
install.

---

## Step 2 — Bridge to ROS 2

In a new terminal, source ROS 2 and run the bridge:

```bash
source /opt/ros/jazzy/setup.bash
cd /path/to/sandy_sim
ros2 launch ros_gz_bridge ros_gz_bridge.launch.py \
    bridge_name:=sandy_bridge \
    config_file:=$PWD/config/sandy_bridge.yaml
```

Verify:

```bash
ros2 topic list | grep sandy
ros2 topic hz /sandy/as7341_down   # should report ~10 Hz
ros2 topic echo /sandy/as7341_down --once
```

`/sandy/pressure` and `/sandy/temperature` won't appear yet — those come
from the helper node in Step 3, not the bridge.

---

## Step 3 — Run the helper node (sliders + pressure + temperature)

In a new terminal:

```bash
source /opt/ros/jazzy/setup.bash
cd /path/to/sandy_sim
python3 ros2_helper/depth_publisher.py
```

A Tk window opens with three sections:

- **Buoy pose**: X, Y, Z sliders. Moving any of them calls Gazebo's
  `set_pose` service to teleport the buoy.
- **Water temperature**:
  - Checkbox toggles between "use Antarctic depth profile" (slider is
    an offset added to a piecewise-linear T(z) curve) and "manual"
    (slider is absolute T).
  - Slider sets the offset or absolute value.
- **Status panel**: live X/Y/Z, depth, pressure (kPa), temperature (°C).

In another terminal, confirm streams are flowing:

```bash
ros2 topic hz /sandy/pressure       # ~10 Hz
ros2 topic hz /sandy/temperature    # ~10 Hz
ros2 topic echo /sandy/pressure --once
ros2 topic echo /sandy/temperature --once
```

Sanity check — at default pose (z = -50 m, depth profile on, offset 0):

- Pressure: ~605 kPa (`101325 + 1028 × 9.80665 × 50`)
- Temperature: about -0.7°C (interpolated from the depth profile)

Move the Z slider. Pressure scales linearly with depth. Move the
temperature slider in profile mode → temperature shifts by the offset.
Untick the profile checkbox → temperature jumps to the absolute value
shown on the slider.

---

## Step 4 — (Optional) Standalone MATLAB analyser

Quick smoke test before you commit to Simulink. Pure-MATLAB script that
subscribes to the same topics and prints rolling sensor readings.

In MATLAB:

```matlab
cd /path/to/sandy_sim
test_revised
```

Expected output:

```
Depth:  50.21 m | R:  12.30  G:   2.10  B:   2.40 | Side:   3.10
--- PHYTOPLANKTON DETECTED (red > 1.5 * blue) ---
```

Move the X slider so the buoy passes over the red sphere → red channel
spikes. Slide it toward the blue sphere → blue channel spikes. If this
works, your full data path (Gazebo → bridge → MATLAB) is good.

If it doesn't print anything: check `ros2 topic hz /sandy/as7341_down`
shows ~10 Hz in a terminal. If yes, the issue is MATLAB-side — typically
ROS Domain ID mismatch. Run `getenv('ROS_DOMAIN_ID')` in MATLAB and
compare to the value in your shell.

---

## Step 5 — Simulink integration

Goal: replace the four constant blocks in your existing Simulink model
(`Gazebo_Light_Intensity`, AS7341 constant, MS5837 constant, DS18B20
constant) with live ROS 2 subscriptions.

### Step 5a — Build the sensor library

In MATLAB:

```matlab
cd /path/to/sandy_sim/matlab
build_sandy_sensor_library
```

This creates `sandy_sensors.slx` in the current folder containing four
library blocks:

| Block                  | Replaces                       | Output                       |
|------------------------|--------------------------------|------------------------------|
| `BPW34_Photodiode`     | `Gazebo_Light_Intensity = 5e-7`| scalar in `[0, 1]`           |
| `AS7341_8Channel`      | AS7341 `170` constant          | `uint16(1×8)` channels F1..F8|
| `MS5837_Pressure`      | MS5837 constant                | `Pressure_Pa`, `Depth_m`     |
| `DS18B20_Temperature`  | DS18B20 constant               | scalar `T (°C)`              |

Open `sandy_sensors.slx` to verify all four blocks built. Each has
mouse-over help describing its I/O.

### Step 5b — Wire blocks into your model

1. Open your existing Simulink model (the schematic with the analog
   photodiode + ESP32 + I²C sensors).
2. With `sandy_sensors.slx` also open, **drag each library block in**
   to replace its constant counterpart.
3. **BPW34 → analog circuit**: the `BPW34_Photodiode` block outputs a
   scalar in `[0, 1]`. Wire it through a `Gain` block with value `5e-7`
   (your existing TIA photocurrent scale) into where the `5e-7` constant
   was going.
4. **AS7341 → digital subsystem**: outputs `uint16(1×8)` matching the
   real sensor's data format. If your AS7341 subsystem expects a scalar,
   insert a `Selector` block to pick one channel — but ideally update
   the subsystem to handle all 8.
5. **MS5837**: two outputs (`Pressure_Pa`, `Depth_m`). Use whichever
   your I²C emulator expects.
6. **DS18B20**: single scalar °C output.

### Step 5c — Configure solver and message limits

This is where the previous "10s per 1.6s" Simulink problem gets fixed.
After dragging blocks in:

```matlab
configure_sandy_model('YourModelName')
```

This sets:
- Solver: Fixed-step discrete at 0.1 s.
- `sensor_msgs/Image` data limit: 512 bytes (covers 8×8 RGB + margin).

**Do not skip this.** Without the variable-size limit, ROS Subscribe
blocks silently truncate image data and you'll get all-zero pixels.
Without the fixed-step solver, every ROS message arrival triggers tiny
adaptive solver steps and RTF craters.

### Step 5d — Run

With Gazebo, bridge, and helper node all running, hit Run in Simulink.
You should see live sensor data flowing into your existing electronics
emulation. Move the sliders in the helper GUI and watch your Simulink
scopes/displays update.

See `matlab/SIMULINK_INTEGRATION.md` for per-block detail and the AS7341
8-channel synthesis weight matrix.

---

## Typical bring-up checklist

Five terminals, in order:

| Terminal | Command                                                                                          |
|----------|--------------------------------------------------------------------------------------------------|
| 1        | `gz sim -r worlds/antarctic_test.sdf` (Step 1)                                                   |
| 2        | `ros2 launch ros_gz_bridge ros_gz_bridge.launch.py bridge_name:=sandy_bridge config_file:=...`   |
| 3        | `python3 ros2_helper/depth_publisher.py`                                                         |
| 4        | (verification) `ros2 topic hz /sandy/pressure` etc.                                              |
| 5        | MATLAB / Simulink                                                                                |

Stop in reverse order. Anything running on stale topics will print
warnings rather than die.

---

## Troubleshooting

### Step 1: Gazebo issues

**Cameras render but pixel values are all (0,0,0)** — the `<emissive>`
material is what makes the spheres self-luminous in dark ambient. If you
removed it, add it back. Alternatively, increase the ambient component
of the scene.

**RTF < 0.5 in Gazebo** — check the rendering engine:
`<render_engine>ogre2</render_engine>` is required. Ogre1 fallback will
hurt. On integrated graphics, expect ~0.6 RTF; that's fine for sensor
testing at 10 Hz.

**Spheres won't move with `gz service set_pose`** — the phantoms are
`<static>true</static>`. The `set_pose` service still works on static
models in Harmonic, but if you have an older sub-version installed it
may not. Remove `<static>true</static>` from the phantom models if so;
they'll still hover (no gravity acts on them).

### Step 2: Bridge issues

**No topics appear in `ros2 topic list`** — verify the bridge is
running: `ros2 node list | grep bridge`. Check the YAML path in the
launch command is correct.

**Bridge starts but topics show 0 Hz on the ROS side** — confirm the
Gazebo side is actually publishing: `gz topic -e -t /sandy/as7341_down -n 1`.
If Gazebo isn't publishing, you have a Gazebo problem, not a bridge
problem. Go back to Step 1.

### Step 3: Helper node issues

**`/sandy/pressure` and `/sandy/temperature` don't appear** — make sure
ROS 2 is sourced in the helper's terminal (`source /opt/ros/jazzy/setup.bash`).

**`gz service` calls fail with "service not available"** — the helper's
terminal needs access to gz-transport, same as Gazebo's terminal.
Re-source whatever environment gives you `gz` on PATH.

**Slider GUI doesn't open** — Tk isn't installed:
`sudo apt install python3-tk`. The node will still run headless,
publishing at the default pose.

**Pressure stays at ~101 kPa regardless of Z slider** — you've likely
got an old `depth_publisher.py` that subscribed to a Gazebo pose topic.
The current version tracks state internally. Re-copy the latest version.

### Step 5: Simulink issues

**`build_sandy_sensor_library` errors with "Could not find MATLAB
Function block"** — Stateflow chart creation is asynchronous in some
MATLAB versions. Edit the script and add `pause(0.5);` after each
`add_block(..., 'MATLAB Function', ...)` call.

**Subscribe block stays grey / no data** — check `ros2 topic list` shows
the topic AND `ros2 topic hz /sandy/...` shows >0 Hz. If yes, check the
model's ROS 2 Domain ID matches your system's (`echo $ROS_DOMAIN_ID`).

**Image data is all zeros** — the variable-size limit isn't set. Re-run
`configure_sandy_model`, or set it manually: Simulation tab → ROS
Toolbox → Variable-Size Messages → `sensor_msgs/Image` → untick "use
defaults" → set `data` length to 512.

**MS5837 / DS18B20 Bus Selector errors** — open the subsystem,
double-click the Bus Selector, verify `fluid_pressure` (or `temperature`)
appears in the right-hand list. If not: Modeling tab → Update Model
first, then reopen the selector.

**Simulink RTF < 1** — see next section.

### MATLAB sim itself is slow (the 10s-per-1.6s problem)

This is **separate** from Gazebo and almost always a Simulink solver
issue. `configure_sandy_model` fixes the big two (fixed-step + Image
limits) but if you're still slow:

1. **Find continuous-time blocks**. Anything with `s` in its transfer
   function, an Integrator, or a continuous state-space block forces
   continuous solving despite the fixed-step setting. The `f(x) = 0`
   algebraic-constraint block in your original schematic is the prime
   suspect — delete it if it's not doing real work.
2. **Replace Scope blocks with `To Workspace`**. Scopes serialise samples
   and force redraws on every update; on a fast-publishing topic that's
   surprisingly expensive.
3. **Set explicit sample times**. Inside each sensor subsystem (AS7341,
   MS5837, DS18B20), set block sample time to `-1` (inherited) or
   explicitly `0.1`. If any block is left at sample time `0`
   (continuous), it drags the whole model back to continuous solving.
4. **ESP32 / sim_time path**: make sure it has a defined sample time, not
   continuous. If it's polling on solver steps, that alone can dominate.
5. **Profile**: `set_param('YourModelName', 'TraceMode', 'PathProfiling')`
   then run; the profile report shows which block is eating cycles.

Get Simulink to RTF ~1 standalone before worrying about Gazebo
synchronisation.

---

## What's deliberately missing

- **No buoyancy plugin.** Re-add only when you switch from sensor
  testing to dynamics testing.
- **No DAVE.** Nothing about your sensor pipeline needs it.
- **No 1 W blue downward LED on the buoy.** The original SDF had this
  lighting the DCM from above. For sensor testing against self-luminous
  phantoms, it just adds noise. Re-add only if you want to validate
  that the LED illuminates the target correctly.
- **No physically-accurate AS7341 spectral rendering.** Gazebo cameras
  are broadband RGB; the 8 AS7341 channels are synthesised in Simulink
  from a fixed weight matrix. Good enough for pipeline testing, not for
  spectral retrieval algorithm validation.

## What's intentionally crude

- **Spheres as phantoms.** Real chlorophyll fluorescence is wavelength-
  shifted and concentration-dependent. The sphere is a "this is where
  signal should be" marker, not a physical model of phytoplankton
  optics. Appropriate for sensor-functionality testing, inappropriate
  for retrieval-algorithm validation. When you reach the latter,
  replace with a custom plugin that modulates colour with depth and
  concentration.

- **Temperature profile.** The piecewise-linear T(z) is based on SOCCOM
  climatology averages and ignores seasonality, frontal position, and
  mesoscale variability. Fine for verifying the DS18B20 emulator reads
  the right register values; not fine for validating thermohaline
  retrieval logic.

- **AS7341 8-channel weight matrix.** The 8×3 RGB-to-F1..F8 mapping
  preserves spectral *trend* (red object → high F7/F8, low F1/F2) but
  not absolute spectral truth. Tune the weights if your detection
  thresholds need it.
