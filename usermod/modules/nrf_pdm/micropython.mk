# nrf_pdm usermod: PDM microphone capture via nrfx_pdm + EasyDMA.
NRF_PDM_MOD_DIR := $(USERMOD_DIR)

SRC_USERMOD_C += $(NRF_PDM_MOD_DIR)/modnrf_pdm.c
CFLAGS_USERMOD += -I$(NRF_PDM_MOD_DIR)
