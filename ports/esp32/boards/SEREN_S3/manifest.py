freeze("$(PORT_DIR)/modules")
# WORKSPACE_PATH is supplied at build time via MICROPY_MANIFEST_WORKSPACE_PATH
# (see build_s3.sh). It points to the git worktree of Zen-Specs/Code_device for
# the branch being built, so it differs per branch/worktree.
package("app", base_path="$(WORKSPACE_PATH)")
package("microdot", base_path="$(WORKSPACE_PATH)")
package("ota", base_path="$(WORKSPACE_PATH)")
package("static", base_path="$(WORKSPACE_PATH)")
freeze("$(WORKSPACE_PATH)", script="main.py")

include("$(MPY_DIR)/extmod/asyncio")

# Useful networking-related packages.
require("bundle-networking")

# Require some micropython-lib modules.
require("aioespnow")
require("neopixel")
require("aioble-server")
require("aioble-peripheral")