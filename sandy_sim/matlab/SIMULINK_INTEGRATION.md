# Simulink integration guide

## What this gives you

A drop-in library of four sensor subsystem blocks. Each one subscribes to
the right ROS 2 topic from your Sandy Gazebo sim and outputs ready-to-use
signals matching the physical sensors:

| Block                  | Replaces        | Topic                       | Output                                   |
|------------------------|-----------------|-----------------------------|------------------------------------------|
| `BPW34_Photodiode`     | `5e-7` constant | `/sandy/photodiode_side`    | scalar in `[0, 1]` (multiply by `5e-7`)  |
| `AS7341_8Channel`      | `170` constant  | `/sandy/as7341_down`        | `uint16(1x8)`, channels F1..F8           |
| `MS5837_Pressure`      | constant        | `/sandy/pressure`           | `[Pa, Depth_m]`                          |
| `DS18B20_Temperature`  | constant        | `/sandy/temperature`        | scalar `T (°C)`                          |

## One-time setup

In MATLAB:

```matlab
cd /path/to/sandy_sim/matlab
build_sandy_sensor_library
```

This creates `sandy_sensors.slx` in the current folder. Open it; you'll
see four library blocks.

## Per-model setup

1. Open your existing Simulink model (the one with the schematic in your
   first screenshot).
2. Open `sandy_sensors.slx` alongside it.
3. **Replace your `Gazebo_Light_Intensity` constant**: drag in
   `BPW34_Photodiode`. Wire its single output through a Gain block with
   value `5e-7` (this is your current TIA photocurrent assumption),
   then into where the `5e-7` constant was going.
4. **Replace the AS7341's `170` constant**: drag in `AS7341_8Channel`.
   Its output is a `uint16(1x8)` vector matching the real sensor's data
   format. If your AS7341 subsystem currently expects a scalar, you'll
   need a Selector block to pick one channel — but ideally update the
   subsystem to handle all 8.
5. **Replace the MS5837 input**: drag in `MS5837_Pressure`. It has two
   outputs (`Pressure_Pa`, `Depth_m`) — use whichever your subsystem
   uses internally.
6. **Replace the DS18B20 input**: drag in `DS18B20_Temperature`. Single
   scalar °C output.

After dragging blocks, run:

```matlab
configure_sandy_model('YourModelName')
```

This sets the solver to fixed-step discrete at 0.1 s and increases the
variable-size message limits for `sensor_msgs/Image`. **Do this before
hitting Run** or the Subscribe blocks will silently truncate image data.

## Test order

1. Confirm Gazebo + bridge + helper node are running, with topics
   publishing (use `ros2 topic hz /sandy/pressure` etc.).
2. In MATLAB, run the model. The ROS 2 Subscribe blocks will connect
   automatically using the default DDS settings.
3. Move the sliders in the helper GUI. You should see:
   - `MS5837` depth output changes when you move the Z slider.
   - `DS18B20` temperature changes when you move the temperature slider.
   - `BPW34` and `AS7341` outputs change when you move the buoy over the
     red/blue phantoms (X or Y sliders).

## Real-time performance

If RTF is still poor after `configure_sandy_model`:

- **Check for continuous-time blocks**. Anything with `s` in its transfer
  function or an Integrator block forces continuous solving despite the
  fixed-step setting. The `f(x) = 0` algebraic-constraint block in your
  schematic is the prime suspect — delete it if it's not doing work.
- **Replace Scope blocks with `To Workspace`**. Scopes serialise samples
  and force redraws; on a fast-publishing topic that's expensive.
- **Set ESP32 / sensor-emulator block sample times explicitly**. If any
  internal block still has Sample Time `-1` and its parent is continuous,
  you've smuggled continuous-time back in.
- **Profile**. In MATLAB: `set_param(model, 'TraceMode', 'PathProfiling')`
  then run; the profile report shows which block is eating cycles.

## How the AS7341 8-channel synthesis works

The block contains an 8x3 weight matrix `W` that maps `[R; G; B]` (the
spatial average of the Gazebo camera output) to 8 channels:

```
F1 (415 nm)  =  0.00*R + 0.05*G + 0.95*B   (deep violet, mostly blue)
F2 (445 nm)  =  0.00*R + 0.10*G + 0.90*B
F3 (480 nm)  =  0.00*R + 0.25*G + 0.75*B
F4 (515 nm)  =  0.00*R + 0.50*G + 0.50*B   (cyan transition)
F5 (555 nm)  =  0.10*R + 0.80*G + 0.10*B
F6 (590 nm)  =  0.30*R + 0.65*G + 0.05*B
F7 (630 nm)  =  0.75*R + 0.25*G + 0.00*B
F8 (680 nm)  =  0.95*R + 0.05*G + 0.00*B
```

The output is scaled to `uint16` (0..65535) to match the real AS7341's
ADC. This is good enough for:
- testing your AS7341 I²C driver / register map
- testing red/blue detection logic
- pipeline integration

It is **not** good enough for:
- validating spectral retrieval algorithms (e.g. chlorophyll-a from
  band ratios)
- characterising real sensor noise

For those, you'd need a custom Gazebo plugin that renders 8 separate
narrowband images with proper spectral response convolution. Out of
scope for the current project phase.

## Troubleshooting

**Subscribe block stays grey / no data**: check `ros2 topic list` shows
the topic, and `ros2 topic hz /sandy/...` shows >0 Hz. If yes, check
your model's ROS 2 Domain ID matches your system's (`echo $ROS_DOMAIN_ID`).

**Image data is all zeros**: the variable-size limit isn't set. Re-run
`configure_sandy_model` or set it manually under Simulation > ROS Toolbox >
Variable-Size Messages.

**Pressure/temperature stuck at zero**: the Bus Selector block in the
MS5837 / DS18B20 subsystem may not have populated its signal list yet.
Open the subsystem, double-click the Bus Selector, and verify
`fluid_pressure` (or `temperature`) appears in the right-hand list. If
not, do Modeling tab > Update Model first, then reopen the selector.

**Model runs at RTF < 0.5**: see the "Real-time performance" section
above. The solver setting matters more than anything else.
