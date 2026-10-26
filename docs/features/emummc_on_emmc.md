# emuMMC on eMMC (raw partition)

This fork adds one feature to upstream Atmosphère: booting an emuMMC that is stored as a **raw partition on the console's internal eMMC** instead of on the SD card.

The feature is ported from [NaGaa95/Atmosphere-Pro](https://github.com/NaGaa95/Atmosphere-Pro). The original implementation is by lulle2007200 (Christoph Baumann), and its emummc module comes from [lulle2007200/emummc](https://github.com/lulle2007200/emummc) (`internal-emummc` branch). Only the eMMC-partition path was taken, and it was re-implemented as small, separate modules so the fork stays easy to rebase on upstream.

## Configuration

emuMMC is still configured in `sdmc:/emummc/emummc.ini`. The value `enabled=4` selects an emuMMC partition on the eMMC. This is the convention of [hekate-pro](https://github.com/NaGaa95/hekate-pro), also supported by the matching hekate fork ([postkevone/hekate](https://github.com/postkevone/hekate), branch `emmc-emummc`). Upstream hekate does **not** support it: it treats `enabled=4` like `enabled=1` (SD).

```ini
[emummc]
enabled=4
sector=0x<start sector of the partition>
id=0x0000
nintendo_path=emuMMC/<folder>/Nintendo
```

| Key | Meaning |
|---|---|
| `enabled` | `0` = off, `1` = emuMMC on SD (upstream behaviour, unchanged), `4` = emuMMC partition on eMMC (this fork) |
| `sector` | Start sector of the emuMMC partition inside the eMMC user area (GPP). Required when `enabled=4`. |
| `id` | emuMMC id, same as upstream. |
| `nintendo_path` | `Nintendo` folder on the **SD card** used by this emuMMC, same as upstream. If empty, `emummc/Nintendo_<id>` is used. |

The partition uses the usual raw emuMMC layout: BOOT0 (4 MiB), then BOOT1 (4 MiB), then the user area (GPP) image. Create the partition with hekate-pro: its partition manager shrinks sysNAND `USER` and adds the `emummc` partition after it (which passes the overlap check below), and its emuMMC tool writes `emummc.ini` with `enabled=4` and the right `sector`. To boot HOS through hekate's own launcher instead of `fusee.bin`, use the hekate fork, with `fss0` pointing at this fork's `package3`.

Upstream Atmosphère treats any non-zero `enabled` value as "SD emuMMC". With upstream, `enabled=4` would therefore try to boot an SD emuMMC at that sector. This fork handles the value correctly.

## Safety checks at boot (fusee)

Before booting an eMMC emuMMC, fusee reads the sysNAND GPT and stops with a fatal error if either of these is true:

- `sector` is at or before the end of the sysNAND `USER` partition. The emuMMC would overlap and corrupt sysNAND, so the error is "emuMMC partition … overlaps sysNAND!".
- The partition's two boot areas (8 MiB) do not fit inside the eMMC. The error is "… is outside of the eMMC!".

Atmosphere-Pro does not have these checks.

## How it works

The SD card is not redirected or changed in any way. Only eMMC accesses are redirected.

1. **fusee (bootloader)**: reads `emummc.ini`. When it sees `enabled=4`, it sets the secure-monitor emuMMC type to `EmummcType_PartitionEmmc` (value `3`) with the start sector. It then reads package1/package2 from the emuMMC partition on the eMMC instead of from sysNAND.
2. **exosphere (secure monitor)**: stores the configuration and returns it, including the start sector, through the `smcAmsGetEmummcConfig` SMC. Type values `0`/`1`/`2` keep their upstream meaning, so the config struct layout is unchanged. Value `3` is the value Atmosphere-Pro uses for "partition on eMMC".
3. **emummc KIP (inside FS)**: for type `3`, every eMMC read or write from FS is served by the KIP's built-in hekate SDMMC driver on the eMMC controller (SDMMC4):
   - The driver starts on first use. It tries HS400 first, then falls back to HS200, HS52 8-bit and HS52 1-bit.
   - Each sector is mapped to `sector + start_sector + active_partition_index * 0x2000`. The 0x2000 multiplier means FS's BOOT0, BOOT1 and GPP map to the three areas inside the emuMMC partition.
   - The card is switched to the GPP for the access, then switched back to the physical partition FS has selected (`active_partition`). The driver's cached partition is resynced from `active_partition` on every request, because FS switches partitions on its own.
   - Accesses that would go past the end of the eMMC fail as read/write errors.
   - The nand-patrol offset gets the same bounds check as upstream's SD-partition mode.
   - SD card accesses stay on the KIP's hekate SD driver, the same way they already do in upstream's file-based mode.
4. **Sysmodules (stratosphere)**: no changes. `ams::emummc::IsActive()` treats every type other than `0` as "emuMMC active" and only type `2` as file-based, so type `3` is reported correctly. `ExosphereEmummcType` still returns `1`.

## Files

New files, which contain all of the feature logic:

| File | Purpose |
|---|---|
| `fusee/program/source/fusee_emummc_emmc.hpp/.cpp` | Parses `enabled=4`, fills the config, checks for sysNAND overlap, and builds the BOOT0/user storages on the eMMC. |
| `emummc/source/emuMMC/emummc_emmc.h/.c` | Runtime redirection of FS eMMC accesses to the eMMC partition. |
| `docs/features/emummc_on_emmc.md` | This document. |

Upstream files with small hook lines:

| File | Change |
|---|---|
| `libraries/libexosphere/include/exosphere/secmon/secmon_emummc_context.hpp` | `EmummcType_PartitionEmmc = 3` |
| `exosphere/program/source/smc/secmon_smc_info.cpp` | `case EmummcType_PartitionEmmc:` copies the partition config |
| `fusee/program/source/fusee_setup_horizon.cpp` | `on_emmc` flag in `ConfigureEmummc()`, plus an include |
| `fusee/program/source/fusee_emummc.cpp` | `EmummcType_PartitionEmmc` branch in `InitializeEmummc()`, plus an include |
| `emummc/source/emuMMC/emummc_ctx.h` | `emuMMC_EMMC_Raw` (value `3`) |
| `emummc/source/emuMMC/emummc.c` | Delegates in `emummc_read_write_inner()`; `emummc_emmc_finalize()` on eMMC controller close; an include |
| `emummc/source/main.c` | Loads the start sector for `emuMMC_EMMC_Raw` too |

## Updating from upstream

```sh
git fetch upstream
git rebase upstream/master   # or: git merge upstream/master
```

Conflicts can only happen at the hook lines listed above. The areas most likely to conflict:

- `ConfigureEmummc()` in `fusee_setup_horizon.cpp`, if upstream changes how `emummc.ini` is parsed.
- `InitializeEmummc()` in `fusee_emummc.cpp`, if upstream adds new emuMMC types.
- `SmcGetEmummcConfig` in `secmon_smc_info.cpp` and `enum EmummcType`. If upstream ever assigns value `3` itself, renumber `EmummcType_PartitionEmmc` and `emuMMC_EMMC_Raw` together, and update this document.
- `emummc/` is a git-subrepo of [m4xw/emummc](https://github.com/m4xw/emummc). Upstream syncs it with `git subrepo`, but for this fork it is ordinary files, and conflicts there are resolved like any other file.

## What was left out of Atmosphere-Pro

| Feature | Why it was left out |
|---|---|
| emuSD (redirecting the SD card to an eMMC/SD partition or files) | Not requested. It also required a second secmon config, larger structs (0x120 → 0x1A0), and a new SMC config item (65013). |
| File-based emuMMC on an eMMC FAT partition (`sys:`) | Only the raw partition was requested. |
| Booting Atmosphère files from eMMC ("boot-storage") | Not requested. |
| `memory_mode_auto` (exosphere.ini), extra PMC/TSENSOR registers, fatal payload, IRAM logging, `ams:bpc` fatal reporting | Not related to this feature. |

## Testing

1. Build: `docker build -t ams-build .`, then `docker run --rm -v "$PWD":/src -w /src ams-build make`.
2. Create an eMMC emuMMC partition with hekate-pro. Check that `emummc.ini` has `enabled=4` and the right `sector`.
3. Boot fusee. In emuMMC, change something visible (for example the console nickname) and confirm that sysNAND does not show the change.
4. Boot sysNAND and confirm it is unaffected.
5. Regression: SD partition (`enabled=1` + `sector`) and SD file-based (`enabled=1` + `path`) emuMMC still boot.
6. Negative: a `sector` that falls inside sysNAND must stop with the overlap fatal error.

## Licensing

Like the rest of Atmosphère, the new files are licensed under GPLv2 (see `LICENSE`). Per this fork's rules they contain no comment header, so their licensing and credits are recorded here: lulle2007200 (Christoph Baumann), NaGaa95, m4xw (emummc), CTCaer (hekate), and the Atmosphère team.
