/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 * Adapted for MicroPython by the Xiaomi LLM Core Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief QMA6100P 3-axis accelerometer driver (I2C transport abstraction)
 *
 * Self-contained driver adapted from the Espressif esp-bsp QMA6100P component.
 * Uses function-pointer-based I2C transport instead of ESP-IDF I2C master bus,
 * making it compatible with MicroPython's machine.I2C protocol.
 */

#include <stdlib.h>
#include <string.h>
#include "qma6100p.h"
#include "esp_log.h"

static const char *TAG = "QMA6100P";

/* ── Register addresses (from esp-bsp qma6100p.c) ──────────────────── */

#define QMA6100P_WHO_AM_I           0x00u
#define QMA6100P_ACCEL_XOUT_H       0x01u
#define QMA6100P_ACCEL_CONFIG       0x0Fu
#define QMA6100P_FIFO_FRAME_CTR     0x0Eu
#define QMA6100P_PWR_MGMT_1         0x11u
#define QMA6100P_NVM_LOAD           0x33u

/* ── Internal device structure ─────────────────────────────────────── */

typedef struct {
    qma6100p_i2c_transport_t transport;
    uint8_t dev_addr;
} qma6100p_dev_t;

/* ── Low-level I2C helpers ─────────────────────────────────────────── */

static esp_err_t qma6100p_write_reg(qma6100p_handle_t sensor,
                                     uint8_t reg, uint8_t val)
{
    qma6100p_dev_t *dev = (qma6100p_dev_t *)sensor;
    return dev->transport.write(dev->transport.ctx, dev->dev_addr,
                                reg, &val, 1);
}

static esp_err_t qma6100p_read_regs(qma6100p_handle_t sensor,
                                     uint8_t reg, uint8_t *buf, uint8_t len)
{
    qma6100p_dev_t *dev = (qma6100p_dev_t *)sensor;
    return dev->transport.read(dev->transport.ctx, dev->dev_addr,
                               reg, buf, len);
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t qma6100p_create(const qma6100p_i2c_transport_t *transport,
                           uint8_t dev_addr,
                           qma6100p_handle_t *handle_ret)
{
    if (transport == NULL || transport->read == NULL ||
        transport->write == NULL || handle_ret == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    qma6100p_dev_t *dev = (qma6100p_dev_t *)calloc(1, sizeof(qma6100p_dev_t));
    if (dev == NULL) {
        return ESP_ERR_NO_MEM;
    }

    dev->transport = *transport;
    dev->dev_addr = dev_addr;
    *handle_ret = (qma6100p_handle_t)dev;
    return ESP_OK;
}

void qma6100p_delete(qma6100p_handle_t sensor)
{
    if (sensor != NULL) {
        free(sensor);
    }
}

esp_err_t qma6100p_get_deviceid(qma6100p_handle_t sensor, uint8_t *deviceid)
{
    return qma6100p_read_regs(sensor, QMA6100P_WHO_AM_I, deviceid, 1);
}

esp_err_t qma6100p_nvm_load(qma6100p_handle_t sensor)
{
    esp_err_t ret;
    uint8_t tmp;

    ret = qma6100p_read_regs(sensor, QMA6100P_NVM_LOAD, &tmp, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    tmp |= (1 << 3);  /* BIT3 */
    return qma6100p_write_reg(sensor, QMA6100P_NVM_LOAD, tmp);
}

esp_err_t qma6100p_wake_up(qma6100p_handle_t sensor)
{
    esp_err_t ret;
    uint8_t tmp;

    ret = qma6100p_read_regs(sensor, QMA6100P_PWR_MGMT_1, &tmp, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    tmp |= (1 << 7);  /* BIT7 — suspend disable */
    return qma6100p_write_reg(sensor, QMA6100P_PWR_MGMT_1, tmp);
}

esp_err_t qma6100p_sleep(qma6100p_handle_t sensor)
{
    esp_err_t ret;
    uint8_t tmp;

    ret = qma6100p_read_regs(sensor, QMA6100P_PWR_MGMT_1, &tmp, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    tmp &= ~(1 << 7);  /* Clear BIT7 — enter suspend */
    return qma6100p_write_reg(sensor, QMA6100P_PWR_MGMT_1, tmp);
}

esp_err_t qma6100p_config(qma6100p_handle_t sensor,
                           qma6100p_acce_fs_t acce_fs)
{
    esp_err_t ret;
    uint8_t config_reg;

    ret = qma6100p_read_regs(sensor, QMA6100P_ACCEL_CONFIG, &config_reg, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    config_reg = (config_reg & 0xF0) | (acce_fs & 0x0F);
    return qma6100p_write_reg(sensor, QMA6100P_ACCEL_CONFIG, config_reg);
}

esp_err_t qma6100p_get_acce_sensitivity(qma6100p_handle_t sensor,
                                         float *acce_sensitivity)
{
    esp_err_t ret;
    uint8_t acce_fs;

    ret = qma6100p_read_regs(sensor, QMA6100P_ACCEL_CONFIG, &acce_fs, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    acce_fs &= 0x0F;
    switch (acce_fs) {
        case ACCE_FS_2G:  *acce_sensitivity = 4096.0f; break;
        case ACCE_FS_4G:  *acce_sensitivity = 2048.0f; break;
        case ACCE_FS_8G:  *acce_sensitivity = 1024.0f; break;
        case ACCE_FS_16G: *acce_sensitivity =  512.0f; break;
        case ACCE_FS_32G: *acce_sensitivity =  256.0f; break;
        default:          *acce_sensitivity = 4096.0f; break;
    }
    return ESP_OK;
}

esp_err_t qma6100p_get_raw_acce(qma6100p_handle_t sensor,
                                 qma6100p_raw_acce_value_t *raw_acce)
{
    uint8_t data[6];
    esp_err_t ret;

    ret = qma6100p_read_regs(sensor, QMA6100P_ACCEL_XOUT_H, data, sizeof(data));
    if (ret != ESP_OK) {
        return ret;
    }

    /* The QMA6100P stores 14-bit data left-shifted by 2 in 6 bytes.
     * Per the esp-bsp driver, divide by 4 to get the raw count. */
    raw_acce->raw_acce_x = (int16_t)((data[1] << 8) | data[0]) / 4;
    raw_acce->raw_acce_y = (int16_t)((data[3] << 8) | data[2]) / 4;
    raw_acce->raw_acce_z = (int16_t)((data[5] << 8) | data[4]) / 4;
    return ESP_OK;
}

esp_err_t qma6100p_get_acce(qma6100p_handle_t sensor,
                             qma6100p_acce_value_t *acce)
{
    esp_err_t ret;
    float sensitivity;
    qma6100p_raw_acce_value_t raw;

    ret = qma6100p_get_acce_sensitivity(sensor, &sensitivity);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = qma6100p_get_raw_acce(sensor, &raw);
    if (ret != ESP_OK) {
        return ret;
    }

    acce->acce_x = (float)raw.raw_acce_x / sensitivity;
    acce->acce_y = (float)raw.raw_acce_y / sensitivity;
    acce->acce_z = (float)raw.raw_acce_z / sensitivity;
    return ESP_OK;
}
