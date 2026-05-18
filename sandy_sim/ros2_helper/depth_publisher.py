#!/usr/bin/env python3
"""
Sandy buoy sim helper node.

Owns the buoy's commanded pose AND the simulated water temperature as
internal state. Publishes:
  /sandy/pressure     (sensor_msgs/FluidPressure) at 10 Hz
  /sandy/temperature  (sensor_msgs/Temperature)   at 10 Hz

Why everything is internal: bridging Gazebo's per-model pose into ROS
through ros_gz_bridge is unreliable for static models (gazebosim/ros_gz#796).
Since the slider GUI is the only thing that moves the buoy, treating the
slider state as ground truth is correct and simpler.

Temperature model:
  - Default "depth profile" mode: Antarctic Southern Ocean T(z) curve.
    Slider becomes a delta offset (-5 to +5 C).
  - Manual mode: slider is the absolute temperature (-2 to +30 C).

Run:
    python3 depth_publisher.py
"""

import subprocess
import sys
import threading

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy
from sensor_msgs.msg import FluidPressure, Temperature

try:
    import tkinter as tk
    HAS_TK = True
except ImportError:
    HAS_TK = False

# -- Physical constants -----------------------------------------------------
RHO_SEAWATER = 1028.0
G            = 9.80665
P_ATM        = 101325.0

# -- Gazebo target ----------------------------------------------------------
WORLD_NAME = 'antarctic_ocean'
MODEL_NAME = 'sandy_buoy'

# -- Defaults ---------------------------------------------------------------
INIT_X = 0.0
INIT_Y = 0.0
INIT_Z = -50.0
INIT_T_OFFSET = 0.0
INIT_T_ABS    = -0.5


def antarctic_temp_profile(z: float) -> float:
    """
    Crude Antarctic Southern Ocean T(z) curve, piecewise linear:
       z =    0 m -> -1.0 C
       z =  -80 m -> -0.5 C
       z = -200 m -> +1.0 C
       z = -500 m -> +2.0 C
       z < -500 m -> +2.0 C (clamp)

    Based on SOCCOM Argo float climatology for the Polar Frontal Zone.
    """
    depth = -z
    if depth <= 0:
        return -1.0
    if depth <= 80:
        return -1.0 + (depth / 80.0) * 0.5
    if depth <= 200:
        return -0.5 + ((depth - 80) / 120.0) * 1.5
    if depth <= 500:
        return 1.0 + ((depth - 200) / 300.0) * 1.0
    return 2.0


class SandyHelper(Node):
    def __init__(self):
        super().__init__('sandy_helper')

        qos = QoSProfile(
            reliability=ReliabilityPolicy.BEST_EFFORT,
            history=HistoryPolicy.KEEP_LAST,
            depth=10,
        )
        self._pressure_pub = self.create_publisher(
            FluidPressure, '/sandy/pressure', qos,
        )
        self._temperature_pub = self.create_publisher(
            Temperature, '/sandy/temperature', qos,
        )

        self._lock = threading.Lock()
        self._x, self._y, self._z = INIT_X, INIT_Y, INIT_Z
        self._use_profile = True
        self._t_slider = INIT_T_OFFSET

        self._timer = self.create_timer(0.1, self._publish_all)

        self.get_logger().info(
            'Sandy helper up. Publishing /sandy/pressure and '
            f'/sandy/temperature at 10 Hz (z={INIT_Z} m).'
        )

    def set_pose(self, x: float, y: float, z: float) -> bool:
        req = f'name: "{MODEL_NAME}", position: {{x: {x}, y: {y}, z: {z}}}'
        try:
            result = subprocess.run(
                ['gz', 'service',
                 '-s', f'/world/{WORLD_NAME}/set_pose',
                 '--reqtype', 'gz.msgs.Pose',
                 '--reptype', 'gz.msgs.Boolean',
                 '--timeout', '500',
                 '--req', req],
                capture_output=True, text=True, timeout=2.0,
            )
            if result.returncode != 0:
                self.get_logger().warn(
                    f'gz service failed: {result.stderr.strip()[:120]}'
                )
                return False
        except Exception as exc:
            self.get_logger().warn(f'gz service exception: {exc}')
            return False
        with self._lock:
            self._x, self._y, self._z = x, y, z
        return True

    def set_temperature_mode(self, use_profile: bool):
        with self._lock:
            self._use_profile = use_profile

    def set_temperature_value(self, value: float):
        with self._lock:
            self._t_slider = value

    def current_temperature(self) -> float:
        with self._lock:
            if self._use_profile:
                return antarctic_temp_profile(self._z) + self._t_slider
            return self._t_slider

    def _publish_all(self):
        with self._lock:
            z = self._z
            use_profile = self._use_profile
            t_slider = self._t_slider

        depth = max(0.0, -z)
        pressure_pa = P_ATM + RHO_SEAWATER * G * depth

        p_msg = FluidPressure()
        p_msg.header.stamp = self.get_clock().now().to_msg()
        p_msg.header.frame_id = 'sandy_buoy'
        p_msg.fluid_pressure = pressure_pa
        p_msg.variance = 100.0
        self._pressure_pub.publish(p_msg)

        if use_profile:
            t_celsius = antarctic_temp_profile(z) + t_slider
        else:
            t_celsius = t_slider

        t_msg = Temperature()
        t_msg.header.stamp = p_msg.header.stamp
        t_msg.header.frame_id = 'sandy_buoy'
        t_msg.temperature = t_celsius
        t_msg.variance = 0.0625
        self._temperature_pub.publish(t_msg)


