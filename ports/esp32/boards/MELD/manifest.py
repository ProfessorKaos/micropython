include("$(MPY_DIR)/extmod/asyncio")
freeze("$(PORT_DIR)/modules")
package("microdot", base_path="$(PORT_DIR)")
package("ota", base_path="$(PORT_DIR)")
# freeze("/home/alexx/Documents/MELD/kair-micro-voice/device")

# Useful networking-related packages.
require("bundle-networking")

# Require some micropython-lib modules.
require("aioespnow")
# require("aioble")
require("dht")
# require("ds18x20")
require("neopixel")
# require("onewire")
require("umqtt.robust")
# require("umqtt.simple")
require("upysh")
