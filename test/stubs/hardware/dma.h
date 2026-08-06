// Host-test stub. Mirrors the signatures of the real header so the
// firmware can be compiled and exercised on a PC. Not used on device.
#pragma once
#include <stdint.h>
#include <stddef.h>
enum dma_channel_transfer_size { DMA_SIZE_8=0, DMA_SIZE_16=1, DMA_SIZE_32=2 };
typedef struct { uint32_t ctrl; } dma_channel_config;
extern "C" {
int  dma_claim_unused_channel(bool);
dma_channel_config dma_channel_get_default_config(uint32_t);
void channel_config_set_transfer_data_size(dma_channel_config*, dma_channel_transfer_size);
void channel_config_set_dreq(dma_channel_config*, uint32_t);
void channel_config_set_read_increment(dma_channel_config*, bool);
void channel_config_set_write_increment(dma_channel_config*, bool);
void dma_channel_configure(uint32_t, const dma_channel_config*, volatile void*, const volatile void*, uint32_t, bool);
void dma_channel_wait_for_finish_blocking(uint32_t);
}
