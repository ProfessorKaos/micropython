# Feature profile: BLE + OTA-over-BLE, no WiFi stack and no config server.
include("../manifest_common.py", chip=options.chip, wifi=False, ble=True, config_server=False)
