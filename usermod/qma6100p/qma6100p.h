/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 * Adapted for MicroPython by the Xiaomi LLM Core Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief QMA6100P 3-axis accelerometer driver for MicroPython
 *
 * Self-contained driver with I2C transport abstraction.
 * Adapted from the Espressif esp-bsp QMA6100P component.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── I2C addresses ─────────────────────────────────────────────────── */

#define QMA6100P_I2C_ADDRESS       0x12u  /*!< I2C address, AD0 low  */
#define QMA6100P_I2C_ADDRESS_1     0x13u  /*!< I2C address, AD0 high */

#define QMA6100P_WHO_AM_I_VAL      0x90u

/* ── Full-scale range enum ─────────────────────────────────────────── */

typedef enum {
    ACCE_FS_2G  = 0b0001,
    ACCE_FS_4G  = 0b0010,
    ACCE_FS_8G  = 0b0100,
    ACCE_FS_16G = 0b1000,
    ACCE_FS_32G = 0b1111,
} qma6100p_acce_fs_t;

/* ── Acceleration value structs ────────────────────────────────────── */

typedef struct {
    int16_t raw_acce_x;
    int16_t raw_acce_y;
    int16_t raw_acce_z;
} qma6100p_raw_acce_value_t;

typedef struct {
    float acce_x;
    float acce_y;
    float acce_z;
} qma6100p_acce_value_t;

/* ── I2C transport abstraction ─────────────────────────────────────── */

/**
 * @brief I2C write callback: write `len` bytes starting at `reg_addr`.
 *
 * @param ctx       Opaque context (e.g. MicroPython I2C object wrapper).
 * @param dev_addr  7-bit I2C slave address.
 * @param reg_addr  Register start address.
 * @param data      Data bytes to write.
 * @param len       Number of bytes to write.
 * @return ESP_OK on success.
 */
typedef esp_err_t (*qma6100p_i2c_write_fn)(void *ctx, uint8_t dev_addr,
                                            uint8_t reg_addr,
                                            const uint8_t *data, uint8_t len);

/**
 * @brief I2C read callback: read `len` bytes starting at `reg_addr`.
 *
 * @param ctx       Opaque context.
 * @param dev_addr  7-bit I2C slave address.
 * @param reg_addr  Register start address.
 * @param data      Buffer to receive data.
 * @param len       Number of bytes to read.
 * @return ESP_OK on success.
 */
typedef esp_err_t (*qma6100p_i2c_read_fn)(void *ctx, uint8_t dev_addr,
                                           uint8_t reg_addr,
                                           uint8_t *data, uint8_t len);

typedef struct {
    void *ctx;                   /*!< Opaque context passed to callbacks  */
    qma6100p_i2c_write_fn write; /*!< I2C write function                  */
    qma6100p_i2c_read_fn  read;  /*!< I2C read function                   */
} qma6100p_i2c_transport_t;

/* ── Handle ────────────────────────────────────────────────────────── */

typedef void *qma6100p_handle_t;

/* ── Driver API ────────────────────────────────────────────────────── */

/**
 * @brief Create and initialise the sensor driver.
 *
 * @param[in]  transport  I2C transport callbacks.
 * @param[in]  dev_addr   I2C device address (QMA6100P_I2C_ADDRESS or _1).
 * @param[out] handle_ret Driver handle.
 */
esp_err_t qma6100p_create(const qma6100p_i2c_transport_t *transport,
                           uint8_t dev_addr,
                           qma6100p_handle_t *handle_ret);

/**
 * @brief Delete and release the sensor driver.
 */
void qma6100p_delete(qma6100p_handle_t sensor);

/**
 * @brief Read WHO_AM_I register (should return 0x90).
 */
esp_err_t qma6100p_get_deviceid(qma6100p_handle_t sensor, uint8_t *deviceid);

/**
 * @brief Load non-volatile memory calibration.
 */
esp_err_t qma6100p_nvm_load(qma6100p_handle_t sensor);

/**
 * @brief Wake the sensor from sleep.
 */
esp_err_t qma6100p_wake_up(qma6100p_handle_t sensor);

/**
 * @brief Enter sleep mode.
 */
esp_err_t qma6100p_sleep(qma6100p_handle_t sensor);

/**
 * @brief Set accelerometer full-scale range.
 */
esp_err_t qma6100p_config(qma6100p_handle_t sensor,
                           qma6100p_acce_fs_t acce_fs);

/**
 * @brief Get current accelerometer sensitivity (LSB/g).
 */
esp_err_t qma6100p_get_acce_sensitivity(qma6100p_handle_t sensor,
                                         float *acce_sensitivity);

/**
 * @brief Read raw accelerometer values (LSB counts).
 */
esp_err_t qma6100p_get_raw_acce(qma6100p_handle_t sensor,
                                 qma6100p_raw_acce_value_t *raw_acce);

/**
 * @brief Read acceleration in g (floating point).
 */
esp_err_t qma6100p_get_acce(qma6100p_handle_t sensor,
                             qma6100p_acce_value_t *acce);

#ifdef __cplusplus
}
#endif
