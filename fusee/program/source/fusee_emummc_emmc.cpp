#include <exosphere.hpp>
#include "fusee_emummc_emmc.hpp"
#include "fusee_mmc.hpp"
#include "fusee_fatal.hpp"
#include "fusee_malloc.hpp"

namespace ams::nxboot {

    namespace {

        constexpr inline u32 EmmcEnabledValue = 4;

        constexpr inline s64 EmummcBootPartitionSize      = 4_MB;
        constexpr inline s64 EmummcBootPartitionsSizeTotal = 8_MB;

        constexpr inline const u16 SysNandUserPartitionName[] = { 'U', 'S', 'E', 'R', 0 };

        struct SysNandGptHeader {
            char signature[8];
            u32 revision;
            u32 header_size;
            u32 header_crc32;
            u32 reserved0;
            u64 my_lba;
            u64 alt_lba;
            u64 first_usable_lba;
            u64 last_usable_lba;
            u8  disk_guid[0x10];
            u64 partition_entry_lba;
            u32 number_of_partition_entries;
            u32 size_of_partition_entry;
            u32 partition_entry_array_crc32;
            u32 reserved1;
        };
        static_assert(sizeof(SysNandGptHeader) == 0x60);

        struct SysNandGptEntry {
            u8  partition_type_guid[0x10];
            u8  unique_partition_guid[0x10];
            u64 starting_lba;
            u64 ending_lba;
            u64 attributes;
            u16 partition_name[0x24];
        };
        static_assert(sizeof(SysNandGptEntry) == 0x80);

        struct SysNandGpt {
            SysNandGptHeader header;
            u8 padding[0x1A0];
            SysNandGptEntry entries[128];
        };
        static_assert(sizeof(SysNandGpt) == 16_KB + 0x200);

        u64 GetSysNandUserPartitionEnd(fs::IStorage &mmc_user_storage) {
            SysNandGpt *gpt = static_cast<SysNandGpt *>(AllocateAligned(sizeof(SysNandGpt), 0x200));

            if (const Result result = mmc_user_storage.Read(sdmmc::SectorSize, gpt, sizeof(*gpt)); R_FAILED(result)) {
                ShowFatalError("Failed to read sysNAND GPT: 0x%08" PRIx32 "\n", result.GetValue());
            }

            if (std::memcmp(gpt->header.signature, "EFI PART", 8) != 0) {
                ShowFatalError("Invalid sysNAND GPT signature\n");
            }

            const u32 num_entries = std::min<u32>(gpt->header.number_of_partition_entries, util::size(gpt->entries));
            for (u32 i = 0; i < num_entries; ++i) {
                if (std::memcmp(gpt->entries[i].partition_name, SysNandUserPartitionName, sizeof(SysNandUserPartitionName)) == 0) {
                    return gpt->entries[i].ending_lba;
                }
            }

            ShowFatalError("sysNAND USER partition not found\n");
        }

    }

    bool IsEmmcEmummcEnabledValue(const char *value) {
        if (value[0] < '0' || value[0] > '9') {
            return false;
        }

        u32 x = 0;
        for (const char *c = value; '0' <= *c && *c <= '9'; ++c) {
            x = x * 10 + static_cast<u32>(*c - '0');
        }

        return x == EmmcEnabledValue;
    }

    void ConfigureEmmcEmummc(secmon::EmummcConfiguration &cfg, u32 sector) {
        if (sector == 0) {
            ShowFatalError("Invalid emummc setting: eMMC emummc requires a sector!\n");
        }

        cfg.base_cfg.type               = secmon::EmummcType_PartitionEmmc;
        cfg.partition_cfg.start_sector = sector;
    }

    void InitializeEmmcEmummcStorages(fs::IStorage &mmc_user_storage, const secmon::EmummcConfiguration &cfg, fs::IStorage **out_boot0, fs::IStorage **out_user) {
        if (const Result result = InitializeMmc(); R_FAILED(result)) {
            ShowFatalError("Failed to initialize mmc: 0x%08" PRIx32 "\n", result.GetValue());
        }

        s64 emmc_size;
        if (const Result result = mmc_user_storage.GetSize(std::addressof(emmc_size)); R_FAILED(result)) {
            ShowFatalError("Failed to get emmc size: 0x%08" PRIx32 "!\n", result.GetValue());
        }

        const u64 start_sector = cfg.partition_cfg.start_sector;
        if (start_sector <= GetSysNandUserPartitionEnd(mmc_user_storage)) {
            ShowFatalError("emuMMC partition (sector 0x%" PRIx64 ") overlaps sysNAND!\n", start_sector);
        }

        const s64 partition_start = static_cast<s64>(start_sector * sdmmc::SectorSize);
        if (partition_start + EmummcBootPartitionsSizeTotal >= emmc_size) {
            ShowFatalError("emuMMC partition (sector 0x%" PRIx64 ") is outside of the eMMC!\n", start_sector);
        }

        *out_boot0 = AllocateObject<fs::SubStorage>(mmc_user_storage, partition_start, EmummcBootPartitionSize);
        *out_user  = AllocateObject<fs::SubStorage>(mmc_user_storage, partition_start + EmummcBootPartitionsSizeTotal, emmc_size - (partition_start + EmummcBootPartitionsSizeTotal));
    }

}
