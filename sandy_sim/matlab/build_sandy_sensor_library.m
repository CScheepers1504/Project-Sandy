function build_sandy_sensor_library()
% BUILD_SANDY_SENSOR_LIBRARY
%   Programmatically constructs a Simulink library 'sandy_sensors.slx' with
%   four ready-to-use sensor subsystem blocks that subscribe to the
%   ROS 2 topics published by the Sandy Gazebo sim:
%
%     [BPW34_Photodiode]    /sandy/photodiode_side   -> scalar 0..1 (analog)
%     [AS7341_8Channel]     /sandy/as7341_down       -> 8x1 vector (F1..F8)
%     [MS5837_Pressure]     /sandy/pressure          -> [Pa, Depth_m]
%     [DS18B20_Temperature] /sandy/temperature       -> Temp (C)
%
%   Usage:
%     1. From MATLAB, with the model NOT open, run:
%          build_sandy_sensor_library
%     2. Open the generated sandy_sensors.slx. You'll see four library
%        blocks. Drag any of them into your existing model.
%     3. After dragging blocks in, run configure_sandy_model('YourModel')
%        to fix solver settings and variable-size message limits.
%
%   Why programmatic: building these subsystems by hand is ~40 GUI clicks
%   each and easy to get wrong. This script is reproducible and inspectable.
%
%   Requires: ROS Toolbox.

lib_name = 'sandy_sensors';

% Close any pre-existing version and start fresh.
if bdIsLoaded(lib_name)
    close_system(lib_name, 0);
end
if exist([lib_name '.slx'], 'file')
    delete([lib_name '.slx']);
end

% Create the library.
new_system(lib_name, 'Library');
load_system(lib_name);
set_param(lib_name, 'Lock', 'off');

fprintf('Building Sandy sensor library...\n');

% Build each sensor subsystem at staggered positions.
build_bpw34_subsystem(lib_name,     [30,  30]);
build_as7341_subsystem(lib_name,    [30, 180]);
build_ms5837_subsystem(lib_name,    [30, 380]);
build_ds18b20_subsystem(lib_name,   [30, 530]);

% Lock and save.
set_param(lib_name, 'Lock', 'on');
save_system(lib_name);
close_system(lib_name);

fprintf('Done. Created %s.slx in %s\n', lib_name, pwd);
fprintf('Open it to drag blocks into your model.\n');
end


% =========================================================================
%  BPW34 photodiode -> scalar analog signal (0..1 normalised)
% =========================================================================
function build_bpw34_subsystem(parent, origin)
name = 'BPW34_Photodiode';
path = [parent '/' name];
add_block('built-in/Subsystem', path, 'Position', pos_box(origin, 280, 100));

% --- Subscribe block ---
sub_path = [path '/Subscribe'];
add_block('ros2lib/Subscribe', sub_path, 'Position', [40 40 140 100]);
set_param(sub_path, ...
    'topicSource', 'Specify your own', ...
    'topic', '/sandy/photodiode_side', ...
    'messageType', 'sensor_msgs/Image', ...
    'qosReliability', 'Best Effort', ...
    'qosDurability', 'Volatile', ...
    'qosHistory', 'Keep last', ...
    'qosDepth', '5', ...
    'sampleTime', '0.1');

% --- MATLAB Function block: average all bytes -> scalar 0..1 ---
fn_path = [path '/Reduce_to_scalar'];
add_block('simulink/User-Defined Functions/MATLAB Function', fn_path, ...
    'Position', [200 40 320 100]);

fn_text = sprintf([ ...
    'function I = reduce(msg, isnew)\n' ...
    '%%#codegen\n' ...
    '%% Average all bytes in the image data; normalise to 0..1.\n' ...
    '%% msg.Data is uint8 array (variable-size).\n' ...
    'persistent last_val\n' ...
    'if isempty(last_val), last_val = 0; end\n' ...
    'if isnew\n' ...
    '    n = double(msg.Data_SL_Info.CurrentLength);\n' ...
    '    if n > 0\n' ...
    '        last_val = double(sum(msg.Data(1:n))) / (n * 255);\n' ...
    '    end\n' ...
    'end\n' ...
    'I = last_val;\n']);
set_stateflow_text(fn_path, 'reduce', fn_text);

% --- Out port ---
out_path = [path '/Light_intensity'];
add_block('built-in/Outport', out_path, ...
    'Position', [380 60 410 80]);

% Connect: Subscribe.Msg -> fn.msg, Subscribe.IsNew -> fn.isnew, fn -> Out
add_line(path, 'Subscribe/1', 'Reduce_to_scalar/1', 'autorouting', 'on');
add_line(path, 'Subscribe/2', 'Reduce_to_scalar/2', 'autorouting', 'on');
add_line(path, 'Reduce_to_scalar/1', 'Light_intensity/1', 'autorouting', 'on');

% Add help text to the subsystem mask.
set_mask_help(path, [ ...
    'BPW34 photodiode proxy. Subscribes to /sandy/photodiode_side ' ...
    '(sensor_msgs/Image, 4x4 RGB from Gazebo) and outputs a scalar ' ...
    'irradiance in [0, 1]. Multiply by your photocurrent scale factor ' ...
    '(e.g. 5e-7 A) before feeding into the TIA.']);
