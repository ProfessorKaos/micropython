# Feature profile: WiFi + config server with static web assets, no BLE.
include("../manifest_common.py", chip=options.chip, wifi=True, ble=False, config_server=True)
