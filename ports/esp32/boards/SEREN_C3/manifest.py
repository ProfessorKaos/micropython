# Thin per-board wrapper: hands off to the shared SEREN manifest layer,
# parameterized by chip and by the feature profile chosen in cmake
# ($(SEREN_PROFILE), set via seren_profile() in boards/SEREN/seren_common.cmake).
include("../SEREN/profiles/$(SEREN_PROFILE).py", chip="c3")