end


% =========================================================================
%  AS7341 -> 8 spectral channels F1..F8 (synthesised from RGB)
% =========================================================================
function build_as7341_subsystem(parent, origin)
name = 'AS7341_8Channel';
path = [parent '/' name];
add_block('built-in/Subsystem', path, 'Position', pos_box(origin, 280, 140));

sub_path = [path '/Subscribe'];
add_block('ros2lib/Subscribe', sub_path, 'Position', [40 50 140 110]);
set_param(sub_path, ...
    'topicSource', 'Specify your own', ...
    'topic', '/sandy/as7341_down', ...
    'messageType', 'sensor_msgs/Image', ...
    'qosReliability', 'Best Effort', ...
    'qosDurability', 'Volatile', ...
    'qosHistory', 'Keep last', ...
    'qosDepth', '5', ...
    'sampleTime', '0.1');

fn_path = [path '/RGB_to_F1F8'];
add_block('simulink/User-Defined Functions/MATLAB Function', fn_path, ...
    'Position', [200 40 340 120]);

% This is the deliberate "useful lie" mapping. Each AS7341 band gets a
% weighted sum of mean(R), mean(G), mean(B). Weights chosen so that:
%   - F1 (415nm, violet) and F2 (445nm) and F3 (480nm) follow Blue dominantly
%   - F4 (515nm) is half Blue, half Green (cyan transition)
%   - F5 (555nm, green) and F6 (590nm) follow Green
%   - F7 (630nm) and F8 (680nm) follow Red
% This preserves the spectral *trend* the AS7341 would see, which is
% sufficient for testing red/blue detection logic. Replace with a custom
% Gazebo plugin if you need physically accurate spectral retrievals.

fn_text = sprintf([ ...
    'function F = rgb_to_bands(msg, isnew)\n' ...
    '%%#codegen\n' ...
    '%% Synthesise 8 AS7341 channels (F1..F8) from a Gazebo RGB image.\n' ...
    '%% Output F is uint16 1x8, range 0..65535, matching AS7341 ADC.\n' ...
    'persistent last\n' ...
    'if isempty(last), last = uint16(zeros(1,8)); end\n' ...
    'if isnew\n' ...
    '    n = double(msg.Data_SL_Info.CurrentLength);\n' ...
    '    if mod(n, 3) == 0 && n > 0\n' ...
    '        d = double(msg.Data(1:n));\n' ...
    '        R = mean(d(1:3:end));\n' ...
    '        G = mean(d(2:3:end));\n' ...
    '        B = mean(d(3:3:end));\n' ...
    '        %% 8x3 weight matrix (rows = F1..F8, cols = R G B).\n' ...
    '        W = [0.00 0.05 0.95;   %% F1 415nm\n' ...
    '             0.00 0.10 0.90;   %% F2 445nm\n' ...
    '             0.00 0.25 0.75;   %% F3 480nm\n' ...
    '             0.00 0.50 0.50;   %% F4 515nm\n' ...
    '             0.10 0.80 0.10;   %% F5 555nm\n' ...
    '             0.30 0.65 0.05;   %% F6 590nm\n' ...
    '             0.75 0.25 0.00;   %% F7 630nm\n' ...
    '             0.95 0.05 0.00];  %% F8 680nm\n' ...
    '        bands = W * [R; G; B];\n' ...
    '        %% Scale 0..255 (byte) up to 0..65535 (16-bit ADC).\n' ...
    '        last = uint16(min(65535, bands * 257));\n' ...
    '    end\n' ...
    'end\n' ...
    'F = last;\n']);
set_stateflow_text(fn_path, 'rgb_to_bands', fn_text);

out_path = [path '/F1_F8'];
add_block('built-in/Outport', out_path, 'Position', [400 70 430 90]);

add_line(path, 'Subscribe/1', 'RGB_to_F1F8/1', 'autorouting', 'on');
add_line(path, 'Subscribe/2', 'RGB_to_F1F8/2', 'autorouting', 'on');
add_line(path, 'RGB_to_F1F8/1', 'F1_F8/1', 'autorouting', 'on');

set_mask_help(path, [ ...
    'AS7341 8-channel proxy. Subscribes to /sandy/as7341_down ' ...
    '(sensor_msgs/Image, 8x8 RGB from Gazebo) and synthesises 8 ' ...
    'spectral channels F1..F8 (415, 445, 480, 515, 555, 590, 630, 680 nm). ' ...
    'Output is uint16(1x8), matching the AS7341 16-bit ADC range. ' ...
    'NOTE: This is a synthesis from broadband RGB. Useful for testing ' ...
    'detection logic and data pipelines; not physically accurate for ' ...
    'spectral retrieval algorithms.']);
end


% =========================================================================
%  MS5837 -> pressure (Pa) and depth (m)
% =========================================================================
function build_ms5837_subsystem(parent, origin)
name = 'MS5837_Pressure';
path = [parent '/' name];
add_block('built-in/Subsystem', path, 'Position', pos_box(origin, 280, 120));

