% Change all MATLAB Function blocks with sample time 1e-6 to 1e-3
model = 'SensingSubsystem';
load_system(model);

% Find every Stateflow chart / MATLAB Function block
blocks = find_system(model, 'LookUnderMasks', 'all', ...
    'FollowLinks', 'on', ...
    'BlockType', 'SubSystem', ...
    'SFBlockType', 'MATLAB Function');

count = 0;
for i = 1:length(blocks)
    try
        current = get_param(blocks{i}, 'SystemSampleTime');
        if strcmp(current, '1e-6') || str2double(current) == 1e-6
            set_param(blocks{i}, 'SystemSampleTime', '1e-3');
            fprintf('Updated: %s\n', blocks{i});
            count = count + 1;
        end
    catch
        % Skip blocks that don't have SystemSampleTime
    end
end

fprintf('\nTotal blocks updated: %d\n', count);
save_system(model);