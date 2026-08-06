// Host-test stub. Mirrors the signatures of the real header so the
// firmware can be compiled and exercised on a PC. Not used on device.
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct spi_inst spi_inst_t;
extern spi_inst_t *spi0;
extern spi_inst_t *spi1;
typedef enum { SPI_CPOL_0 = 0, SPI_CPOL_1 = 1 } spi_cpol_t;
typedef enum { SPI_CPHA_0 = 0, SPI_CPHA_1 = 1 } spi_cpha_t;
typedef enum { SPI_LSB_FIRST = 0, SPI_MSB_FIRST = 1 } spi_order_t;
struct spi_hw_t { volatile uint32_t cr0, cr1, dr; };
extern "C" {
uint32_t spi_init(spi_inst_t*, uint32_t);
void spi_set_format(spi_inst_t*, uint32_t, spi_cpol_t, spi_cpha_t, spi_order_t);
int  spi_write_blocking(spi_inst_t*, const uint8_t*, size_t);
bool spi_is_busy(spi_inst_t*);
uint32_t spi_get_dreq(spi_inst_t*, bool);
spi_hw_t* spi_get_hw(spi_inst_t*);
}
