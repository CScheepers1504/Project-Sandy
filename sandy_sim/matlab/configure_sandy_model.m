function configure_sandy_model(model_name)
% CONFIGURE_SANDY_MODEL  Apply Sandy-friendly settings to an open model.
%
%   configure_sandy_model('YourModelName')
%
%   Fixes the two things that cause real-time grief:
%
%     1. Solver: switches to Fixed-step discrete at 0.1 s. This is the
%        single biggest factor in getting RTF up to ~1. With a continuous
%        solver, ROS Subscribe blocks force microscopic adaptive steps
%        every time a message arrives.
%
%     2. Variable-size message limits: increases the maximum 'data' array
%        length for sensor_msgs/Image to cover the AS7341 8x8 RGB (192
%        bytes -> 256 with margin) and BPW34 4x4 RGB (48 bytes -> 64).
%        Without this, the Subscribe block silently truncates.
%
%   Run AFTER you've dragged sensor blocks into your model. Run BEFORE
%   you hit Run.

if nargin < 1
    error('Usage: configure_sandy_model(''YourModelName'')');
end

if ~bdIsLoaded(model_name)
    error('Model %s is not loaded. Open it first.', model_name);
end

fprintf('Configuring %s...\n', model_name);

% -- Solver settings ------------------------------------------------------
set_param(model_name, 'SolverType', 'Fixed-step');
set_param(model_name, 'Solver', 'FixedStepDiscrete');
set_param(model_name, 'FixedStep', '0.1');
set_param(model_name, 'StopTime', 'inf');  % run until user stops
fprintf('  Solver: Fixed-step discrete, step = 0.1 s\n');

% -- Variable-size message limits ----------------------------------------
% For ROS Toolbox, we use the ros2.internal API. Documented form is the
% Configuration Parameters dialog (Simulation > ROS Toolbox > Variable-Size
% Messages), but the API below sets it programmatically.

try
    msg_specs = ros2.internal.getMessageInfo(model_name);
    %#ok<*UNRCH>
    if isfield(msg_specs, 'sensor_msgs_Image')
        set_param(model_name, ...
            'ROS2VarSize_sensor_msgs_Image_data', '512');
        fprintf('  sensor_msgs/Image data: 512 bytes (covers 8x8 RGB + margin)\n');
    end
catch
    % Fallback: use the official ros.slros.internal API if available.
    try
        ros.slros.internal.bus.Util.setBusMaxLength(model_name, ...
            'sensor_msgs/Image', 'data', 512);
        fprintf('  sensor_msgs/Image data limit set via slros API\n');
    catch ME
        warning(['Could not set variable-size limits programmatically: %s\n' ...
                 'Set them manually: Simulation tab -> ROS Toolbox -> ' ...
                 'Variable-Size Messages -> sensor_msgs/Image -> ' ...
                 'untick "use defaults" -> set data length to 512.'], ...
                ME.message);
    end
end

fprintf('Done. Save the model and run.\n');
end
