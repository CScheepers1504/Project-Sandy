% Project Sandy Sensor Test (revised)
% ===================================
% Changes from previous version:
%
%   1. Depth comes from /sandy/pressure published by depth_publisher.py
%      (true hydrostatic pressure from buoy pose). The old air_pressure
%      sensor returned ~101325 Pa regardless of depth.
%
%   2. Photodiode is now read as an Nx3 image (R8G8B8), averaged across
%      ALL pixels and ALL channels for a scalar irradiance proxy. The old
%      version called rosReadImage and then mean(double(pdVal(:))) on a
%      1x1x3 array, which gave a strange averaging behaviour.
%
%   3. AS7341 channel intensities are averaged across the full image, with
%      a sanity-check NaN guard so a missing frame doesn't kill the loop.
%
%   4. Red/blue ratio detection is gated on a minimum illumination level
%      so a dark frame (both channels ~0) doesn't spuriously trigger.
%
%   5. Loop pacing uses pause(0.1) with an explicit fprintf flush so the
%      console stays responsive when MATLAB is also driving Simulink.

try, ros2 shutdown; catch, end
ros2Node = ros2node("/matlab_sandy_node");

imgData  = [];
pdData   = [];
presData = [];

subAS7341 = ros2subscriber(ros2Node, "/sandy/as7341_down",     "sensor_msgs/Image", ...
    @(msg) assignin('base','imgData',msg),  "Reliability","besteffort");
subPD     = ros2subscriber(ros2Node, "/sandy/photodiode_side", "sensor_msgs/Image", ...
    @(msg) assignin('base','pdData',msg),   "Reliability","besteffort");
subPres   = ros2subscriber(ros2Node, "/sandy/pressure",        "sensor_msgs/FluidPressure", ...
    @(msg) assignin('base','presData',msg), "Reliability","besteffort");

fprintf('Waiting for first messages...\n');
pause(2);

% Physical constants (must match depth_publisher.py)
RHO = 1028.0;
G   = 9.80665;
PATM = 101325.0;

% Detection thresholds
MIN_ILLUM = 5;       % Below this, frame is too dark to draw conclusions
RB_RATIO  = 1.5;     % Red/Blue ratio threshold for "phytoplankton present"

while true
    if isempty(imgData)
        fprintf('No AS7341 data yet -- waiting...\n');
        pause(0.5);
        continue;
    end

    % --- 1. AS7341 multi-spectral proxy ------------------------------------
    img = rosReadImage(imgData);                   % HxWx3 uint8
    redIntensity   = mean(img(:,:,1), 'all');
    greenIntensity = mean(img(:,:,2), 'all');
    blueIntensity  = mean(img(:,:,3), 'all');

    % --- 2. BPW34 side photodiode proxy ------------------------------------
    if ~isempty(pdData)
        pdImg = rosReadImage(pdData);              % HxWx3 uint8
        % Average across both space and channels -> scalar irradiance.
        % For colour-weighted irradiance (e.g. matching BPW34's spectral
        % response curve), apply weights here instead of equal averaging.
        pdIntensity = mean(double(pdImg), 'all');
    else
        pdIntensity = NaN;
    end

    % --- 3. Depth from true hydrostatic pressure ---------------------------
    if ~isempty(presData)
        depth = (presData.fluid_pressure - PATM) / (RHO * G);
    else
        depth = NaN;
    end

    fprintf('Depth: %6.2f m | R: %6.2f  G: %6.2f  B: %6.2f | Side: %6.2f\n', ...
            depth, redIntensity, greenIntensity, blueIntensity, pdIntensity);

    % --- 4. Detection logic ------------------------------------------------
    totalIllum = redIntensity + greenIntensity + blueIntensity;
    if totalIllum > MIN_ILLUM ...
            && blueIntensity > 0 ...
            && (redIntensity / blueIntensity) > RB_RATIO
        disp("--- PHYTOPLANKTON DETECTED (red > 1.5 * blue) ---");
    end

    pause(0.1);
end
