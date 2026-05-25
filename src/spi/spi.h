#ifndef SPI_SPI_H_
#define SPI_SPI_H_

#include <stddef.h>
#include <stdint.h>

// SPI (CPOL, CPHA)
// - 0: (0, 0)
// - 1: (0, 1)
// - 2: (1, 0)
// - 3: (1, 1)
enum spi_modes {
    SPI_MODE_0,
    SPI_MODE_1,
    SPI_MODE_2,
    SPI_MODE_3
};

/**
 *  @addtogroup drivers
 * @{
 */
int spi0_lock(void);
int spi0_unlock(void);
int spi0_init(void);
int spi0_tx_rx(uint8_t *p_tx, uint8_t *p_rx, size_t len);
int spi0_tx(uint8_t *ptx, size_t len);
int spi0_rx(uint8_t *prx, size_t len);
int spi0_tx_rx_16(uint16_t dtx, uint16_t *drx);
int spi0_tx_rx_32(uint32_t dtx, uint32_t *drx);

/**
 * @}
 */

#endif
