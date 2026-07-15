freeze("$(PORT_DIR)/ble_only")
include("$(MPY_DIR)/extmod/asyncio")

# Useful networking-related packages.
require("bundle-networking")

# Require some micropython-lib modules.
require("aioespnow")
require("neopixel")
require("aioble")

