MCU_SERIES = m4
MCU_VARIANT = nrf52
MCU_SUB_VARIANT = nrf52840
SOFTDEV_VERSION = 7.3.0
SD=s140
LD_FILES += boards/SEEED_XIAO_NRF52/XIAO_bootloader.ld boards/nrf52840_1M_256k.ld

# Default location for per-board user C modules. Override by passing
# USER_C_MODULES on the make command line if needed. This path matches
# the example build helper `ports/nrf/zenrf.sh` which uses
# USER_C_MODULES=../../usermod/modules when invoked from `ports/nrf`.
USER_C_MODULES ?= ../../usermod/modules

NRF_DEFINES += -DNRF52840_XXAA

MICROPY_VFS_LFS1 = 1
MICROPY_VFS_LFS2 = 1
FS_SIZE = 256k

# DEBUG ?= 1

uf2: hex
	python3 $(TOP)/tools/uf2conv.py -c -o $(BUILD)/firmware.uf2 -f 0xADA52840 $(BUILD)/firmware.hex
