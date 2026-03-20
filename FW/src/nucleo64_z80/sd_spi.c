/*
 * Copyright (c) 2026 @hanyazou
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "nucleo64_z80.h"
#include "nucleo64_config.h"

#include <stdio.h>

/*
 * SD(SPI) pin mux helper
 *
 * The SCK/MISO/MOSI pins are shared with non-SPI GPIO signals.
 * This module switches those pins between:
 *   - SPI Alternate Function (when SD is accessed)
 *   - GPIO Hi-Z (when SD is not accessed)
 *
 * Notes:
 * - CS is always forced inactive (High) on acquire/release for fail-safe.
 * - CS assertion/deassertion during an actual transaction is the caller's
 *   responsibility.
 * - SPI peripheral configuration (baudrate/mode, etc.) is handled by CubeMX;
 *   we only toggle SPE and pin modes here.
 */
static struct sd_spi_pin {
    GPIO_TypeDef *port;
    uint32_t pin;
    uint32_t alternate;  // GPIO_AFx_SPIy
} sd_spi_pins[] = {
    {SD_SPI_SCK_GPIO_Port, SD_SPI_SCK_Pin, SD_SPI_SCK_GPIO_AF},
    {SD_SPI_MISO_GPIO_Port, SD_SPI_MISO_Pin, SD_SPI_MISO_GPIO_AF},
    {SD_SPI_MOSI_GPIO_Port, SD_SPI_MOSI_Pin, SD_SPI_MOSI_GPIO_AF},
};

SPI_HandleTypeDef *g_hspi = NULL;

void sd_spi_start(SPI_HandleTypeDef *hspi)
{
    // Register SPI handle and park pins into "released" state.
    g_hspi = hspi;
    sd_spi_release();
}

void sd_spi_acquire(void)
{
    // Fail-safe: ensure SD is not selected while switching pin mux.
    HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);

    // Stop SPI before reconfiguring pins to avoid unintended edges.
    __HAL_SPI_DISABLE(g_hspi);

    // Switch shared pins to SPI alternate function.
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    for (unsigned int i = 0; i < (sizeof(sd_spi_pins) / sizeof(*sd_spi_pins)); i++) {
        struct sd_spi_pin *pin = &sd_spi_pins[i];
        GPIO_InitStruct.Pin = pin->pin;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        GPIO_InitStruct.Alternate = pin->alternate;
        HAL_GPIO_Init(pin->port, &GPIO_InitStruct);
    }

    // Enable SPI. CS remains inactive; caller controls CS timing.
    __HAL_SPI_ENABLE(g_hspi);
}

void sd_spi_release(void)
{
    // Fail-safe: deselect SD before disabling SPI / switching pins.
    HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);

    // Disable SPI peripheral (SPE=0).
    __HAL_SPI_DISABLE(g_hspi);

    // Park shared pins into Hi-Z so other GPIO functions can use them.
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    for (unsigned int i = 0; i < (sizeof(sd_spi_pins) / sizeof(*sd_spi_pins)); i++) {
        struct sd_spi_pin *pin = &sd_spi_pins[i];
        GPIO_InitStruct.Pin = pin->pin;
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(pin->port, &GPIO_InitStruct);
    }
}
