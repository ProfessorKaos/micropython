#freeze("$(PORT_DIR)/seren_modules")
#freeze("/home/alexx/Zen-Specs/Code_device/app")
package("app", base_path="~/Zen-Specs/Code_device/)
package("microdot", base_path="~/Zen-Specs/Code_device/)
package("ota", base_path="~/Zen-Specs/Code_device/)
package("static", base_path="~/Zen-Specs/Code_device/)

include("$(MPY_DIR)/extmod/asyncio")

# Useful networking-related packages.
require("bundle-networking")

# Require some micropython-lib modules.
require("aioespnow")
require("neopixel")
require("aioble-server")
require("aioble-peripheral")

