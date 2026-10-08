freeze("$(PORT_DIR)/modules")
package("app", base_path="$(WORKSPACE_PATH)", files=[

    "commands.py",
    # "config_server.py",
    "network_manager.py",
    "led.py",
    # "led_c3.py",
    "mcpwm.py",
    # "mic.py",
    "ota_ble.py",
    "aioble_ota.py",
    "ble.py",
    "rotary.py",
    "run.py",
    "system.py",
    "version.py"
    ])
# package("microdot", base_path="$(WORKSPACE_PATH)")
# package("ota", base_path="$(WORKSPACE_PATH)")
# package("static", base_path="$(WORKSPACE_PATH)", files=["index.py", "main.py", "style.py"])
freeze("$(WORKSPACE_PATH)", script="main.py")

include("$(MPY_DIR)/extmod/asyncio")

# Useful networking-related packages.
# require("ssl")
# require("ntptime")
# require("bundle-networking")

# Require some micropython-lib modules.
require("aioespnow")
require("neopixel")
require("aioble-server")
require("aioble-peripheral")

