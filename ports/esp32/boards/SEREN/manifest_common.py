# Shared SEREN frozen-content manifest: the single source of truth for what
# goes into every SEREN firmware image.
#
# Included from boards/SEREN/profiles/<profile>.py with feature flags; the
# per-board boards/SEREN_Cx/manifest.py supplies the chip. Options:
#
#   options.chip          -- "c3", "c6" or "s3"
#   options.wifi          -- WiFi/networking stack (webrepl, requests, ...)
#   options.ble           -- BLE support (aioble + OTA-over-BLE)
#   options.config_server -- config server and its static web assets (implies wifi)

options.defaults(wifi=False, ble=False, config_server=False)

freeze("$(PORT_DIR)/modules")

# Build variant baked in by seren_common.cmake ("BLE", "FLASH_4M", ... or ""
# for the default profile). The app derives its OTA manifest name from it.
module("build_variant.py", base_path="$(BUILD_DIR)")

app_files = [
    "commands.py",
    "led.py",
    "network_manager.py",
    "rotary.py",
    "run.py",
    "system.py",
    "version.py",
]
if options.chip == "c3":
    app_files.append("led_c3.py")
else:
    app_files.append("mcpwm.py")
    if options.chip == "s3":
        app_files.append("mic.py")
if options.ble:
    app_files += ["aioble_ota.py", "ble.py", "ota_ble.py"]
if options.config_server:
    app_files.append("config_server.py")

package("app", base_path="$(WORKSPACE_PATH)", files=app_files)

# OTA support, used by both the BLE and config-server update paths.
package("ota", base_path="$(WORKSPACE_PATH)")

if options.config_server:
    package("microdot", base_path="$(WORKSPACE_PATH)")
    # Only the gzipped variants (tools/convert_statics.py) are flashed; raw
    # html/css/js stay on the host as source of truth.
    package("static", base_path="$(WORKSPACE_PATH)", files=["index.py", "main.py", "style.py"])

freeze("$(WORKSPACE_PATH)", script="main.py")

include("$(MPY_DIR)/extmod/asyncio")

if options.wifi:
    # Useful networking-related packages (mip, ssl, requests, webrepl, ...).
    require("bundle-networking")

# Require some micropython-lib modules.
require("aioespnow")
require("neopixel")

if options.ble:
    require("aioble-server")
    require("aioble-peripheral")