sub_path = [path '/Subscribe'];
add_block('ros2lib/Subscribe', sub_path, 'Position', [40 30 140 90]);
set_param(sub_path, ...
    'topicSource', 'Specify your own', ...
    'topic', '/sandy/pressure', ...
    'messageType', 'sensor_msgs/FluidPressure', ...
    'qosReliability', 'Best Effort', ...
    'qosDurability', 'Volatile', ...
    'qosHistory', 'Keep last', ...
    'qosDepth', '5', ...
    'sampleTime', '0.1');

% Bus Selector for FluidPressure -> scalar
bus_path = [path '/Bus_Selector'];
add_block('built-in/BusSelector', bus_path, 'Position', [200 30 220 90]);
% Note: the user may have to update the model and reopen the selector
% mask once to actually populate the signal list. We pre-set the field name.
set_param(bus_path, 'OutputSignals', 'fluid_pressure');

% Compute depth: (P - P_atm) / (rho * g)
% P_atm = 101325, rho = 1028, g = 9.80665 -> rho*g = 10080.3382
depth_path = [path '/Compute_depth'];
add_block('simulink/User-Defined Functions/Fcn', depth_path, ...
    'Expr', '(u - 101325) / 10080.3382', ...
    'Position', [280 70 360 100]);

% Outputs
p_out = [path '/Pressure_Pa'];
add_block('built-in/Outport', p_out, 'Position', [400 30 430 50]);
d_out = [path '/Depth_m'];
add_block('built-in/Outport', d_out, 'Position', [400 75 430 95]);

add_line(path, 'Subscribe/1', 'Bus_Selector/1', 'autorouting', 'on');
add_line(path, 'Bus_Selector/1', 'Pressure_Pa/1', 'autorouting', 'on');
add_line(path, 'Bus_Selector/1', 'Compute_depth/1', 'autorouting', 'on');
add_line(path, 'Compute_depth/1', 'Depth_m/1', 'autorouting', 'on');

set_mask_help(path, [ ...
    'MS5837-30BA proxy. Subscribes to /sandy/pressure ' ...
    '(sensor_msgs/FluidPressure, computed from buoy Z by the helper node). ' ...
    'Outputs absolute pressure in Pa AND depth in m ' ...
    '(depth = (P - 101325) / (1028 * 9.80665)).']);
end


% =========================================================================
%  DS18B20 -> temperature (C)
% =========================================================================
function build_ds18b20_subsystem(parent, origin)
name = 'DS18B20_Temperature';
path = [parent '/' name];
add_block('built-in/Subsystem', path, 'Position', pos_box(origin, 280, 100));

sub_path = [path '/Subscribe'];
add_block('ros2lib/Subscribe', sub_path, 'Position', [40 30 140 90]);
set_param(sub_path, ...
    'topicSource', 'Specify your own', ...
    'topic', '/sandy/temperature', ...
    'messageType', 'sensor_msgs/Temperature', ...
    'qosReliability', 'Best Effort', ...
    'qosDurability', 'Volatile', ...
    'qosHistory', 'Keep last', ...
    'qosDepth', '5', ...
    'sampleTime', '0.1');

bus_path = [path '/Bus_Selector'];
add_block('built-in/BusSelector', bus_path, 'Position', [200 30 220 90]);
set_param(bus_path, 'OutputSignals', 'temperature');

out_path = [path '/Temperature_C'];
add_block('built-in/Outport', out_path, 'Position', [300 50 330 70]);

add_line(path, 'Subscribe/1', 'Bus_Selector/1', 'autorouting', 'on');
add_line(path, 'Bus_Selector/1', 'Temperature_C/1', 'autorouting', 'on');

set_mask_help(path, [ ...
    'DS18B20 temperature proxy. Subscribes to /sandy/temperature ' ...
    '(sensor_msgs/Temperature, published by the helper node from the ' ...
    'Antarctic depth profile OR a manual slider). Outputs scalar T in C.']);
end


% =========================================================================
%  Helpers
% =========================================================================
function p = pos_box(origin, w, h)
% Build [x1 y1 x2 y2] from top-left origin and width/height.
p = [origin(1), origin(2), origin(1) + w, origin(2) + h];
end

function set_stateflow_text(block_path, fn_name, code)
% Replace the body of a MATLAB Function block with new code.
% The Stateflow API is used because Simulink doesn't expose function
% block bodies via set_param directly.
sf = sfroot;
chart = sf.find('-isa', 'Stateflow.EMChart', 'Path', block_path);
if isempty(chart)
    error('Could not find MATLAB Function block at %s', block_path);
end
chart.Script = code;
% Rename the chart's function to match (cosmetic).
fns = chart.find('-isa', 'Stateflow.Function');
if ~isempty(fns)
    fns(1).Name = fn_name;
end
end

function set_mask_help(block_path, text)
% Add a description that appears on hover.
set_param(block_path, 'AttributesFormatString', sprintf('Sandy Sensor'));
set_param(block_path, 'Description', text);
end
