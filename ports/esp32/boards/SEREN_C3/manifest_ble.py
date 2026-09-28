freeze("$(PORT_DIR)/modules")
# WORKSPACE_PATH is supplied at build time via MICROPY_MANIFEST_WORKSPACE_PATH
# (see build_s3.sh). It points to the git worktree of Zen-Specs/Code_device for
# the branch being built, so it differs per branch/worktree.
package("app", base_path="$(WORKSPACE_PATH)", files=[
    "aioble_ota.py",
    "ble.py",
    "commands.py",
    # "config_server.py",
    "led.py",
    "led_c3.py",
    # "mcpwm.py",
    # "mic.py",
    "network_manager.py",
    "ota_ble.py",
    "rotary.py",
    "run.py",
    "system.py",
    "version.py"
    ])
package("microdot", base_path="$(WORKSPACE_PATH)")
package("ota", base_path="$(WORKSPACE_PATH)")
# Only the gzipped variants (tools/convert_statics.py) are flashed; raw
# html/css/js stay on the host as source of truth.
# package("static", base_path="$(WORKSPACE_PATH)", files=["index.py", "main.py", "style.py"])
freeze("$(WORKSPACE_PATH)", script="main.py")

include("$(MPY_DIR)/extmod/asyncio")

# Useful networking-related packages.
require("bundle-networking")

# Require some micropython-lib modules.
require("aioespnow")
require("neopixel")
require("aioble-server") # Not including the BLE functionality for now
require("aioble-peripheral")