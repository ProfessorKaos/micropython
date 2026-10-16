# SEREN is the shared configuration layer, not a buildable board.
# The boards are SEREN_C3, SEREN_C6 and SEREN_S3 (thin per-chip directories
# that include ../SEREN/seren_common.cmake).
message(FATAL_ERROR
    "SEREN is the shared board layer, not a board. Build one of:\n"
    "  make BOARD=SEREN_C3\n"
    "  make BOARD=SEREN_C6\n"
    "  make BOARD=SEREN_S3\n"
    "Optionally add BOARD_VARIANT=<WIFI|BLE|WIFI_BLE|...> for the feature profile "
    "(see boards/SEREN/README.md).")