class SliderGUI:
    def __init__(self, node: SandyHelper):
        self._node = node

        self._root = tk.Tk()
        self._root.title('Sandy buoy control')
        self._root.geometry('480x420')

        pose_frame = tk.LabelFrame(self._root, text='Buoy pose', padx=8, pady=4)
        pose_frame.pack(fill='x', padx=10, pady=4)

        self._x = tk.DoubleVar(value=INIT_X)
        self._y = tk.DoubleVar(value=INIT_Y)
        self._z = tk.DoubleVar(value=INIT_Z)

        for label, var, lo, hi in [
            ('X (m)', self._x, -10.0, 10.0),
            ('Y (m)', self._y, -10.0, 10.0),
            ('Z (m, negative = down)', self._z, -100.0, 0.0),
        ]:
            row = tk.Frame(pose_frame)
            row.pack(fill='x', pady=2)
            tk.Label(row, text=label, width=22, anchor='w').pack(side='left')
            tk.Scale(
                row, variable=var, from_=lo, to=hi,
                resolution=0.1, orient='horizontal', length=220,
                command=lambda _e=None: self._send_pose(),
            ).pack(side='left', fill='x', expand=True)

        temp_frame = tk.LabelFrame(
            self._root, text='Water temperature (DS18B20 source)',
            padx=8, pady=4,
        )
        temp_frame.pack(fill='x', padx=10, pady=4)

        self._t_mode = tk.BooleanVar(value=True)
        self._t_value = tk.DoubleVar(value=INIT_T_OFFSET)

        mode_row = tk.Frame(temp_frame)
        mode_row.pack(fill='x', pady=2)
        tk.Checkbutton(
            mode_row, text='Use Antarctic depth profile (slider = offset)',
            variable=self._t_mode,
            command=self._on_mode_change,
        ).pack(side='left')

        slider_row = tk.Frame(temp_frame)
        slider_row.pack(fill='x', pady=2)
        self._t_label = tk.Label(
            slider_row, text='Offset (C)', width=22, anchor='w',
        )
        self._t_label.pack(side='left')
        self._t_scale = tk.Scale(
            slider_row, variable=self._t_value, from_=-5.0, to=5.0,
            resolution=0.1, orient='horizontal', length=220,
            command=lambda _e=None: self._send_temp(),
        )
        self._t_scale.pack(side='left', fill='x', expand=True)

        self._status = tk.Label(
            self._root, text='Ready.', fg='gray30', justify='left',
        )
        self._status.pack(pady=8, fill='x', padx=10)

        btn_row = tk.Frame(self._root)
        btn_row.pack(pady=4)
        tk.Button(btn_row, text='Reset pose', command=self._reset_pose).pack(
            side='left', padx=4)
        tk.Button(btn_row, text='Reset temp', command=self._reset_temp).pack(
            side='left', padx=4)

        self._refresh_status()
        self._root.after(500, self._send_pose)
        self._root.after(600, self._send_temp)

    def _on_mode_change(self):
        use_profile = self._t_mode.get()
        self._node.set_temperature_mode(use_profile)
        if use_profile:
            self._t_label.config(text='Offset (C)')
            self._t_scale.config(from_=-5.0, to=5.0)
            self._t_value.set(INIT_T_OFFSET)
        else:
            self._t_label.config(text='Absolute (C)')
            self._t_scale.config(from_=-2.0, to=30.0)
            self._t_value.set(INIT_T_ABS)
        self._send_temp()

    def _reset_pose(self):
        self._x.set(INIT_X); self._y.set(INIT_Y); self._z.set(INIT_Z)
        self._send_pose()

    def _reset_temp(self):
        if self._t_mode.get():
            self._t_value.set(INIT_T_OFFSET)
        else:
            self._t_value.set(INIT_T_ABS)
        self._send_temp()

    def _send_pose(self):
        x, y, z = self._x.get(), self._y.get(), self._z.get()
        self._node.set_pose(x, y, z)

    def _send_temp(self):
        self._node.set_temperature_value(self._t_value.get())

    def _refresh_status(self):
        x, y, z = self._x.get(), self._y.get(), self._z.get()
        depth = max(0.0, -z)
        p_kpa = (P_ATM + RHO_SEAWATER * G * depth) / 1000.0
        t = self._node.current_temperature()
        self._status.config(
            text=(f'Pose: x={x:.2f}  y={y:.2f}  z={z:.2f}\n'
                  f'Depth: {depth:.2f} m   Pressure: {p_kpa:.1f} kPa\n'
                  f'Temperature: {t:.2f} C'),
        )
        self._root.after(200, self._refresh_status)

    def run(self):
        self._root.mainloop()


def main(argv=None):
    rclpy.init(args=argv)
    node = SandyHelper()

    spin_thread = threading.Thread(
        target=rclpy.spin, args=(node,), daemon=True,
    )
    spin_thread.start()

    if HAS_TK and '--no-gui' not in (argv or sys.argv):
        gui = SliderGUI(node)
        try:
            gui.run()
        except KeyboardInterrupt:
            pass
    else:
        if not HAS_TK:
            node.get_logger().warn('tkinter not available — headless mode.')
        try:
            spin_thread.join()
        except KeyboardInterrupt:
            pass

    node.destroy_node()
    rclpy.shutdown()


if __name__ == '__main__':
    main()
