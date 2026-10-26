#ifndef __EMUMMC_EMMC_H__
#define __EMUMMC_EMMC_H__

#include <stdbool.h>
#include <stdint.h>

bool emummc_emmc_is_active(void);
uint64_t emummc_emmc_read_write(void *buf, unsigned int sector, unsigned int num_sectors, bool is_write);
void emummc_emmc_finalize(void);

#endif
