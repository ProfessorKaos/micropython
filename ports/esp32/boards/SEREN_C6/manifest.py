freeze("$(PORT_DIR)/seren_c6_modules")

# freeze("$(PORT_DIR)/seren_modules", "_boot.py")
# freeze("$(PORT_DIR)/seren_modules", "espnow.py")
# freeze("$(PORT_DIR)/seren_modules", "flashbdev.py")
# freeze("$(PORT_DIR)/seren_modules", "inisetup.py")
# freeze("$(PORT_DIR)/seren_modules", "main.py")
# freeze("$(PORT_DIR)/seren_modules", "webrepl_cfg.py")

# package("app", files=[
# "aioble_ota.py",
# "ble.py",
# "led.py",
# "network_manager.py",
# "ota_ble.py",
# "rotary.py",
# "run.py",
# "system.py",
# "version.py",
# ], base_path="$(PORT_DIR)/seren_modules")

# freeze("$(PORT_DIR)/seren_modules", (
# "app/aioble_ota.py",
# "app/ble.py",
# "app/led.py",
# "app/network_manager.py",
# "app/ota_ble.py",
# "app/rotary.py",
# "app/run.py",
# "app/system.py",
# "app/version.py",
# ))


# freeze("$(PORT_DIR)/seren_modules/app", 
#        ("aioble_ota.py", "ble.py", "led.py", 
#         "network_manager.py", "ota_ble.py", 
#         "rotary.py", "run.py", "system.py", "version.py")
#         )


# freeze("$(PORT_DIR)/seren_modules/ota")

include("$(MPY_DIR)/extmod/asyncio")

# Useful networking-related packages.
# require("bundle-networking")

# Require some micropython-lib modules.
require("aioespnow")
require("neopixel")

#require("aioble")
# freeze("$(MPY_LIB_DIR)/micropython/bluetooth/aioble","peripheral.py")
require("aioble-server")
require("aioble-peripheral")


# require("dht")
# require("ds18x20")
# require("onewire")
# require("umqtt.robust")
# require("umqtt.simple")
# require("upysh")
