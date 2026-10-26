#include "emummc_emmc.h"
#include "emummc.h"
#include "emummc_ctx.h"
#include "../emmc/sdmmc_driver.h"
#include "../utils/fatal.h"

enum
{
    EmmcMode_InitFail = 0,
    EmmcMode_1Bit_HS52,
    EmmcMode_8Bit_HS52,
    EmmcMode_HS200,
    EmmcMode_HS400,
};

extern volatile int *active_partition;

static sdmmc_t emmc_sdmmc;
static sdmmc_storage_t emmc_storage;
static bool emmc_initialized = false;
static bool emmc_nand_patrol_checked = false;

bool emummc_emmc_is_active(void)
{
    return emuMMC_ctx.EMMC_Type == emuMMC_EMMC_Raw;
}

static int _emmc_init_mode(u32 mode)
{
    u32 bus_width = SDMMC_BUS_WIDTH_8;
    u32 type = SDHCI_TIMING_MMC_HS400;

    switch (mode)
    {
    case EmmcMode_1Bit_HS52:
        bus_width = SDMMC_BUS_WIDTH_1;
        type = SDHCI_TIMING_MMC_HS52;
        break;
    case EmmcMode_8Bit_HS52:
        type = SDHCI_TIMING_MMC_HS52;
        break;
    case EmmcMode_HS200:
        type = SDHCI_TIMING_MMC_HS200;
        break;
    default:
        break;
    }

    return sdmmc_storage_init_mmc(&emmc_storage, &emmc_sdmmc, bus_width, type);
}

static void _emmc_set_partition(u32 partition)
{
    if (emmc_storage.partition == partition)
        return;

    if (!sdmmc_storage_set_mmc_partition(&emmc_storage, partition))
        fatal_abort(Fatal_InitMMC);
}

static void _emmc_nand_patrol_ensure_integrity(void)
{
    if (emmc_nand_patrol_checked)
        return;

    emmc_nand_patrol_checked = true;

    fs_nand_patrol_t nand_patrol;
    unsigned int nand_patrol_sector = emuMMC_ctx.EMMC_StoragePartitionOffset + NAND_PATROL_SECTOR;

    _emmc_set_partition(FS_EMMC_PARTITION_GPP);

    if (sdmmc_storage_read(&emmc_storage, nand_patrol_sector, 1, &nand_patrol) && nand_patrol.offset > emmc_storage.sec_cnt)
    {
        memset(&nand_patrol, 0, sizeof(fs_nand_patrol_t));
        sdmmc_storage_write(&emmc_storage, nand_patrol_sector, 1, &nand_patrol);
    }

    _emmc_set_partition(*active_partition);
}

static void _emmc_ensure_initialized(void)
{
    if (emmc_initialized)
        return;

    for (u32 mode = EmmcMode_HS400; mode != EmmcMode_InitFail; mode--)
    {
        if (_emmc_init_mode(mode))
        {
            emmc_initialized = true;
            break;
        }

        sdmmc_storage_end(&emmc_storage);
    }

    if (!emmc_initialized)
        fatal_abort(Fatal_InitMMC);

    _emmc_set_partition(*active_partition);
    _emmc_nand_patrol_ensure_integrity();
}

uint64_t emummc_emmc_read_write(void *buf, unsigned int sector, unsigned int num_sectors, bool is_write)
{
    _emmc_ensure_initialized();

    emmc_storage.partition = *active_partition;

    sector += emuMMC_ctx.EMMC_StoragePartitionOffset;
    sector += sdmmc_nand_get_active_partition_index() * BOOT_PARTITION_SIZE;

    _emmc_set_partition(FS_EMMC_PARTITION_GPP);

    uint64_t res = 0;
    if (__builtin_expect(sector + num_sectors <= emmc_storage.sec_cnt, 1))
    {
        if (!is_write)
            res = sdmmc_storage_read(&emmc_storage, sector, num_sectors, buf);
        else
            res = sdmmc_storage_write(&emmc_storage, sector, num_sectors, buf);
    }

    _emmc_set_partition(*active_partition);

    return res;
}

void emummc_emmc_finalize(void)
{
    emmc_initialized = false;
}
