#include "spi.h"

#include <string.h>
#include <stdio.h>

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_system.h"

#include "board_config.h"

static spi_device_handle_t h_spi0;

static spi_bus_config_t spi0_buscfg =
{
    .miso_io_num = SPI0_MISO_PIN,
    .mosi_io_num = SPI0_MOSI_PIN,
    .sclk_io_num = SPI0_CLK_PIN,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .flags = SPICOMMON_BUSFLAG_MASTER,
    .max_transfer_sz = 0 // Automatic max size
};

static spi_device_interface_config_t spi0_devcfg =
{
    .clock_speed_hz = 1 * 1000 * 1000, // Clock
    .mode = 0,                          // SPI mode 0
    .spics_io_num = SPI0_CS_PIN,        // CS pin control disabled
    .queue_size = 7,                    // We want to be able to queue 7 transactions at a time
    .pre_cb = NULL
};

static SemaphoreHandle_t spi0_mutex;
int spi0_init(void)
{
    int ret = ESP_OK;

    spi0_mutex = xSemaphoreCreateMutex();

    if (spi0_mutex == NULL) {
        return -1;
    }

    ret = spi_bus_initialize(SPI2_HOST, &spi0_buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);

    ret = spi_bus_add_device(SPI2_HOST, &spi0_devcfg, &h_spi0);
    ESP_ERROR_CHECK(ret);

    return ret;
}

int spi0_lock(void)
{
    return xSemaphoreTake(spi0_mutex, portMAX_DELAY);
}

int spi0_unlock(void)
{
    return xSemaphoreGive(spi0_mutex);
}

int spi0_tx_rx_16(uint16_t dtx, uint16_t *drx)
{
    int ret = 0;
    static spi_transaction_t t = {0};

    t.length = 2 * 8;
    t.rxlength = 0;
    t.tx_buffer = NULL;
    t.rx_buffer = NULL;
    t.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
    t.tx_data[0] = (dtx >> 8) & 0xFF;
    t.tx_data[1] = dtx & 0xFF;

    ret = spi_device_polling_transmit(h_spi0, &t);

    if(ret == ESP_OK && drx != NULL) {
        *drx = t.rx_data[1] | ((uint16_t) t.rx_data[0] << 8);
    }

    return ret;
}

int spi0_tx_rx_32(uint32_t dtx, uint32_t *drx)
{
    int ret = 0;
    static spi_transaction_t t = {0};

    t.length = 4 * 8;
    t.rxlength = 4 * 8;
    t.tx_buffer = NULL;
    t.rx_buffer = NULL;
    t.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
    t.tx_data[0] = (dtx >> 24) & 0xFF;
    t.tx_data[1] = (dtx >> 16) & 0xFF;
    t.tx_data[2] = (dtx >> 8) & 0xFF;
    t.tx_data[3] = (dtx) & 0xFF;

    ret = spi_device_polling_transmit(h_spi0, &t);

    if(ret == ESP_OK && drx != NULL) {
        *drx = (uint32_t) t.rx_data[3] |
               ((uint32_t) t.rx_data[2] << 8)  |
               ((uint32_t) t.rx_data[1] << 16) |
               ((uint32_t) t.rx_data[0] << 24);
    }

    return ret;
}

int spi0_tx_rx(uint8_t *p_tx, uint8_t *p_rx, size_t len)
{
    int ret = ESP_OK;
    spi_transaction_t t = {0};

    t.length = len * 8;
    t.rxlength = 0; // Auto - rx_len = length, rx_buffer can be NULL
    
    t.rx_buffer = p_rx; // Can be set to NULL
    t.tx_buffer = p_tx; // Can be set to NULL

    ret = spi_device_polling_transmit(h_spi0, &t);

    return ret;
}

int spi0_tx(uint8_t *ptx, size_t len)
{
    return spi0_tx_rx(ptx, NULL, len);
}

int spi0_rx(uint8_t *prx, size_t len)
{
    return spi0_tx_rx(NULL, prx, len);
}
