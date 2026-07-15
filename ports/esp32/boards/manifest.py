freeze("$(PORT_DIR)/modules")
freeze("$(PORT_DIR)/microdot")
include("$(MPY_DIR)/extmod/asyncio")

# Useful networking-related packages.
require("bundle-networking")

# Require some micropython-lib modules.
require("aioespnow")
require("aioble")
require("dht")
# require("ds18x20")
require("neopixel")
# require("onewire")
require("umqtt.robust")
# require("umqtt.simple")
require("upysh")
