# Galaxy A5 2017 (a5y17lte, Exynos 7880): evidence log

> **Direction change (2026-10-18):** the XNU/iOS investigation (sections 1–8) is closed. The project now targets **mainline Linux / postmarketOS** booted by mainline U-Boot (sections 13+). The hardware facts in sections 9–12 apply unchanged.

Every claim below was checked against source on 2026-10-18. Sources are in `upstream/`.

| Source | Version |
|---|---|
| Apple XNU | `apple-oss-distributions/xnu`, tags xnu-4570.1.46, xnu-6153.141.1, xnu-8792.81.2, xnu-12377.121.6 |
| UniLoader | `ivoszbg/uniLoader` @ bdc0f6d (2026-10-13) |
| Device kernel | `Exynos7880-Linux/android_kernel_samsung_universal7880`, branch hybris-17.1 (DTS + configs only) |

## 1. XNU has a Cortex-A53 target (BCM2837 / Raspberry Pi 3)

| Tag | Files mentioning BCM2837 |
|---|---|
| xnu-4570.1.46 (first public ARM64) | 0 |
| xnu-6153.141.1 | 11 |
| xnu-8792.81.2 | 10 |
| xnu-12377.121.6 (latest) | 0 — removed |

In xnu-8792.81.2:

- `makedefs/MakeInc.def:32` sets `SUPPORTED_ARM64_MACHINE_CONFIGS = BCM2837 T6000 T8101 T8103 VMAPPLE`.
- `pexpert/pexpert/arm64/board_config.h:90` defines a BCM2837 board: 4 CPUs, 1 cluster, KPC off.
- `pexpert/arm/pe_serial.c:493` contains the `PI3_UART` mini-UART driver.
- `osfmk/arm64/start.s:689` routes the BCM2837 timer interrupt to FIQ before the MMU is turned on.
- `#ifndef BCM2837` skips lines exist in `osfmk/kern/startup.c:729` and `iokit/Kernel/IOStartIOKit.cpp:81`.

**Conclusion:** use xnu-8792.81.2 as the base. It is the newest release that still has the A53 board path.

## 2. XNU already speaks the Samsung UART

`pe_serial.c` (`APPLE_UART`) programs `rULCON0` and `rUTRSTAT0`, the Samsung s3c/exynos UART register layout. It binds to the device-tree compatible string `"uart-1,samsung"` (line 877) when `enable-legacy-serial` is set.

The 7880 has two UARTs, both `samsung,exynos-uart`:

- `uart@13810000` (exynos7880.dtsi:335)
- `uart@13820000` (exynos7880.dtsi:348)

**Conclusion:** the serial driver should mostly work as-is once the device tree points at 0x13820000. This still needs proving on hardware.

## 3. The interrupt controller does NOT match — main porting work

- **Exynos 7880:** GICv2 (`arm,cortex-a15-gic`) at `0x106E1000` (distributor) and `0x106E2000` (CPU interface). Source: exynos7880.dtsi:270.
- **XNU:** the only GIC code is GICv3, under `HAS_GIC_V3` for VMAPPLE (`pexpert/arm/pe_fiq.c:15`, `VMAPPLE.h:40`). It uses redistributors, which GICv2 does not have.
- **XNU interrupt-controller drivers:** only abstract classes and examples ship (`IOInterruptController.h`, `PassthruInterruptController.h`, `Examples/drvGenericInterruptController`). Apple's real AIC driver is closed source.

**Required work:**

- Write a GICv2 backend for `pe_fiq.c`, which handles timer FIQs.
- Write a GICv2 `IOInterruptController` for device IRQs.
- **Risk:** Samsung's EL3 monitor may own GIC Group 0 (FIQ). If so, the timer must go through IRQ instead. Verify on hardware.

## 4. Timer, CPUs, power

- **Timer:** `arm,armv8-timer` at 26 MHz, using PPIs 13/14/11/10. XNU arm64 already uses the generic timer. Set `timebase-frequency` = 26000000 in the Apple device tree.
- **CPUs:** 8× `arm,cortex-a53` in two clusters (cpu@0..3 and cpu@100..), started via `enable-method = "psci"` with `method = "smc"`. BCM2837 is 4 CPUs in 1 cluster, so for the first boot use one CPU only. SMP later needs PSCI CPU_ON.

## 5. Memory and display

- **RAM:** `0x40000000` size `0x3E400000`, plus `0x80000000` size `0x80000000` (a5y17lte_common.dtsi:21,26). The gap at 0x7E400000 is reserved by firmware.
- **Display:** DECON at `0x14830000` (exynos7880.dtsi:1406), the same base UniLoader already uses for a3xelte (Exynos 7580). The A5 framebuffer is 1080×1920. Its physical address is unknown; read it from DECON window registers at runtime or from the device.

## 6. UniLoader

- Boots Linux only. A search for xnu, mach-o, boot_args and darwin found nothing.
- Supported Exynos SoCs: 7420, 7570, 7885, 8895, 9610, 9810, 990. There is no 7880 SoC or board.
- `board/samsung/board-a3xelte.c` is a close template: same DECON base and the same simplefb approach.

**Required work:**

- Add an Exynos 7880 SoC and an a5y17lte board.
- Add an XNU payload path: load the Mach-O, build `boot_args` (with Video = framebuffer), build an Apple-format flattened device tree from the facts above, then jump.

## 7. Samsung firmware

`hybris-a5y17lte_defconfig` has `CONFIG_TIMA_LKMAUTH=y` but no `CONFIG_UH*` or `CONFIG_RKP*`. That suggests no EL2 hypervisor sits in the way of a non-Linux kernel. Unconfirmed until tested on hardware.

## 8. iOS 7.1.2 kernelcache (experiment track)

- Not verified yet. That needs the iPhone 5s IPSW and reverse engineering.
- The expected blockers, still unconfirmed:
  - an Apple-format device tree from iBoot,
  - AIC-only interrupt handling inside closed kexts,
  - Cyclone implementation-defined system registers, which trap on a Cortex-A53.

## 9. Boot image and framebuffer facts used in step 1

- **Boot image packaging.** Mainline U-Boot packages for a5y17lte with:
  - `mkbootimg --base 0x40000000 --kernel_offset 0 --ramdisk_offset 0x01000000 --tags_offset 0x100 --pagesize 2048 --second_offset 0xf00000`
  - no dt.img
  - its docs say stock S-Boot ignores the offsets (docs.u-boot.org, board/samsung/axy17lte)
- **UART.** U-Boot's `exynos78x0.dtsi` uses UART2 at `0x13820000` (`samsung,exynos4210-uart`), matching section 2.
- **Framebuffer at `0x67000000`.** Every sibling Exynos S-Boot of this generation uses it:
  - uniLoader `board-j4lte.c` (7570) and `board-a3xelte.c` (7580)
  - mainline `exynos7870-j5y17lte.dts` (2026-03 patch)
  - U-Boot `exynos7870-on7xelte` (1080×1920, same as the A5)

  The 7870 J5 also has the identical DRAM bank `0x40000000 + 0x3e400000`.

  **Not yet confirmed on the A5.** The step 1 image prints the IDMA base-address registers to settle it.
- **DECON trigger.** `HW_SW_TRIG_CONTROL` is at offset `0x70` (`decon_7880/regs-decon.h:92`), the same as 7570/7885, so the same `0x1281` write is used.

## 10. Step 1 results (emulated, 2026-10-18)

- **Test setup.** The real `out/a5y17lte-step1-boot.img` runs in Unicorn 2.1.4 (QEMU CPU core, Cortex-A53 model).
- **Results:**
  - The image self-relocates to `0x47000000` and halts in `wfe` after 35.0M instructions.
  - Tested at load addresses `0x40000000`, `0x40080000` and `0x40001000`. The framebuffer is byte-identical in all three.
- **MMIO touched:** only DECON `0x14830070` (write `0x1281`), DECON WIN0-3, and IDMA0-5 `+0x74`. No other hardware.
- **Not proven by emulation:** real S-Boot entry state, real register values, and real scan-out. That is the phone test.

## 11. Hardware result 1 (phone, 2026-10-18 02:27)

**Result.** After flashing `a5y17lte-step1-boot.img`, the Samsung "Galaxy A5 / powered by android" logo stayed on screen for more than 2 minutes. There was no uniLoader text.

**Explanation.**

- The panel is **MIPI command mode with hardware TE trigger** (`exynos7880.dtsi` decon_f: `psr_mode = <2>`, `trig_mode = <0>`).
- In command mode the panel keeps its own frame memory and only updates when DECON is triggered (`decon_reg_7880.c:611` `decon_reg_set_trigger`).
- Step 1 wrote `0x1281` once, before drawing, and never triggered again. So any drawing could not reach the panel.

**Inconclusive.** This does not show whether the code ran.

**Fix (step 1b):**

- DECON is put in software trigger, with `TRIG_AUTO_MASK` and one `SW_TRIG` frame per printk line.
- Then the board reboots via PMU `SWRESET` (`0x106B0400` = 1, from `S5E7880-pmusfr.h:161` and `exynos-reboot.c:72,299`) after 30 s.
- A self-reboot proves execution even with no picture.

**Emulated:** 333 frames fired, all text drawn, the reset write reached, and no other MMIO touched.

## 12. Hardware result 2: step 1b WORKS on the phone (2026-10-18)

Photo: `evidence/hw-step1b-2026-10-18.jpg`. uniLoader built 02:28:46 (commit 79cdfbe) ran on the real a5y17lte. All text and the R G B W bars are visible.

**Display and loader**

| Reading | Meaning |
|---|---|
| Text visible on screen | S-Boot runs a raw uniLoader from the boot partition. The command-mode trigger fix works. |
| Bars appear in R G B W order | The framebuffer is `0x67000000`, 1080×1920, ARGB8888. **Confirmed.** |
| DECON TRIG = `0x1000` | The SW_TRIG bit self-clears after a frame. |
| WIN0 = `0xffff0121` (enabled); WIN1-3 = `0x300` (off) | Only window 0 is in use. |
| IDMA1 (`0x14822000`) ADDR_Y0 = `0x67000000`; IDMA0 = 0 | S-Boot scans out through **IDMA G1**, not G0. |

**CPU state at hand-off**

| Register | Value | Meaning |
|---|---|---|
| CurrentEL | **EL1** | We are handed control at EL1, the level XNU's `start.s` expects. EL2 is not available to us. |
| MIDR | `0x410fd034` | Cortex-A53 r0p4. |
| MPIDR | `0x80000000` | Boot CPU is cluster 0, core 0. |
| **CNTFRQ_EL0** | **0** | Firmware never set it, and EL1 cannot. XNU must take the 26 MHz timer from the device tree (`timebase-frequency`, `pexpert/arm/pe_identify_machine.c:85`), never from CNTFRQ. |
| DAIF | `0x2c0` | D, I and F masked; SError unmasked. |
| SCTLR_EL1 | `0x30c5183c` | MMU off (M=0); C and I bits set. Leftover cache state from S-Boot means XNU's entry must clean/invalidate before enabling the MMU. |
| TCR_EL1 | `0x0000002000000020` | Leftover from S-Boot; ignored because the MMU is off. |
| VBAR_EL1 | `0x43e00800` | S-Boot's vectors are at 0x43e00000. XNU and its device tree must not be placed over that region before XNU installs its own vectors. |
| ID_AA64PFR0 | `0x2222` | EL0-3 implemented. GIC field = 0, so no GICv3 system registers. **This confirms a memory-mapped GICv2 only** (section 3). |
| ID_AA64MMFR0 | `0x1122` | 40-bit physical addresses, 16-bit ASIDs. **TGran16 = 0, so the 16 KB page granule is unsupported**; only 4 KB and 64 KB work. |

**Page size check in XNU 8792**

- 16 KB pages come from `__ARM_16K_PG__`, defined only in `H13.h:73` and `VMAPPLE.h:44`.
- `BCM2837.h` does not define it, so the A53 target uses `ARM_PGSHIFT 12` (4 KB, `proc_reg.h:1149-1152`). This matches the hardware.
- The iOS 7.1.2 (A7) kernel also uses 4 KB pages.

**Reboot (confirmed by Jimmy, 11:37)**

- The phone reboots by itself about every 30 s and loops back into step 1b.
- This shows that PMU `SWRESET` (`0x106B0400` = 1) works from EL1.
- It also shows that the physical counter runs at about 26 MHz, so the 26 MHz timer from the device tree is correct.

## 13. Mainline Linux, milestone 1: sources and decisions (2026-10-18)

**Base versions:** Linux **v7.2** (8d3ae5928, 2026-08-16), U-Boot **v2026.10** (55084065).

**What mainline already has.** In v7.2 the 7870 sibling has:

- `exynos7870.dtsi`, `clk-exynos7870.c` and 7870 pinctrl
- five phones: a2corelte, j5y17lte, j6lte, j7xelte, on7xelte

There is no 7880 support yet.

**7880 compared with 7870.** Taken from the Samsung `pwrcal S5E78x0-sfrbase.h` files and the DTS.

| Block | Status |
|---|---|
| CMU PERI, FSYS, G3D, DISPAUD, ISP, MFCMSCL, CPUCL0/1 | Same addresses |
| UARTs, MMC, USB, I2C, DECON, pinctrl ALIVE/FSYS/TOUCH | Same addresses |
| CMU_MIF | 7870 has one at 0x10460000; 7880 has CMU_MIF0 at 0x10450000, CMU_MIF1 at 0x10550000 and CMU_CCORE at 0x10680000 |
| GIC | 0x104E1000 on 7870 vs **0x106E1000** on 7880 |
| PMU | 0x10480000 on 7870 vs **0x106B0000** on 7880 |
| MIF pinctrl | 0x10530000 on 7870 vs 0x10630000 on 7880 |

A full port needs a 7880 clock-driver variant. Milestone 1 needs no clocks.

**Cross-checks against independent sources:**

- **CNTFRQ.** Mainline `exynos7870.dtsi` hard-codes `clock-frequency = <26000000>` with the comment "Non-updatable, broken stock Samsung bootloader does not configure CNTFRQ_EL0". This matches our hardware reading CNTFRQ_EL0 = 0 (section 12).
- **Reboot modes.** On the 7880, Samsung `sec_ext.h` puts INFORM3 at PMU+0x80C, and `sec_reboot.c` uses prefix 0x12345670 with the codes download=1, recovery=4, bootloader=0xd. These are identical to the mainline 7870 `syscon-reboot-mode` values.

**Display.** The panel is MIPI command mode (`psr_mode = <2>`, `trig_mode = <0>`).

- Samsung `decon_reg_configure_trigger` (decon_reg_7880.c:384) sets HW_TRIG_EN, and `decon_reg_set_trigger` unmasks HW_TRIG_MASK.
- So the init writes HW_SW_TRIG_CONTROL = 0x10 (HW trigger, unmasked, no auto-mask). This is a **hypothesis to be confirmed on the phone**.
- The fallback is step 1b's software trigger (0x1000 then 0x1100) through /dev/mem.

**Memory.** Taken from Samsung `exynos7880-rmem.dtsi`.

- Bank 0 is 0x40000000 + 0x3E400000; bank 1 is 0x80000000 + 0x80000000.
- 0x7E400000–0x80000000 is secure DRAM.
- M1 marks every Samsung carve-out no-map, plus 0x67000000–0x7E400000 (framebuffer, modem, gnss and ION).

**U-Boot facts (v2026.10):**

- **DRAM banks.** `board/samsung/common/board.c` sizes DRAM as 12×256 MB from 0x40000000 using `get_ram_size()`.
- **Memory fixup.** U-Boot rewrites the kernel's `/memory` node to those banks, **including the secure hole**. M1's DTS therefore lists `secure-dram@7e400000` as no-map explicitly. This was confirmed in emulation: the fixed-up `/memory` has 12 contiguous banks, and every no-map node survives.
- **Boot command.** `bootcmd=source $prevbl_initrd_start_addr:bootscript` (include/configs/exynos78x0-common.h). S-Boot's ramdisk carries a FIT that contains the boot script.
- **Load address.** `a5y17lte_defconfig` is linked at 0x40001000 and is **not** position-independent. S-Boot's real load address is unknown, so M1 enables `CONFIG_POSITION_INDEPENDENT` ("almost any 4K aligned address").
- **Toolchain.**
  - **ld.lld cannot link U-Boot's arm64 image.** `arch/arm/cpu/armv8/u-boot.lds` places `.bss ADDR(.rela.dyn) (OVERLAY)`. ld.lld put `__bss_start` at 0x0, so U-Boot computed `Monitor len: c00086d0`, reserved about 3 GB, and wrote below RAM. This was found in emulation with a DEBUG build.
  - **GNU ld fixes it.** The xPack aarch64-none-elf GCC 14.2.1-1.1 (ld 2.43.1, sha256 a2c9731f…b117, matching the release .sha) gives `__bss_start` 0x40067a60 and `__bss_end` 0x40071120.

## 14. Milestone 1 build and emulation (2026-10-18)

**Image.** `out/a5y17lte-mainline-m1-boot.img` contains:

- **kernel:** u-boot.bin (491 KB)
- **ramdisk:** FIT (5.8 MB) holding the Linux 7.2 Image (6,078,472 B, sha256 33ebee32…), the DTB, the initramfs (nolibc init) and the boot script

**Emulation** (`tools/emulate_uboot.py`, log in `out/emulated-uboot-m1.log`):

- U-Boot was started at 0x40080000, 0x40001000 and 0x40000000, with the FIT at 0x41000000 and at 0x5A000000.
- In every combination U-Boot:
  - printed `DRAM: 3 GiB`
  - ran the FIT boot script and verified all sha256 hashes
  - loaded the kernel at 0x48000000 (`ARMd` magic), the initrd at 0x4FFFF000 and the FDT at 0x4FFFB000
  - jumped to Linux with x0 = the FDT

**Limit.** Unicorn cannot follow Linux past `__enable_mmu`: it raises an unhandled prefetch abort (exception type 3). Linux itself is therefore unverified until the phone test.

**What the phone test shows:**

- Text appearing before t = 15 means the TE hardware refresh works.
- Text appearing only at t = 15 means the software refresh works and TE does not.
- The phone landing in TWRP at t = 180 means the reboot-mode node works.
- If the screen shows nothing and the phone never reboots, Linux did not reach init.

## 15. Hardware result: mainline milestone 1 BOOTS on the phone (2026-10-18)

**Test.** Image `a5y17lte-mainline-m1-boot.img` (sha256 e51aeb39…cb778e). Photos:

- `evidence/hw-mainline-m1-phaseA-2026-10-18.jpg`
- `evidence/hw-mainline-m1-phaseB-2026-10-18.jpg`

A video was also taken.

| Observation | Meaning |
|---|---|
| S-Boot loaded U-Boot and the FIT, and Linux ran | The whole boot chain works on hardware. |
| `CPU: All CPU(s) started at EL1` | All CPUs started at EL1. |
| `cpus : 0-7` | All 8 Cortex-A53 cores came up via PSCI `smc`, both clusters. |
| `Memory: 2596712K/3145728K available (… 544660K reserved)`; MemTotal 2602468 kB | The no-map carve-outs are honoured. |
| `/soc@0/interrupt-controller@106e1000` probed | GIC at 0x106E1000 works. |
| `clocksource: Switched to clocksource arch_sys_counter` | The 26 MHz timer from the DT works. |
| `simple-framebuffer 67000000.framebuffer: fb0`, format a8r8g8b8, 1080x1920x32, linelength 4320; `Console: … 67x60` | The boot framebuffer works as a Linux console. |
| `Run /init as init process` | Userspace reached; no panics or oopses on screen. |
| **Phase A counter visible at t = 9 s**, before the software refresh started | **The panel refreshes by itself** with DECON HW_SW_TRIG_CONTROL = 0x10 (HW/TE trigger, unmasked, no auto-mask). The hypothesis in section 13 is **confirmed**. |
| At t ≈ 180 the phone **rebooted straight into TWRP** (Jimmy, 2026-10-19 00:27) | `syscon-reboot` (PMU+0x400) and `syscon-reboot-mode` (INFORM3 = 0x12345674) both work under Linux. |

**Still open:** the exact load addresses S-Boot uses (TWRP readings not taken yet). They didn't matter here, because position-independent U-Boot coped.

## 16. Milestone 2: keys (built and tested 2026-10-19)

**7880 pin control.** Taken from the Samsung universal7880 `drivers/pinctrl/samsung/pinctrl-exynos.c`, `exynos7880_pin_banks0..7`.

- The bank types are identical to the 7870's: `bank_type_5` for ALIVE, `bank_type_4` for the rest.
- Differences from the 7870:

| Controller | Difference |
|---|---|
| ALIVE (0x139F0000) | Adds `gpa3` (5 pins, 0x0a0, wake-up EINT 0x0c). `etc0`, `etc1` and `gpq0` have no EINT. |
| TOP | Adds `gpd5` (5 pins, 0x180, EINT 0x30). |
| Controller 1 | CCORE (gpm0, 0x10630000) replaces the 7870's MIF. |

- **Kernel patches** in `mainline/patches/`, applied to v7.2 on branch `a5y17lte`:
  1. `dt-bindings: pinctrl: samsung: add exynos7880 compatibles`
  2. `pinctrl: samsung: add Exynos7880 support` (full 8-controller table)

  Both pass `checkpatch.pl`; the only flag is the intentional missing Signed-off-by. They follow `Documentation/process/coding-assistants.rst`: an `Assisted-by:` tag, and no AI sign-off.
- **Wake-up interrupts.** Taken from the Samsung `exynos7880.dtsi` pinctrl_0 node:
  - SPI 0–15 serve as the per-pin interrupts for gpa0 and gpa1
  - SPI 16 is the muxed wake-up interrupt for gpa2 and gpa3

  This matches the mainline 7870 layout.
- **Keys.** Taken from the Samsung `exynos7880-a5y17lte_common.dtsi`:
  - Power gpa0-0 (KEY_POWER 116)
  - Volume Down gpa2-1 (114)
  - Volume Up gpa2-0 (115)
  - Home gpa1-7 (KEY_HOMEPAGE 172)
  - All pins use function 0xF (EINT), pud 0, drv 0.

  These match mainline `exynos7870-j5y17lte.dts`.
- **Checks so far:**
  - The DTB builds with W=1 and no warnings.
  - The emulated U-Boot path reaches Linux with an 8 KB FDT that contains the pinctrl node.
- **Phone test:**
  - The screen should list `input: event0 = gpio-keys`.
  - Each key should print pressed and released.
  - Holding Power for 3 s should reboot into TWRP.

### 16a. Hardware result (2026-10-19 00:48)

Photos: `evidence/hw-mainline-m2-{boot,keys,twrp}-2026-10-19.jpg`.

**Working:**

- `input: gpio-keys as /devices/platform/gpio-keys/input/input0`; init lists `event0 = gpio-keys`.
- **Power** (gpa0-0, per-pin SPI 0): 9 presses, each pressed and released.
- **Volume Up** (gpa2-0, muxed SPI 16): 5 presses.
- **Volume Down** (gpa2-1, muxed SPI 16): 4 presses.

  So both interrupt paths work: the per-pin GIC SPI and the muxed wake-up EINT.
- **Holding Power for 3 s rebooted into TWRP** (photo).
- All 8 CPUs online; MemTotal 2602388 kB.

**Not seen:**

- **Home** (gpa1-7, SPI 15). No Home events appear in the photo, and it isn't yet known whether Home was pressed.

**Kernel warning:**

- `samsung-pinctrl 139f0000.pinctrl: Failed to create device link (0x180) with supplier soc@0 for /soc@0/pinctrl@139f0000/wakeup-interrupt-controller`
- This is fw_devlink. It is non-fatal: the wake-up interrupts work, as Volume Up/Down prove.

**Cosmetic:**

- `^[[26~` echoes before Volume Down lines.
- Cause: the VT keyboard turns the key into an escape sequence, and the console tty echoes it.
- Fix: turn off tty echo in init.

## 17. Milestone 3: eMMC (read-only), PASSED 2026-10-19

**Same block as the 7870.** In the Samsung universal7880 kernel, the 7880 dwmmc0 and the 7870 dwmmc0 share:

- the driver (`samsung,exynos-dw-mshc`)
- the address 0x13540000 and interrupt SPI 245
- the clock IDs (310, 313, 602)
- `clock-frequency` 800 MHz and `ciu-div` 3
- the DDR timing

The only difference is the SDR drive phase: 2 on the 7880, 4 on the 7870.

**Timing conversion:**

- Samsung's driver reads `<div fine-drive drive sample>` (`dw_mmc-exynos.h:145`).
- Mainline reads `<sample drive>` (`dw_mmc-exynos.c:401–413`).
- Converting Samsung's 7870 values reproduces mainline `exynos7870-j5y17lte`'s `<0 4>`/`<2 4>` exactly, which confirms the mapping.
- So the A5 gets SDR `<0 2>` and DDR `<2 4>`.

**Kernel patch 0003.** `dt-bindings: mmc: samsung,exynos-dw-mshc: add exynos7880`, with `exynos7870-dw-mshc-smu` as the fallback. It passes checkpatch, and no driver change is needed.

**Temporary clocks:**

- There is no 7880 CMU driver yet, so the `biu` and `ciu` clocks are fixed clocks (200 and 800 MHz).
- S-Boot leaves the eMMC clocks running, since it loads the boot image from this eMMC.
- 800 MHz is Samsung's maximum CIU rate, so if the real rate is lower the card runs *slower* than requested. That is the safe direction.
- To limit risk further: 52 MHz high-speed SDR only, with no DDR, HS200 or HS400 (those need tuning plus the 1.8 V regulator), and no pinctrl or regulator handling.

**Data safety:**

- The init sets `/dev/mmcblk0` read-only (`BLKROSET`) before reading anything.
- It only reads LBA 1 (the GPT signature) and the sysfs partition names.
- No filesystems are mounted.

**Expected on screen:**

- `emmc : mmcblk0 set READ-ONLY`
- the card name with about 29,800 MiB (32 GB eMMC)
- `GPT header OK`
- a list of Samsung partition names (BOOT, RECOVERY, SYSTEM, USERDATA and so on)

### 17a. Hardware result for M3 (2026-10-19 00:58)

Photo: `evidence/hw-mainline-m3-emmc-2026-10-19.jpg`. After pressing Power, the phone returned to TWRP, so the hold-Power exit still works.

**Working:**

- `mmcblk0: mmc0:0001 BJNB4R 29.1 GiB` (the init shows `BJNB4R, 29820 MiB`).
- `mmcblk0boot0`, `mmcblk0boot1` (4 MiB each) and `mmcblk0rpmb` all registered.
- The read-only lock took effect (`mmcblk0 set READ-ONLY`).
- So command transfers work, and so does the EXT_CSD data read: the size comes from EXT_CSD SEC_COUNT. That was at about 300 kHz.

**Failing:**

- `mmc0: error -110 whilst initialising MMC card` on the first attempt; the retry succeeded.
- At HS 52 MHz (`Bus speed = 200000000Hz (req 52000000Hz, actual 50000000HZ div = 2)`):
  - `switch to bus width 8 failed` and `switch to bus width 4 failed`
  - every data read gives `I/O error ... sector 0`
  - `unable to read partition table`
- The init reports `read of LBA 1 FAILED` and `0 partitions`.

**Interpretation:**

- The data path works slowly and fails at 50 MHz.
- With fixed clocks the driver assumes a 200 MHz `cclk_in` and divides by 2 in CLKDIV. Mainline Exynos normally sets the CIU clock to exactly `wanted × (ciu_div + 1)` (`dw_mci_exynos_adjust_clock`) and leaves CLKDIV bypassed. So the drive/sample phase settings are applied under conditions the real driver never uses.
- **The real CIU rate is unknown.**

**Samsung's clock path for MMC0.** Taken from `S5E7880-cmu.c:308,400,545` and `-cmusfr.h`:

- The mux is CMU_CCORE+0x288, bit 12, selecting BUS_PLL/2 or MEDIA_PLL/2.
- The divider is CMU_CCORE+0x488, bits 0–9.
- PLL_CON0 is at +0x140 (BUS) and +0x120 (MEDIA).
- The pll141xx formula is f = 26 MHz × M / (P << S), with M in bits 16–25, P in bits 8–13, S in bits 0–2, and enable in bit 31.

**Next build: M3b (diagnostic, still read-only):**

- It shows the real MMC0 CIU rate from those registers.
- It sweeps the card clock through debugfs (0.4 to 52 MHz) and does an uncached 64 KiB read plus a GPT check at each step.
- It keeps the fastest passing rate and lists the partitions.

### 17b. Hardware result for M3b (2026-10-19 01:08)

Photos: `evidence/hw-mainline-m3b-{clock,keys}-2026-10-19.jpg`.

**Real eMMC clock** (CMU_CCORE registers read on the phone):

| Register | Value | Meaning |
|---|---|---|
| BUS_PLL_CON0 | `0xa0f60400` | M=246, P=4, S=0 → **1599.0 MHz** |
| MEDIA_PLL_CON0 | `0xa0cd0400` | M=205, P=4, S=0 → 1332.5 MHz |
| MMC0 mux | `0x00200000` | bit 12 = 0, so BUS_PLL/2; bit 21 is the mux gate enable |
| MMC0 div | `0x1` | ÷2 |

- So the **MMC0 CIU is 399.75 MHz**, not the 800 MHz assumed in M3.
- Every card clock in M3 was therefore half the requested one: slower, which is the safe direction.
- The DT fixed clock is now 399750000.

**Clock sweep:**

- `0.4:FAIL 1.0:FAIL 5.0:FAIL 10.0:FAIL 20.0:FAIL 26.0:FAIL 40.0:FAIL 52.0:FAIL`, giving `no frequency passed`.
- **Sector reads fail regardless of clock.**
- Card identification and the EXT_CSD read still work (BJNB4R, 29820 MiB).

**Keys:** **Home works** (code 172). With this, all four keys are confirmed on hardware. Holding Power for 3 s still reboots into TWRP.

**Suspect: the SMU sector-access policy.**

- Mainline `dw_mci_exynos_config_smu` writes `MPSCTRL0 = SECURE_WRITE | NON_SECURE_READ | NON_SECURE_WRITE | VALID` (0x71), which leaves out SECURE_READ.
- Samsung's `DWMCI_MPSCTRL_BYPASS` (`dw_mmc-exynos.h:212`) sets all four access bits plus VALID (0xF1).
- If the 7880 FSYS DMA master is secure, 0x71 blocks every sector *read*. EXT_CSD reads are not sector-checked, which fits the observations.
- **M3c** shows the SMU registers, writes 0xF1, and repeats the sweep.

### 17c. Hardware result for M3c: eMMC READS WORK (2026-10-19 01:15)

Photos: `evidence/hw-mainline-m3c-{smu,keys}-2026-10-19.jpg`.

**SMU registers:**

| | SEC | CTRL0 | BEGIN0 | END0 |
|---|---|---|---|---|
| found | `0xdfe26492` | `0x00000071` | `0x00000000` | `0xffffffff` |
| after the Samsung bypass write | `0xdfe26492` | `0x000000f1` | `0x00000000` | `0xffffffff` |

- The found value is exactly mainline's write (0x71, no SECURE_READ).
- The bypass write from EL1 took effect.

**Clock sweep:** **0.4, 1, 5, 10, 20, 26, 40 and 52 MHz all pass**. The init used 52 MHz and rescanned the partitions.

**eMMC:**

- `GPT header OK ("EFI PART" in LBA 1)`
- **25 partitions:** BOTA0 BOTA1 EFS CPEFS m9kefs1 m9kefs2 m9kefs3 CARRIER PARAM BOOT RECOVERY OTA RADIO TOMBSTONES DNT PERSISTENT STEADY RESERVED2 SYSTEM CACHE HIDDEN CP_DEBUG NAD_FW NAD_REFER USERDATA

**Clock:** the kernel log shows `Bus speed = 99937500Hz (req 400000Hz, actual 399750HZ div = 125)`. That is the measured 399.75 MHz CIU divided by `ciu-div + 1` = 4, so the fixed-clock value is consistent.

**Keys:** all four (Power, Volume Up/Down, Home) work again.

**Root cause, confirmed.** On the Exynos7880 the eMMC DMA reads are checked by the SMU as *secure* accesses, so they need MPSCTRL0.SECURE_READ.

**Kernel patch 0004:** `mmc: dw_mmc-exynos: allow secure reads through the SMU on Exynos7880`.

- It sets SECURE_READ for `samsung,exynos7880-dw-mshc-smu` only.
- It passes checkpatch.
- **M3d** checks that the kernel alone (with no userspace SMU write) gives 0xf1. It also reports bus width/timing and a 32 MiB read speed.

### 17d. Hardware result for M3d: the kernel fix alone, at full speed (2026-10-19 01:25)

Photos: `evidence/hw-mainline-m3d-{emmc,keys,ghosting}-2026-10-19.jpg`.

| Observation | Meaning |
|---|---|
| `smu : kernel SEC 0xdfe26492 CTRL0 0x000000f1` (init no longer writes the SMU) | **Patch 0004 works on its own.** |
| GPT OK, 25 partitions (same list as 17c) | Partition table read correctly. |
| `ios : clock 52000000 Hz, actual clock 49968750 Hz` | The measured 399.75 MHz / 4 / 2 = 49.97 MHz, so the fixed-clock value is exactly right. |
| `ios : bus width 3 (8 bits)` | The 8-bit bus switch now passes: the M3 failure was the same SMU issue, because the bus test reads also go through the IDMAC. |
| `ios : timing spec 8 (mmc DDR52)` | The mmc core chose DDR52 by itself. The Exynos dw_mmc capabilities include 1.8V DDR, and the card supports it. |
| `ios : signal voltage 1 (1.80 V)` | No vqmmc regulator is described; the I/O rail is already at 1.8 V from S-Boot. |
| `speed : read 32 MiB in 390 ms = 82.0 MiB/s` | Uncached sequential read at about 80% of the DDR52 8-bit theoretical maximum (~104 MB/s). |
| All four keys work; holding Power reboots into TWRP | Keys and exit unaffected. |

**Cosmetic.** One photo (`…-ghosting-…`) shows two console frames overlapping, so text looks doubled while the screen redraws. Two possible causes:

- the camera caught a moment mid-refresh, or
- the TE refresh scanned out while fbcon was scrolling.

Watch for it, but it doesn't affect function.

**Status:**

- **Milestone 3 (eMMC read) passes.**
- The fixed clocks stay until a 7880 CMU driver exists.
- HS200/HS400 (up to ~200 MB/s) would need tuning and a real clock driver.

## 18. Milestone 4: USB networking (built 2026-10-19, not yet tested on the phone)

### The 7880 USB block is the 7870's

Every point below comes from the Samsung universal7880 kernel unless marked.

| Item | Exynos7880 (Samsung) | Exynos7870 (mainline v7.2) |
|---|---|---|
| Controller | `udc: usb@13600000`, `samsung,exynos7870-dwusb2`, DWC3 at SPI 230 | `usb@13600000`, `samsung,exynos7870-dwusb3`, SPI 230 |
| PHY | `usbphy@135c0000`, `samsung,exynos7870-usbdrd-phy`, PMU syscon | `phy@135c0000`, same compatible |
| PMU PHY control | `PMU_USBDEV_PHY_CONTROL` = PMU + 0x704 (`S5E7880-pmusfr.h:197`) | `EXYNOS5_USBDRD_PHY_CONTROL` 0x704, enable bit 1 |
| No VBUS pad | `is_not_vbus_pad` (7880 **and** Samsung's own 7870 DT) | mainline 7870 PHY init forces VBUSVALID/BVALID and sets VBUSVLDEXT |
| USB PLL | pll1418x, `usb_pll_rates` = 50 MHz (M 400, P 13, S 4) | pll_1417x/1418x share the same 0822x formula |

**CMU_FSYS (0x13730000).** Compared register by register between Samsung's `S5E7870-cmusfr.h` and `S5E7880-cmusfr.h`:

- All 32 Exynos7870 registers sit at the same offsets on the 7880; the 7880 adds one (`CLK_ENABLE_CLK_FSYS_BUS_SECURE_INTMEM`, 0x840).
- In `-cmu.c`, every FSYS gate has the same register and bit. The only difference is the parent's home: CMU_MIF on the 7870, CMU_CCORE on the 7880.

So the **mainline Exynos7870 CMU_FSYS driver can drive the 7880's**. Its inputs from CMU_CCORE are fixed clocks for now, given the clock names the 7870 driver looks up.

**USB supplies.** In `exynos7880-a5y17lte_common.dtsi`, the s2mpu05 PMIC supplies:

- LDO8 (vdd33)
- LDO1 (vdd10)
- LDO3 (dwc3-vbus)

All three are `regulator-boot-on` and `regulator-always-on`, so they are described here as always-on fixed regulators.

**Open risk: the USB PLL may be off.**

- Mainline's pll_1417x has no rate table, so it only gets recalc ops and **cannot enable the PLL**. It relies on S-Boot.
- Samsung's kernel calls `vclk_disable(p1_usb_pll)` at boot (`S5E7880-vclk.c:854`), which suggests S-Boot leaves it on. Not confirmed.
- The M4 `a5-diag usb` step therefore prints USB_PLL_CON0, the FSYS gates, the CCORE FSYS_BUS/USB20DRD mux/div/enable and the PMU 0x704 state.
- If the PLL or a CCORE gate is off, it programs Samsung's 50 MHz values or turns the gate on, then `/init` re-binds the PHY and DWC3.

### Kernel patches 0005–0007

All three are binding-only and checkpatch-clean:

- `dt-bindings: clock: samsung,exynos7870-cmu: add exynos7880 CMU_FSYS`
- `dt-bindings: phy: samsung,usb3-drd-phy: add exynos7880`
- `dt-bindings: usb: samsung,exynos-dwc3: add exynos7880`

Each uses the 7870 compatible as fallback. **No driver change is needed.**

Not checked: `dt_binding_check`. dtschema can't be installed here (PyPI is unreachable); the edited YAML parses.

### Userspace

Static aarch64 binaries built with clang against musl 1.2.5 (`build-userspace.sh`):

- busybox 1.36.1
- dropbear 2026.94
- `a5-diag`

They were smoke-tested in a Unicorn user-mode runner (`tools/uemu.py`): musl long-double printf, `busybox echo`, `busybox sh -c`, `dropbear -V`.

### Gadget and services

- Composite gadget: **NCM** network + **ACM** serial (VID 0x1d6b, PID 0x0104).
- The phone is 172.16.42.1 (postmarketOS convention); udhcpd gives the PC 172.16.42.2 with no router, so the PC keeps its normal internet route.
- Services:
  - dropbear on port 22: root, empty password, `-B`
  - telnetd on port 23
  - a shell on the ACM port
- The eMMC stays read-only (BLKROSET).

### Checks so far

- Kernel builds; every config option applied.
- DTB builds with W=1 and no warnings.
- The initramfs contains busybox, dropbear and a5-diag.
- The emulated U-Boot path reaches the Linux entry with a 12 KB FDT.

### 18a. Hardware result for M4 (2026-10-19 03:05)

Photo: `evidence/hw-mainline-m4-usbclk-2026-10-19.jpg`.

**USB clocks as S-Boot left them:**

| Register | Value | Meaning |
|---|---|---|
| CCORE FSYS_BUS | mux `0x00200000`, div `0x2`, en `0x1` | BUS_PLL/2 ÷ 3 = **266.5 MHz**; the DT fixed clock is now 266500000 |
| CCORE USB20DRD | mux `0x00200000`, div `0xf`, en `0x1` | BUS_PLL/2 ÷ 16 = **49.969 MHz**, matching the DT value |
| FSYS USB_PLL_CON0 | `0xa1900d04` | M 400, P 13, S 4, enabled + locked = **50.000 MHz**: **S-Boot leaves the USB PLL on**, at exactly Samsung's rate |
| FSYS mux_usb_pll / phyclk_user | `0x00201000` / `0x00201000` | Gate (bit 21) and select (bit 12) set by the 7870 CMU driver's critical gates |
| FSYS en_bus | `0x07ffffff` | All bus gates on |
| FSYS en_ref | `0x00000001` | Reference gate on |
| PMU USBDEV_PHY_CONTROL | `0x00000002` | PHY enable bit set (not isolated) |

**Bug in /init:**

- `busybox --install -s` failed for every `/usr/bin` and `/usr/sbin` applet because those directories did not exist. That left no `head`, `udhcpd`, `telnetd` or `setsid`.
- `UDC=$(ls /sys/class/udc | head -1)` therefore came out empty: `/init: line 41: head: not found`, then `UDC = NONE`, then `usb0 missing`.
- The re-bind branch did **not** run, so `/sys/class/udc` was **not** empty: **the DWC3 controller probed.**

**Fix (M4b):**

- `/usr/bin` and `/usr/sbin` are added to the initramfs and created before `--install`.
- `head` is no longer needed.
- The FSYS_BUS rate is set to the measured value.

### 18b. Hardware result for M4b, phone side (2026-10-19 03:08)

Photo: `evidence/hw-mainline-m4b-usb-2026-10-19.jpg`.

**Working:**

- `UDC = 13600000.usb`: the DWC3 gadget controller runs on the 7880 using the mainline 7870 drivers, with no driver changes.
- `gadget bound (NCM + ACM)`.
- `usb0 172.16.42.1`, with udhcpd, dropbear and telnetd started.
- `serial : shell on the PC's /dev/ttyACM0`.
- Kernel log: `exynos5_usb3drd_phy 135c0000.phy` probed; its only messages are the expected dummy `vbus`/`vbus-boost` regulators.
- Clocks are identical to 18a.

**Side effect:**

- An `a5y17lte:~#` prompt appeared on the phone's own screen.
- Likely cause:
  - busybox `cttyhack` falls back to `/sys/class/tty/console/active` (tty0) when it can't identify ttyGS0.
  - The PC (ModemManager) opening `/dev/ttyACM0` then starts that shell.
- To fix next build: start the serial shell with `setsid -c` on ttyGS0 directly, without cttyhack.

**Pending:** the PC side (lsusb, DHCP, SSH).

### 18c. PC side of M4b: no enumeration; M4c built (2026-10-19 03:13)

**What the PC saw:**

- PC `lsusb`: only its own root hubs.
- `sudo dmesg -w` while replugging the cable with M4b running: **no USB events at all**, not even an enumeration error.
- The same PC port and cable enumerated TWRP shortly before (`18d1:4ee2 SAMSUNG_Android` on usb 3-5).

So the PC never sees the D+ pull-up.

**Suspect: the S2MU004 MUIC.** From Samsung `exynos7880-a5y17lte_common.dtsi`:

- `s2mu004@3d` on `i2c@13870000`, pins `gpc1-1`/`gpc1-0`, `muic` enabled.
- The MUIC switches D+/D- between OPEN, USB and UART through `SW_CTRL` (0xCA).
  - Fields `[7:5]` DM and `[4:2]` DP: 1 = USB, 2 = UART.
- `CTRL1` (0xC7) bit 2 selects 1 = auto or 0 = manual switching (`set_manual_sw`).
- Samsung's driver routes to USB with `com_to_usb()`.
- S-Boot's MUIC state is unknown.

**M4c changes:**

- The TOP pin controller is added (SPI 438, 19 banks including the 7880-only `gpd5`).
- An `i2c-gpio` bus is placed on gpc1-0 (SDA) and gpc1-1 (SCL), since the 7880 CMU_PERI clock for the hardware I2C isn't supported yet.
- `a5-diag muic` reads REV_ID, ADC, DEVICE_TYPE1–3, CHG_TYPE, CTRL1 and SW_CTRL.
  - If D+/D- isn't on USB, it sets manual mode with DM = DP = USB, and `/init` re-attaches the gadget.
- `a5-diag usbregs` shows:
  - the UDC state
  - DWC3 GCTL/GSTS/GUSB2PHYCFG/DCFG/DCTL/DSTS
  - PHY LINKSYSTEM/UTMI/CLKRST/HSPHYCTRL/PLLTUNE
- The serial shell now uses `setsid -c` on ttyGS0 instead of cttyhack.

### 18d. Hardware result for M4c: THE PC ENUMERATES THE PHONE (2026-10-19 03:19)

Photos: `evidence/hw-mainline-m4c-{muic,pc-dmesg}-2026-10-19.jpg`.

**Phone side:**

- **MUIC as S-Boot left it:** `rev 29 adc 00 dev 00 00 01 chg 00`, `CTRL1 17 (auto switching) SW_CTRL 00 (D+/D- = OPEN)`.
  - **S-Boot leaves the USB data lines disconnected.** That was the M4/M4b failure.
- **After a5-diag:** `CTRL1 13 (manual) SW_CTRL 24 (D+/D- = USB)`, then the gadget was re-attached and `udc : state configured`.
- **The GPIO I2C (gpc1-0 SDA, gpc1-1 SCL) to the S2MU004 at 0x3d works.**

**Register dump:**

| Block | Values |
|---|---|
| DWC3 | GCTL 30c12004, GSTS 3e800000, GUSB2PHYCFG 40102540, DCFG 00a00888, DCTL 80f00a00 (RUN_STOP set), DSTS 0003f388 |
| PHY | LINKSYSTEM 080001c0, UTMI 00000660, CLKRST 800000e9, HSPHYCTRL 00150430, PLLTUNE 0000000e |

**PC side** (Linux Mint, `dmesg`, xhci port 3-5):

- `New USB device found, idVendor=1d6b, idProduct=0104, bcdDevice= 7.02`
- `Product: Galaxy A5 2017 (mainline Linux)`, `Manufacturer: a5y17lte mainline`, `SerialNumber: a5y17lte-m4`
- `cdc_acm 3-5:1.2: ttyACM0: USB ACM device`
- `cdc_ncm 3-5:1.0 usb0: register 'cdc_ncm' ... 02:a5:17:00:00:02`, renamed `enx02a517000002`

**USB gadget mode works at high speed on the Exynos7880 under mainline Linux.**

## Open items

1. ~~Phone test of milestone 1~~: passed in full (section 15).
2. **TWRP readings** (`/proc/iomem`, `/proc/cmdline`, `/proc/device-tree`, `chosen/linux,initrd-start`):
   - where S-Boot really loads the ramdisk (FIT), to confirm it cannot overlap the kernel at 0x48000000
   - the real memory map
3. **Shrinking the carve-outs:** decide which M1 no-map regions are truly secure and which can return to Linux.
4. **Next milestones:**
   - 7880 clock driver (MIF0/MIF1/CCORE)
   - pinctrl, then gpio-keys
   - UART
   - MMC (eMMC/SD)
   - USB gadget networking
   - touch
   - DSI/DECON (Linux 6.18+ has the 7870 DSIM)
   - postmarketOS device package
5. ~~Framebuffer address~~ confirmed in section 12. ~~UART cable~~ not needed; the screen is the console.

## 18e. M4c on hardware: USB network up, telnet login; dropbear flag bug (M4d)

User test of M4c (2026-10-19), PC side:
- `ip addr show enx02a517000002` → `inet 172.16.42.2/24` (DHCP lease from udhcpd)
- `ping 172.16.42.1` → 3/3 received, ~2 ms
- `telnet 172.16.42.1` → busybox root shell `~ #` (first interactive login)
- On the phone: `uname -a` → `Linux a5y17lte 7.2.0+ #2 SMP PREEMPT … aarch64`,
  8 CPUs in /proc/cpuinfo, `free -m` total 2539 MiB.
- `ssh root@172.16.42.1` → Connection refused. `/tmp/dropbear.log`:
  `Invalid option -E` + usage text. In dropbear 2026.94, `-E` exists only
  when syslog support is compiled in; we build `--disable-syslog`, so it
  already logs to stderr and rejects the flag. The default pidfile
  `/var/run/dropbear.pid` also had no directory.

M4d fix: drop `-E`, pass `-P /var/run/dropbear.pid`, add /var, /var/run,
/var/log to the cpio and to init's mkdir. Image
`out/a5y17lte-mainline-m4d-boot.img`, sha256
`5cd13ecb78e97869ac72feaec6e559965c8ec5d4440f113555a3872bebc8f291`.

Manual start in telnet (`mkdir -p /var/run && dropbear -R -B -p 22`) then
showed the second bug: on connect, `Early exit: Non-multiuser Dropbear
requires a non-multiuser kernel` (PC: `kex_exchange_identification: Connection
reset by peer`). `DROPBEAR_SVR_MULTIUSER 0` needs CONFIG_MULTIUSER=n; our
kernel has it =y, so M4d builds the default multi-user dropbear (root from
/etc/passwd, blank password via -B).

## 18f. M4d on hardware: SSH works (M4 passed)

Photo: `evidence/hw-mainline-m4d-ssh.jpg`. PC side:
- `enx02a517000002` 172.16.42.2/24 (DHCP), ping 3/3, 1.9–3.6 ms.
- `ssh root@172.16.42.1` → ED25519 key `SHA256:TDbU+Wc8HhspWRKQvoCM8nO7D+N58FtthHgajBi5xnE`,
  login banner "Galaxy A5 (2017) - mainline Linux 7.2.0+. eMMC is read-only
  in this image.", prompt `a5y17lte:~#`.
- `/tmp/dropbear.log`: "Waiting for kernel randomness to be initialised…"
  (~9 s at boot), host key generated on first connect, "Auth succeeded with
  blank password for 'root'".
- `scp` fails: `sh: /usr/libexec/sftp-server: not found`. OpenSSH 9+ scp
  uses SFTP by default, and dropbear has no sftp-server. The legacy protocol
  (`scp -O`) runs `scp -t` on the phone, which the image also lacked.
  M4e adds dropbear's own `scp` to dropbearmulti (+ /bin/scp symlink), so
  `scp -O` works; a real sftp-server comes with postmarketOS (OpenSSH).

## 18g. M4e on hardware: scp works

Photo: `evidence/hw-mainline-m4e-scp.jpg`. New host key
`SHA256:p3WHFClYT++zz/SdX4A+J5EEEgQNCT08xVfTPv+l/g4` (generated per boot, eMMC
read-only). `scp -O test.txt root@172.16.42.1:/tmp/` → 100%, 6 bytes;
`ssh root@172.16.42.1 cat /tmp/test.txt` → `hello`. PC dmesg on the
TWRP→mainline switch: 18d1:4ee2 (TWRP) disconnects, then 1d6b:0104
"Galaxy A5 2017 (mainline Linux)", Mfr "a5y17lte mainline", serial
"a5y17lte-m4", cdc_ncm usb0 → enx02a517000002, cdc_acm ttyACM0.

## 19. Milestone 5a: distro kernel config, SYSTEM probe (PASSED on the phone 2026-10-19, see 19a)

**Read-only lock correction.** Since M3, `a5-diag info` locked the eMMC with
`BLKROSET` on `/dev/mmcblk0`. In v7.2 that only marks the whole-disk device:
`blkdev_roset()` (`block/ioctl.c:530`) sets `BD_READ_ONLY` on the bdev it is
called on, the mmc driver has no `set_read_only` hook, and
`bdev_read_only()` (`include/linux/blkdev.h:833`) checks that bdev's flag or
the disk's `GD_READ_ONLY`. So `mmcblk0p1..p25` were never read-only. Nothing
wrote to them: no image mounted a filesystem and no partition was written over
SSH. M5a also writes `/sys/block/mmcblk0/force_ro` = 1 (`set_disk_ro()`,
`drivers/mmc/core/block.c:327`), which covers every partition, and checks
each partition's `ro` attribute: `emmc : ..., all READ-ONLY`.

**Kernel config** (`mainline/config/a5y17lte.config`, M5 block). Based on the
pmaports `kconfigcheck.toml` (pmaports f3f92690) categories default, nftables
and zram, all built in (still no modules), plus what a root filesystem needs:
EXT4, INOTIFY_USER, FANOTIFY, AUTOFS, SYSVIPC, POSIX_MQUEUE, SECCOMP, cgroups,
namespaces, dm-crypt, zram (needs `BLK_DEV`, which allnoconfig turns off) and
nftables. Deferred: module compression (no modules), BTF/kprobes/ftrace/LSM,
DRM fbdev emulation (no DRM driver yet), udmabuf, iptables-legacy.

**SYSTEM probe.** `a5-diag system` finds the partition by GPT name
(`PARTNAME=SYSTEM` in its uevent, no hard-coded number), prints its size and
reads only the ext4 superblock (magic, label, filesystem size).

**Checks so far:**
- All 159 fragment options applied.
- Kernel Image 11.3 MiB (in memory 11.7 MiB, 0x48000000-0x48bb0000), below
  the ramdisk U-Boot places at 0x4feff000.
- Emulated U-Boot with the FIT at 0x41000000 and at 0x5A000000: hashes OK,
  `reached Linux entry 0x48000000`, FDT magic OK.
- Builds are reproducible (same commit, clean trees: same image bytes).
- Image `out/a5y17lte-mainline-m5a-boot.img`, sha256
  `12cf73579ddff07bca2a16090314c3d6e7f4c87ad6ddd637d709925a82af93ce`.

**Expected on the phone:** M4 behaviour unchanged (keys, USB network, SSH),
plus `emmc : BJNB4R, 29820 MiB, 25 partitions, all READ-ONLY` and
`system : mmcblk0pN, ... MiB, ro=1` with the ext4 superblock line.

### 19a. Hardware result for M5a (2026-10-19)

Jimmy flashed `a5y17lte-mainline-m5a-boot.img` (sha256 `12cf7357…`): the
screen console and SSH work as in M4. Readings taken over SSH (read-only):

| Reading | Meaning |
|---|---|
| `uname -v`: `#1 SMP PREEMPT Mon Oct 19 01:25:32 UTC 2026` | The reproducible build (epoch = commit 510ecc6) |
| `emmc : BJNB4R, 29820 MiB, 25 partitions, all READ-ONLY`; `/sys/class/block/mmcblk0p*/ro` = 1 for all 25 | `force_ro` now covers every partition |
| `system : mmcblk0p19, 9928704 sectors = 4848 MiB, ro=1` | SYSTEM is p19, 4848 MiB (5,083,496,448 bytes) |
| `system : ext4, label "/", 4296 MiB filesystem` | Holds the LineageOS system image Sailfish uses; the filesystem is 552 MiB smaller than the partition |
| EFS = `mmcblk0p3`, 40960 sectors (20 MiB); USERDATA = `mmcblk0p25`, 50110464 sectors | By GPT name, for the M5c backup |
| `/proc/config.gz`: 697 options `=y` | The M5 config is running |

All 8 CPUs online; MemTotal 2597040 kB (the extra built-in code costs about 5 MB against M2's 2602388 kB).


## 20. W0: Waydroid kernel options and simpledrm (built, not yet tested on the phone)

First milestone of the open-Android path (ANDROID-ROADMAP.md, branch
android-roadmap). Branch `android-w0`, kept off `main` until it passes.

**Kernel config** (`mainline/config/a5y17lte.config`, W0 blocks):
- Binder: ANDROID_BINDER_IPC, ANDROID_BINDERFS, ANDROID_BINDER_DEVICES =
  "binder,hwbinder,vndbinder". With binderfs on, v7.2 `binder_init()`
  (drivers/android/binder.c:7134) creates no /dev/binder misc nodes; the three
  devices appear inside every binderfs mount instead.
- LXC: VETH (needs NET_CORE, which allnoconfig leaves off), CPUSETS, FUSE_FS.
- waydroid-net.sh (waydroid/waydroid main): bridge `waydroid0`, NAT to the
  host. It defaults to iptables, legacy binary first, with a mangle CHECKSUM
  rule for DHCP replies; nftables only when LXC_USE_NFT=true. So: BRIDGE,
  NETFILTER_XTABLES(_LEGACY), IP_NF_IPTABLES(_LEGACY), IP_NF_FILTER/NAT/MANGLE,
  IP_NF_TARGET_MASQUERADE, NETFILTER_XT_NAT, XT_TARGET_MASQUERADE,
  XT_TARGET_CHECKSUM, NFT_COMPAT (iptables-nft). In v7.2 every IP_NF_* table
  depends on IP_NF_IPTABLES_LEGACY, which depends on NETFILTER_XTABLES_LEGACY.
- Display: FB_SIMPLE replaced by DRM + DRM_SIMPLEDRM + DRM_FBDEV_EMULATION.
  The same simple-framebuffer node (0x67000000, 1080x1920) becomes
  /dev/dri/card0 for phoc and Waydroid; fbcon runs on DRM's fbdev client.
  simpledrm only writes the buffer; the panel refresh still comes from
  `a5-diag panel` (DECON HW_SW_TRIG_CONTROL = 0x10, sec. 15), unchanged.
- **pmaports check (W0b).** Jimmy's copy of pmaports f3f92690
  kconfigcheck.toml, category waydroid, showed what W0 lacked: static
  devices binder/hwbinder/vndbinder, BLK_DEV_LOOP, SQUASHFS (+XATTR, XZ;
  MISC_FILESYSTEMS), TUN, VLAN_8021Q, BRIDGE_VLAN_FILTERING,
  CROSS_MEMORY_ATTACH, NETFILTER_XT_MATCH_COMMENT. All added, built in.
  **One deliberate difference:** pmaports asks ANDROID_BINDERFS = n; we keep
  y, because AOSP R and later requires binderfs (phase A), and Waydroid uses
  binderfs when /dev/binder is absent. Everything else in the category is
  satisfied (m accepted as y). BOOT partition = 33,554,432 B (TWRP,
  `blockdev --getsize64`).

**Checks so far** (Linux v7.2 8d3ae5928 + patches 0001-0007, clang 18):
- `allnoconfig` + fragment: all 181 options applied, none overridden.
- Image builds: 12,879,880 B on disk (M5a: 11.3 MiB), in memory 0xcb0000 =
  13,303,808 B, so 0x48000000-0x48cb0000, still below the ramdisk at
  0x4feff000. Raw Image sha256 b03c6287… (not the reproducible build; that
  is build-mainline.sh on the PC).
- DTB unchanged, W=1 clean.
- `a5-diag android` compiles with -Wall -Werror (musl 1.2.5, v7.2 uapi).
- W0 (first build) boot image on Jimmy's PC: 14,419,984 B, sha256
  5be8bfa3…; kernel 12,879,880 B, the same size as here. Superseded by W0b
  (not flashed).

**`a5-diag android`** (called from /init after `a5-diag usb`): prints the
driver behind /dev/dri/card0 (DRM_IOCTL_VERSION) and the connector modes,
mounts binderfs at /dev/binderfs, creates device `a5test` through
binder-control (BINDER_CTL_ADD), checks BINDER_VERSION, reads
/proc/pressure/memory, then `w0 : PASS` or `FAIL`. No eMMC access.

**Expected on the phone:**
- Console text as before, now via simpledrm (dmesg: `[drm] Initialized
  simpledrm`); no change in the TE refresh.
- `drm : /dev/dri/card0 OK, driver simpledrm` and `card0-Unknown-1 connected,
  modes 1080x1920` (connector name may differ).
- `binder : binderfs OK, device N:M, protocol 8 (want 8)`, then
  `/dev/binderfs/{binder,hwbinder,vndbinder} OK, protocol 8`, a `psi` line,
  `w0 : PASS`.
- Keys, USB networking, SSH unchanged.

### 20a. Hardware result for W0b: W0 PASSED (2026-10-19 14:35)

Jimmy flashed `a5y17lte-mainline-w0b-boot.img` (14,487,568 B, sha256
`86ce1e4e…13b9aa`, built on his PC from 52661c4). Photos:
`evidence/hw-mainline-w0b-{android,net}-2026-10-19.jpg`.

| Reading | Meaning |
|---|---|
| Kernel log and console fully visible, refreshing | **simpledrm + fbdev emulation keep the command-mode panel working**: the TE refresh from `a5-diag panel` is unaffected (main W0 risk, sec. 20). |
| `drm : /dev/dri/card0 OK, driver simpledrm`; `card0-Unknown-1 connected, modes 1080x1920` | KMS device for phoc/Waydroid on the boot framebuffer. |
| `binder : binderfs OK, device 250:4, protocol 8 (want 8)` | binderfs mounts; BINDER_CTL_ADD and BINDER_VERSION work. |
| `/dev/binderfs/{binder,hwbinder,vndbinder} OK, protocol 8` | The three default devices from ANDROID_BINDER_DEVICES appear in the mount (minors 1-3; the test device took 4). |
| `psi : some avg10=0.00 … total=0` | PSI available for Android's lmkd. |
| `w0 : PASS` | |
| Boot log: `loop: module loaded`, `tun: Universal TUN/TAP device driver`, `8021q: 802.1Q VLAN Support` | The W0b options are built in. |
| emmc `all READ-ONLY`; SYSTEM p19 ro=1; USB (UDC, NCM+ACM, MUIC switch, `udc : state configured`), `usb0 172.16.42.1` | M3-M5a behaviour unchanged. |
| MemTotal 2,595,984 kB (M5a: 2,597,040 kB) | W0b's extra code costs about 1 MB. |

Over SSH (new per-boot host key `SHA256:tLcqUhen…fYg`):
- `dmesg`: `[drm] Initialized simpledrm 1.0.0 for 67000000.framebuffer on
  minor 0` at 0.055 s, then `fb0: simpledrmdrmfb frame buffer device`.
- `/dev/binderfs`: binder-control 250:0, binder 250:1, hwbinder 250:2,
  vndbinder 250:3, a5test 250:4, plus `features/`.

The banner says "W0" (the W0b build reused W0's init text).

**Next:** W1, Waydroid headless on Nura, after M5d. It needs these W0 options in
the kernel Nura boots, so `android-w0` merges into `main` once the Nura
session has finished M5c/M5d.

## 21. Milestone 5b/5c: postmarketOS rootfs built and written to SYSTEM (2026-10-19)

**M5b.** `./build-pmos.sh` (pmaports f3f92690, pmbootstrap 7d23010; our
`pmos/` device and kernel packages): `samsung-a5y17lte-root.img`, 686,817,280
bytes, sha256 `06e88091addeafbb295de4d6dd45428f14e46cc49676d10bc2b2cbf2c63b676b`.
ext4 `pmOS_root` (features include `metadata_csum`, `64bit`, `orphan_file`),
e2fsck 1.47 clean. OpenRC (pinned: the console UI now defaults to systemd),
inittab getty on ttyGS0; runlevels have `sshd`, `unudhcpd.usb0`, `nftables`,
`chronyd`. `/usr/lib/os-release`: `PRETTY_NAME="Nura edge"`. pmbootstrap's
`/etc/fstab` had a `/boot` line for a pmOS_boot partition this phone doesn't
have, and was owned by the build user (1000:1000); build-pmos.sh drops the
line and makes the file 0:0. The `/` line keeps `defaults`, so OpenRC would
remount root read-write.

**M5c steps 1-2 (TWRP, read-only).** TWRP's by-name links
(`/dev/block/platform/13540000.dwmmc0/by-name`) map EFS → mmcblk0p3 (20,971,520 B)
and SYSTEM → mmcblk0p19 (5,083,496,448 B), the same as mainline (19a).
Backups in `~/a5-backup` (not in git): EFS `5efb6122…`, SYSTEM `17530cf1…`;
phone and PC hashes match, and e2fsck -fn on the SYSTEM copy is clean.

**Before step 3 (re-check).** EFS had changed to `5f6cfe57…`: mount count
333 → 337, plus the superblock, journal and the contents of `nv_data.bin` and
`nv_data.bin.md5`. Those are the modem's NV data, which only an OS with the
modem stack writes (not TWRP, not our images), so presumably Sailfish booted
after 04:35. A second backup, `EFS-p3-20261019-1440.img`, was saved. SYSTEM
had also changed (`fd8b3f19…`), but only in mount metadata: mount count
71 → 79, last mounted on `/system_root`, with free blocks/inodes and lifetime
writes unchanged. **TWRP mounts SYSTEM (`/system_root`) and EFS at startup**;
it did so 3 s before the first backup too. So the SYSTEM backup is still a
valid rollback.

**M5c step 3: the first eMMC write (Jimmy's OK).** `adb push` to TWRP's tmpfs
`/tmp` (18.3 MB/s), sha256 on the phone `06e88091…`, then
`dd if=/tmp/root.img of=/dev/block/platform/13540000.dwmmc0/by-name/SYSTEM bs=4M && sync`:
686,817,280 bytes, exit 0.
- After `blockdev --flushbufs` + `drop_caches`, `head -c 686817280` of SYSTEM
  gives sha256 `06e88091…`, the same as root.img.
- TWRP's e2fsck 1.43.3 can't check it: `unsupported feature(s): FEATURE_C12`
  (orphan_file, e2fsprogs 1.47). A read-back checked with e2fsck 1.47.0 on the
  PC is clean: `pmOS_root: 6751/83904 files, 134672/167680 blocks`.
- EFS is unchanged by the write (`5f6cfe57…`).
- The rest of SYSTEM (after the 655 MiB image) still holds old data;
  `resize2fs` in M5e grows the filesystem over it.

## 22. Milestone 5d: boot Nura from SYSTEM, read-only under a RAM overlay (PASSED on the phone 2026-10-19, see 22a)

Includes W0 (merged, section 20). After the M5a/W0 checks, the gadget, the
MUIC switch and `usb0 = 172.16.42.1`, `/init` waits 3 s for Volume Down
(`a5-diag held 3000`, key state via `EVIOCGKEY`). Without it, `/init` does this:
- **Find and mount SYSTEM.** It finds the partition by GPT name, refuses to
  continue unless `/sys/block/mmcblk0/force_ro` is 1, and mounts it ext4
  `ro,noload`. It requires `/sbin/init` and `/usr/lib/os-release` there.
- **Overlay.** It puts a tmpfs (size 50% of RAM) overlay on top
  (`CONFIG_OVERLAY_FS`). Writes in Nura go to RAM only and are lost at reboot.
- **RAM-layer settings.** It writes two files into the RAM layer:
  `/etc/NetworkManager/conf.d/90-a5y17lte-usb0.conf` (usb0 unmanaged, so our
  address stays) and `/etc/conf.d/unudhcpd.usb0` (`UNUDHCPD_SERVER=172.16.42.1`;
  the OpenRC script's default is 172.16.41.1, unudhcpd 0.5.0).
- **Hand-off.** It moves the layers to `/mnt/system-ro` and `/mnt/overlay-rw`,
  moves /dev, /proc and /sys (configfs with the gadget comes along), and runs
  `switch_root /newroot /sbin/init` (busybox init + OpenRC). Nura's own getty
  on ttyGS0 and sshd take over from our dropbear and serial shell.

If any step fails, or Volume Down is held, the rescue services start as in
M4/W0 (dropbear root, telnet, udhcpd, serial shell, hold Power -> TWRP).

**Checks so far:**
- All fragment options applied (OVERLAY_FS, DRM_SIMPLEDRM, ANDROID_BINDERFS, ...).
- `/init` passes `dash -n` and `bash -n`. Not run anywhere yet: the
  host can't run aarch64 binaries (binfmt points to a missing
  qemu-aarch64-static) and `tools/uemu.py` lacks `openat`.
- Kernel 12.9 MiB in memory (ends 0x48ce0000), below the ramdisk at 0x4feff000.
- Emulated U-Boot with the FIT at 0x41000000 and 0x5A000000: hashes OK,
  `reached Linux entry 0x48000000`.
- Image `out/a5y17lte-mainline-m5d-boot.img`, 14622736 bytes, sha256
  `7ab5ddd7b9be221693293c6f3758b059474ee7be6dd51ca69b5f12b6022be3a3`.

**Expected on the phone:**
- `nura : /dev/mmcblk0p19 PRETTY_NAME="Nura edge", read-only`, then
  `switch_root`, then OpenRC, then a login prompt on tty1.
- PC: DHCP 172.16.42.2 from unudhcpd; `ssh user@172.16.42.1` (key login;
  the host key is new on every boot); a getty on /dev/ttyACM0.
- In Nura, `cat /sys/block/mmcblk0/force_ro` gives 1 and `findmnt /` shows overlay.

### 22a. Hardware result for M5d: NURA BOOTS (2026-10-19)

Jimmy flashed `a5y17lte-mainline-m5d-boot.img` (sha256 `7ab5ddd7…`).
Photos: `evidence/hw-mainline-m5d-init.jpg` (initramfs screen: `M5d`,
`system : ext4, label "pmOS_root", 655 MiB filesystem`, all M5a/W0/M4 lines,
`net : usb0 172.16.42.1`, the Volume Down prompt) and
`evidence/hw-mainline-m5d-nura-openrc.jpg` (OpenRC boot: `ssh-keygen:
generating new host keys`, `Starting sshd ... ok`, `Welcome to Nura!`,
`Kernel 7.2.0+ on an aarch64 (/dev/tty1)`, `a5y17lte login:`).

| Observation | Meaning |
|---|---|
| OpenRC boots, "Welcome to Nura!" | `switch_root` from our initramfs into busybox init + OpenRC on SYSTEM works |
| `ssh user@172.16.42.1` with Jimmy's key | Gadget and usb0 survive `switch_root`; Nura's sshd, unudhcpd (172.16.42.2 to the PC) and nftables (usb* accepted) work |
| `findmnt /`: overlay, `lowerdir=/nura/ro,upperdir=/nura/rw/upper` | Root is the RAM overlay over the read-only SYSTEM |
| `/sys/block/mmcblk0/force_ro` = 1 | The eMMC stays read-only under Nura |
| Over SSH (Claude): `id`: user in `wheel` (10); `free -m`: 2535 MiB total | |

**OpenRC errors seen (Jimmy):**
- `hwclock`: no RTC driver yet (the S2MPU05 RTC needs the PMIC on I2C).
- Clock skew: the clock starts at Jan 1 with no RTC; file times are newer.
- `logbookd.db` restore: every boot is a first boot (RAM overlay).
- No wireless: no Wi-Fi driver yet.
- `postmarketos-zram-swap failed to start`.

**Root access.** `doas: not found` and `su` (root is locked) are expected:
this pmaports commit uses sudo, not doas (postmarketos-base depends on
`sudo-virt`, `_pmb_recommends="sudo-rs"`). `sudo-rs 0.2.15` is installed
setuid root; `sudo -S id` with the user password gives `uid=0(root)`.

**zram root cause.** `/usr/sbin/zramstart` runs under `set -e` and calls
`modprobe -q zram num_devices=1` before `zramctl`. Our kernel has no modules
and the rootfs has no `/lib/modules/7.2.0+`, so kmod exits 1 with
`modprobe: FATAL: Module zram not found in directory /lib/modules/7.2.0+`,
even though zram is built in: `/sys/block/zram0` exists and `comp_algorithm`
offers `lzo-rle lzo lz4 zstd`. Fix: ship `modules.builtin` (+ `.modinfo`,
depmod output) for the running kernel release, so modprobe treats built-in
drivers as present. The release strings must match first: the FIT kernel says
`7.2.0+` (untagged git tree), the pmOS package would say `7.2.0` (tarball).


**Fix, built 2026-10-19 (not on the phone yet).**
- **Kernel release.** Both kernels are now `7.2.0-a5y17lte`
  (`CONFIG_LOCALVERSION`, empty `LOCALVERSION`). The kernel package (r1)
  ships `modules.builtin`, `modules.builtin.modinfo` and an empty
  `modules.order`.
- **Two traps on the way.**
  - Without `modules.order`, Alpine's `kmod-34.2-r1.trigger` treats
    `/usr/lib/modules/<rel>` as uninstalled and deletes the depmod files.
    With it, the trigger runs `depmod`.
  - mkinitfs 2.12.0 (`internal/filelist/modules/modules.go:146`) needs
    `modules.dep` to exist once the modules directory exists. It accepts
    missing (built-in) modules.
- **Checked on the image.** Host `modprobe -n -d <copy> -S 7.2.0-a5y17lte`
  exits 0 for zram, ext4 and dm_crypt (1 for a made-up name).
- **doas** added next to sudo-rs (`/etc/doas.d/10-postmarketos.conf`:
  `permit persist keepenv :wheel`).
- **New root.img:** 688,914,432 B, sha256 `ef09f3b78962f52b999bc38dee79febb213347eb1a53b23771be12b488a07d72`, e2fsck clean.
- **Still needed:** the boot image must also be rebuilt (the M5d FIT kernel
  is still `7.2.0+`); that comes with M5e.

## 23. Milestone 5e: Nura read-write on SYSTEM (built 2026-10-19, not yet tested on the phone)

**Boot image** `out/a5y17lte-mainline-m5e-boot.img` (14,624,784 B, sha256
`bc0ab7679a415460c125ed853fcd73e38d4d2c397379ae39c8b4b64797dddd66`, from 69d8a95).
- Kernel `7.2.0-a5y17lte`; all fragment options applied.
- 3 s prompt: no key → `a5-diag lock-except SYSTEM`, then SYSTEM mounted rw
  (`errors=remount-ro`) and `switch_root`. The lock sets BLKROSET on the raw
  disk and the other 24 partitions, then sets force_ro to 0, then checks each
  partition's `ro` attribute and BLKROGET on the disk; `/sys/block/mmcblk0/ro`
  can't be used, because `disk_ro_show` shows only the disk flag. Volume Up
  → M5d RAM mode; Volume Down → rescue. Any failure → force_ro 1 → rescue.
- Checks: busybox (aarch64, qemu-user) `sh -n` on the init; the real
  key-dispatch block with a stub `a5-diag` (exit 10 → rescue, 11 →
  `boot_nura ram`, 1 → `boot_nura rw`); emulated U-Boot with the FIT at
  0x41000000 and 0x5A000000 reaches the Linux entry, all hashes OK. Kernel
  ends 0x48ce0000. `lock-except` itself only runs on the phone (no
  `mmcblk0` on the PC).

**Rootfs** `build/pmos/samsung-a5y17lte-root.img` (688,914,432 B, sha256
`bac1e0c63d11bcfdddf29cf2048e08333c933e72893dfea59b0a7e7e1f30bccc`).
- Packages: device-samsung-a5y17lte 3-r2 (+ -openrc),
  linux-postmarketos-exynos7880 7.2-r1, doas, sudo-rs.
- Checked on the image: e2fsck clean; `/usr/lib/modules/7.2.0-a5y17lte`;
  `modprobe -n` exits 0 for zram and ext4; the boot runlevel has `swclock`
  and no `hwclock`; the usb0 settings for NetworkManager and unudhcpd;
  `authorized_keys` = Jimmy's key; sshd enabled (`/usr/sbin/sshd.pam`).
- pmbootstrap installs into a fresh rootfs each build (`--zap`).
  `postmarketos-base-openrc`'s post-install adds hwclock back after ours;
  build-pmos.sh removes the link.
- **Host side effect:** `pmbootstrap shutdown` unregisters the host's
  qemu-aarch64 binfmt; `sudo systemctl restart systemd-binfmt` restores it.

### 23a. M5e step 2a-2c: new rootfs written to SYSTEM (2026-10-19, TWRP)

- **2a (read-only).** By-name EFS → p3, SYSTEM → p19 (5,083,496,448 B), USERDATA
  → p25; nothing mounted. Backup `~/a5-backup/SYSTEM-p19-pmos-m5c.img` (first
  686,817,280 B): sha256 `06e88091…` on the phone and the PC, identical to the
  M5c root.img. So the pmOS SYSTEM was byte-unchanged through M5d (ro,noload)
  and every TWRP start: TWRP doesn't mount this filesystem.
- **EFS.** `211aaf7b…`, where after M5c it was `5f6cfe57…`. Compared with
  `EFS-p3-20261019-1440.img`, one 4K block differs: the superblock (mount
  count 337 → 338, last mount 16:00:18), i.e. one TWRP start. No file
  changed.
- **2b.** `adb push` to TWRP `/tmp` (18.5 MB/s), phone sha256 `bac1e0c6…`;
  `dd if=/tmp/root.img of=/dev/block/platform/13540000.dwmmc0/by-name/SYSTEM
  bs=4M && sync`: 688,914,432 B in 8.0 s, exit 0.
- **2c.** After `blockdev --flushbufs` + `drop_caches`, `head -c 688914432`
  of SYSTEM: sha256 `bac1e0c6…`. The PC copy of the read-back gives the
  same hash; e2fsck 1.47.0 clean (`pmOS_root`, UUID `9394aaf4…`, matching
  its fstab). EFS still `211aaf7b…`. `/tmp/root.img` removed.
- Next: Jimmy flashes the M5e boot image (2d) and does the first boot (2e).

## 24. M5e boot test: "lock : check FAILED (1)" and the fix (2026-10-19)

**Phone result.** Kernel and SYSTEM (`pmOS_root`) fine; normal boot → `lock : check
FAILED (1), force_ro back to 1` → rescue. Nothing was unlocked.

**Cause (kernel source, v7.2, and reproduced on the PC).** `bdev_read_only(bdev)`
= `BD_READ_ONLY` of that bdev **or** `get_disk_ro(disk)`, and `get_disk_ro()` =
`BD_READ_ONLY` of the *whole-disk* bdev (`disk->part0`) or `GD_READ_ONLY`
(`include/linux/blkdev.h:827-836`). So BLKROSET on `/dev/mmcblk0` makes every
partition read-only, including the kept one; the M5e check `ro[SYSTEM] == 0`
could never pass (the one failure), and the earlier "whole disk BLKROGET = 1"
check can't hold together with a writable partition. Loop-device test on the
PC (6 partitions-style: disk setro, p1/p3 setro, p2 setrw): p2 stays ro=1; with
the disk flag cleared instead: disk 0, p1/p3 1, p2 0.

**Fix.** `lock-except` now clears the whole-disk flag (BLKROSET 0; `a5-diag info`
had set it), sets BLKROSET 1 on every other partition and 0 on the kept one,
sets force_ro 0, and checks each partition's `ro` plus BLKROGET disk = 0. It
prints every step (device, ioctl, value, errno); init saves it to
`/tmp/lock.log` and shows it. **Consequence:** the raw `/dev/mmcblk0` node is
writable in the rw boot; no flag can avoid that while one partition is
writable. The other 24 partitions are read-only; mmcblk0boot0/1 keep their own
force_ro = 1.

### 24b. M5e step 3: write/reboot test PASSED (2026-10-19)

Image `out/a5y17lte-mainline-m5e-boot.img` (sha256 `e38f49cd…`, commit 66ba460).
- **Write test.** A 50 MB file written in Nura (read-write SYSTEM) has sha256
  `7558406d…f3b8` and was still intact after a hard reboot.
- **Reboot bug.** `sudo reboot` in Nura hangs on the Samsung logo; the reboot
  had to be forced through recovery. Not diagnosed yet (no reset/poweroff
  driver for the 7880 in our tree is the likely area; unverified).
- **Filesystem.** SYSTEM pulled to the PC: `e2fsck -fn` reports only the
  orphan_file flag (expected with -n); `e2fsck -fy` confirms clean, zero real
  errors.
- **EFS.** Now `a65f32b894a23b331cb990a4b76c5b715e8542f9e522ec3ca023a40d0f4b2378`,
  changed again. Cause: TWRP automounts EFS, not Nura (Nura keeps it read-only).
- Not started: resize2fs, apk via PC NAT.

## 25. M5e step 4: resize2fs fails with EBUSY on the mounted root (2026-10-19)

**Symptom (phone, Nura rw).** `resize2fs /dev/mmcblk0p19` (1.47.4, as root):
`Resource busy while opening /dev/mmcblk0p19`, also with `-d 1`. `/proc/mounts`
and `/etc/mtab` correctly show p19 on `/` ext4 rw; `/dev/root` → mmcblk0p19.
`fuser -m` lists every process (it is the root filesystem), so it can't help.

**Cause.** The built kernel `.config` has `# CONFIG_BLK_DEV_WRITE_MOUNTED is not
set` (`block/Kconfig:77`, default y, but our allnoconfig base leaves it off).
With it off, the kernel refuses any write-open of a block device that a
filesystem has mounted (`BLK_OPEN_RESTRICT_WRITES`, `block/bdev.c`) with EBUSY.
resize2fs opens the device read-write even for an online resize. No process
holds the node; lock-except closes its fds and never uses O_EXCL.

**Workaround, no kernel change.** `mainline/tools/ext4-grow.c` issues
`EXT4_IOC_RESIZE_FS` on the mount point (what resize2fs does after the open;
no device open needed). Static aarch64 binary: `build/ext4-grow-aarch64`
(not in git, git-ignored). PC test on a loop-mounted copy of the M5e root.img
extended to 5,083,496,448 B: `ext4-grow mnt 1241088` → `done: 1202956 blocks`
(statfs, overhead excluded), 1024369 blocks free, unmount, `e2fsck -fn` clean
(1241088 blocks). Not yet run on the phone.

Keeping the option off also blocks raw writes to the mounted SYSTEM node, which
is a useful guard; the alternative (CONFIG_BLK_DEV_WRITE_MOUNTED=y) needs a new
boot image.

### 25a. M5e step 4: root grown online on the phone (2026-10-19)

Binary `build/ext4-grow-aarch64` (sha256 `55d8e7c1…`), run in Nura as root:
`sudo /tmp/ext4-grow / 1241088`.
- Output: `/: block size 4096, now 158492 blocks, growing to 1241088` →
  `done: 1202956 blocks` (statfs counts, overhead excluded; same numbers as the
  PC test).
- `df -h /`: 4.6G total, 538.6M used, 3.9G available, 12%.
- dmesg: `EXT4-fs (mmcblk0p19): resized filesystem from 168192 to 1241088
  blocks`, then `resized filesystem to 1241088`.
- resize2fs itself stays blocked by CONFIG_BLK_DEV_WRITE_MOUNTED being off
  (EVIDENCE 25).
- Not yet done: reboot and `e2fsck -fn`/`-fy` of the pulled SYSTEM on the PC
  to confirm the grown filesystem is clean.

### 25b. M5e step 4 PASSED: grown, rebooted, verified clean (2026-10-20)

- **Reboot.** `sudo reboot` hung on the Samsung logo again; repeatable. Same
  workaround: forced power-off (Volume Down + Power), then a normal power-on
  into TWRP/recovery. Still undiagnosed.
- **Check.** SYSTEM pulled to the PC after the reboot; `e2fsck -fn` on the grown
  filesystem: completely clean, zero errors, and not even the orphan_file
  flag this time. 6802/532608 files, 176027/1241088 blocks used.
- M5e steps 1-4 are all done: boot image with the lock fix (EVIDENCE 24, 24b),
  write/reboot test, online grow (25, 25a), reboot and fsck (25b).

## 26. M5e step 5: internet through PC NAT, `apk update` works (PASSED 2026-10-20)

**PC side** (runtime only, gone at PC reboot): `ip_forward` = 1; iptables
MASQUERADE for 172.16.42.0/24 out `wlp2s0`; two ACCEPT rules in `DOCKER-USER`
for `enx02a517000002` ↔ `wlp2s0` (Docker's `FORWARD` policy is DROP, so
MASQUERADE alone is not enough).

**Phone side** (RAM only, repeat after every boot):
- Default route `via 172.16.42.2 dev usb0`.
- DNS: a resolv.conf with `nameserver 1.1.1.1` bind-mounted over
  `/etc/resolv.conf`; this got round the conflict with the systemd-resolved
  stub resolv.conf that Jimmy reported.
- Clock set by hand (`date -u -s`): Nura has no RTC and the forced power-off
  never saves the time, so it must be set after each boot before TLS works.
- `a5-diag` is not in Nura (only in the init and rescue image); the MUIC was
  already on USB from the boot, so nothing was needed there.

**Results.** `ping 172.16.42.2` OK, `ping 1.1.1.1` OK, `nslookup
dl-cdn.alpinelinux.org` OK, `sudo apk update` OK: `37182 distinct packages
available`. The apk index cache under `/var/cache/apk` is the only write to
SYSTEM so far; no package was installed.

**Not made persistent yet:** route, DNS and clock live in RAM; the PC rules are
lost at PC reboot. Making them permanent belongs in the device package (an
OpenRC script), through a rootfs rebuild.

## 27. The `reboot` hang: diagnosis so far, and screen banners (2026-10-20)

**Symptom.** `reboot` in Nura: the gadget tears down cleanly, the screen stays
on the Samsung logo, forced power-off (Volume Down + Power) needed. Seen three
times, repeatable.

**Reset path checked (read-only).** `syscon-reboot` (PMU+0x400 = 1) is in the DT
and `POWER_RESET_SYSCON=y`; the reset does happen (logo returns) and the init's
3 s Power hold reboots into TWRP through the same node. No watchdog node, no
power-off handler (separate, not hit yet), PSCI v0.1 without SYSTEM_RESET.
`reboot-mode` writes INFORM3 only for a known command; a plain `reboot` (cmd
NULL) writes nothing. Samsung's `sec_reboot()` writes INFORM2 = 0x12345678 and
INFORM3 = 0x12345670 first.

**Registers** (`mainline/tools/pmu-peek.c`, read-only mmap; `dd` on /dev/mem
gives EFAULT for MMIO on arm64): before the reboot PMU+0x404 = 0x20000000,
INFORM2 (0x808) = 0x12345678, INFORM3 (0x80c) = 0; after the forced power-off
and a normal boot, identical. INFORM2 already holds Samsung's value, INFORM3 = 0
is also the state of a boot that works, so the INFORM theory is unlikely but not
fully excluded (0x12345670 vs 0 is untested).

**USB.** `18d1:4ee2 SAMSUNG_Android` is TWRP (see EVIDENCE 6 and 18), not a
download mode. A first run suggested a slow boot (TWRP at +180 s, then Nura);
that was the forced recovery trip. Hands-off test, `reboot` at 01:20:08 and
6+ minutes untouched: only the gadget teardown, then no USB device at all until
the forced power-off. So it is a real hang before our gadget exists: S-Boot,
U-Boot before `bootm`, or early Linux. Screen: the Samsung logo (first runs;
the last run's description was not given).

**Open.** (1) Plain `reboot -f` from rescue (nothing mounted): not run, the
Volume Down hold missed. (2) If rescue works, `mount -o remount,ro /` + `reboot
-f` in Nura. (3) If rescue hangs too, write INFORM3 = 0x12345670 (PMU only)
before the reboot.

**Banners.** First version (13-row banners at top and bottom, big text
`KERNEL ALIVE`/`RESCUE MODE`, image `b6732974…`): on the phone the green banner
was bigger than wanted, and when Nura's boot output took over the bottom copy
scrolled away (init reset the scroll region before `switch_root`), leaving the
lower part of the login screen blank (old photo
`evidence/hw-mainline-m5e-nura-rw-login-2026-10-19.jpg`: the boot text and
login prompt end about two thirds down the 60-row console).

Second version: one compact banner pinned at the bottom, 7 rows: Tux (11
columns) beside one block-letter word, green `ALIVE` or red `RESCUE`, with a
blank row above. The log scrolls in a scroll region above it (rows 1-52) and
the region is **never reset**, not even at `switch_root`, so the banner should
stay through OpenRC and the login prompt. The top copy and the words `KERNEL`
and `MODE` are gone for compactness. In rescue the banner is repainted in red
without clearing the log. Checked with busybox under qemu: `sh -n` and a crude
screen emulator (rows 54-60, no overflow past 67 columns). **Not verified:**
that nothing in Nura resets the region or clears the screen (busybox `getty`,
OpenRC's consolefont service); a full-screen program on the console that resets
the region can overwrite the banner. If it disappears, the fallback is a
repaint loop started from init (chrooted into the new root, since the
initramfs root is gone after `switch_root`) or an OpenRC service in the
device package.
New image `out/a5y17lte-mainline-m5e-boot.img`, sha256 `661b68aa3582c18193db592822547adadf12901eeae06bcf3cd4a3c0013a732c`
(14,626,832 B; replaces `b6732974…`, `a4eb48ec…` and `e38f49cd…`, all in git
history).

Third version (Jimmy's mockup): the banner is a pixel-art Tux 13 rows tall
(2 console cells per pixel: rounded head, two eye holes, yellow beak, hollow
belly, flippers, yellow feet) with the label to its right in block letters,
two stacked words vertically centred beside it: `KERNEL` / `ALIVE` (green) or
`RESCUE` / `MODE` (red; the penguin is red too in rescue). Two copies: one
drawn at rows 1-13 right after the screen is cleared at init start (it scrolls
away with the log), one pinned at rows 48-60 with the log in a scroll region
above it (rows 1-46, never reset, so it should stay through Nura's boot as in
the second version, still unverified on the phone). Deviations from the
mockup: the belly is a black hole, not white, and the beak and feet are the
console's yellow-brown, because "the whole penguin green" was also asked (the
console has no orange; the belly could be white, at the cost of a green-only
penguin). Checked with busybox under qemu: `sh -n`, a screen emulator
rendering both copies (widest column 64 of 67; drawing takes about 0.8 s under
qemu, so far less on the phone). Image `out/a5y17lte-mainline-m5e-boot.img`,
sha256 `2cd79c35dc22e0e48ad3049b1b2e2c0831ebdce26aa768ffd92d839adbeca4eb` (14,626,832 B), replacing `661b68aa…`.

Phone result for the third version: penguin, colours and the `KERNEL ALIVE`
text look right, but each copy (13 rows) takes too much of the screen. A
garbled a5-diag info block in one photo was a frame caught mid-update (Jimmy),
not a bug.

Fourth version: each copy is 7 rows (was 13): Tux 12 pixels wide at 1 console
cell per pixel (head, two eye holes, yellow beak, flippers, hollow belly,
yellow feet) and the label on one line beside it in 3x5 block letters (5 rows,
vertically centred), `KERNEL ALIVE` green or `RESCUE MODE` red, about 62
columns wide in all. Same layout otherwise: a copy at rows 1-7 right after the
clear, one pinned at rows 54-60, the log in a scroll region (rows 1-52) that
is never reset. Checked with busybox under qemu: `sh -n` and the screen
emulator (both copies render, letters legible). Image
`out/a5y17lte-mainline-m5e-boot.img`, sha256 `2156feb8149ad1b6512cb7319f821c95144521947a0911243cc0bb87b3b0a9c8 (built from commit 2980aa8; the build time stamp comes from the HEAD commit, so rebuilding at a later commit gives other bytes)` (14,626,832 B), replacing
`2cd79c35…`.

Fifth version (Jimmy: same colours, about half the size again, and the label
`KERNEL ALIVE` renamed `KERNEL MODE`): each copy is 4 rows (was 7): a Tux 10
pixels wide at 1 console cell per pixel (head with two eye holes, yellow beak,
flippers with a hollow belly, yellow feet) and the label on one line beside
it, at the face row, in plain bright white text on a green (`KERNEL MODE`) or
red (`RESCUE MODE`) bar, about 36 columns wide in all. The 3x5 block letters
are gone (5 rows tall, too big for 4 rows), so the label is now ordinary
console text, not block letters. Layout otherwise unchanged: a copy at rows
1-4 right after the clear, one pinned at rows 57-60, the log in a scroll
region (rows 1-56) that is never reset. Checked with busybox under qemu: `sh
-n` and the screen emulator (both copies render). Image
`out/a5y17lte-mainline-m5e-boot.img`, sha256 `88aa19ce34967cd6b200f55e5a5eaf524d1750e4e7db5b46f4dd770f904593c8` (14626832 B), built at commit
869f03c (the build time stamp comes from the HEAD commit), replacing `2156feb8…`.

Sixth version (Jimmy: penguin 6 rows tall instead of 4, the rest the same): the
Tux is 10 pixels wide and 6 rows tall (rounded head, two eye holes, yellow
beak, flippers around a hollow belly, yellow feet); the banner is therefore 6
rows: copies at rows 1-6 and 55-60, scroll region rows 1-53. The label stays
on one line beside it, now at the beak row (row 3 of 6, was row 2 of 4), same
text and colours. Checked with busybox under qemu: `sh -n` and the screen
emulator. Image `out/a5y17lte-mainline-m5e-boot.img`, sha256 `996ed3645d518f97b9b89f78443083e3b40e582a1bad3cb0e906555836f494c9` (14626832 B),
built at commit a970597, replacing `88aa19ce…`.

Seventh version (Jimmy: penguin 10 rows tall instead of 6, the rest the same):
the Tux is 12 pixels wide at 2 console cells per pixel (24 columns, so the
pixels stay square) and 10 rows tall; the banner is therefore 10 rows: copies
at rows 1-10 and 51-60, scroll region rows 1-49. Shape: head, two eye holes,
yellow beak, flippers around a hollow belly, yellow feet. The label stays on
one line beside it at the beak row (row 4 of 10), same text and colours.
Checked with busybox under qemu: `sh -n` and the screen emulator (widest
column 53 of 67). Image `out/a5y17lte-mainline-m5e-boot.img`, sha256 `4b6fe1b0fef7061a852aca6d692d62f12ad0a3684008cb2ad04bc7197fa55116`
(14626832 B), built at commit 55ed892, replacing `996ed364…`.

## 28. Reboot hang: INFORM breadcrumb test, and `reboot recovery` also hangs (2026-10-22/23)

**Test A: breadcrumb through the rescue-mode Power hold.** In rescue mode
`pmu-poke` wrote PMU+0x81c = 0xdeadbeef. Then Power was held 3 s. That is not a
hardware reset: `mainline/init/init.c:113-120` (and `a5-diag.c:466-471`) call
`reboot(LINUX_REBOOT_CMD_RESTART2, "recovery")`, so it goes through
reboot-mode (INFORM3 = 0x12345674) and syscon-reboot (PMU+0x400). **It worked
and the phone reached TWRP** (the same result as in EVIDENCE 27). TWRP has no
/dev/mem node, so the registers could only be read after TWRP -> Reboot ->
System into Nura:

| Register | Value after TWRP and a boot to Nura |
|---|---|
| PMU+0x81c (breadcrumb) | 0x00000000 (written 0xdeadbeef: **did not survive**) |
| PMU+0x404 | 0x20000000 (same as EVIDENCE 27) |
| INFORM2 (0x808) | 0x12345678 (Samsung's sec_reboot() marker, not written by us; same as EVIDENCE 27) |
| INFORM3 (0x80c) | 0x00000000 |

Two resets (rescue -> TWRP, TWRP -> Nura) and two Samsung-side stages (S-Boot,
TWRP) sit between the write and the read, so the cleared breadcrumb does not say
which of them cleared it. Not informative about the hang.

**Test B: `sudo reboot recovery` inside Nura.** Same syscall as the Power hold in
Test A. **It hung at the Samsung logo**, like plain `reboot`; forced power-off
needed.

**Reading.** The reset path is the same in A and B (RESTART2 "recovery" ->
reboot-mode -> syscon-reboot), and A works while B hangs. So this does not
contradict EVIDENCE 27: that entry only showed that the path works from the
rescue init, and never that a named command works from Nura. What differs is
the state of the system at reset time (Nura: OpenRC, rw ext4 on mmcblk0p19, more
drivers bound, other shutdown callbacks), not the reset mechanism or the
reboot-mode value. The INFORM3 theory is now close to excluded: the value
0x12345674 is written in A and B alike. The breadcrumb test cannot decide it
further.

**Untested from EVIDENCE 27's open list:** (1) plain `reboot -f` from rescue;
(2) `mount -o remount,ro /` + `reboot -f` in Nura.

**Addendum (2026-10-23): plain `reboot -f` from rescue mode also hangs.** Samsung
logo for about 5 minutes, forced off with Volume Down + Power 10 s. (EVIDENCE 27
lists this test as not run; Jimmy says an earlier session had already seen it,
but it is recorded here as a run result.) Matrix so far:

| Where | Command | Result |
|---|---|---|
| rescue | `reboot(RESTART2, "recovery")` (Power hold) | works, TWRP |
| rescue | plain / `-f` (cmd NULL, INFORM3 untouched) | hangs |
| Nura | plain `reboot` | hangs |
| Nura | `reboot recovery` | hangs |

So the environment is not the only variable: a named command works in rescue
and a plain reboot does not. The earlier statement "INFORM3 theory close to
excluded" is withdrawn; it only held for the recovery case. Nura + recovery
hanging is still unexplained by that alone.

**Addendum 2 (2026-10-23): INFORM3 = 0x12345670 written from rescue, then `reboot -f`: still hangs at the Samsung logo.**
Forced off. The INFORM3-marker theory is disproven for the plain reboot. Only
RESTART2 "recovery" from rescue has been seen to work (INFORM3 = 0x12345674, and
S-Boot then boots TWRP). Not yet known whether the hang is in S-Boot or in our
U-Boot/Linux on a warm start: step 1b's uniLoader rebooted by PMU SWRESET every
30 s and came back each time (EVIDENCE 12), and TWRP -> Reboot -> System boots
our image, so a warm start of our payload can work; what is different after Linux
has run is the hardware state it leaves behind.

## 29. `reboot download` was a no-op: busybox `reboot` cannot pass a command (2026-10-25)

**Observation (Jimmy).** In rescue mode `reboot download` did nothing: no
reboot-mode notifier output in dmesg, and the phone stayed in rescue mode.

**Reading of the sources.**
- `mainline/init/init.c` has no reboot-mode notifier. It only calls
  `reboot(RESTART2, "recovery")` (init.c:113-120). The notifier is the kernel's
  `syscon-reboot-mode` from `mainline/dts/exynos7880.dtsi:197-203`:
  bootloader = 0x1234567d, download = 0x12345671, recovery = 0x12345674, written
  to INFORM3 (PMU+0x80c). Any other string, or NULL, writes nothing.
- The busybox build (`upstream/userspace/busybox/.config`, 1.36.1 defconfig) has
  `# CONFIG_FEATURE_CALL_TELINIT is not set`. Then `reboot` without `-f` does
  `kill(1, SIGTERM)` and ignores its argument. PID 1 (init.c, a5-diag keys)
  installs no signal handler, so nothing happens. This matches the no-op. I
  recalled busybox's halt behaviour from memory and did not read its source.
- Consequence: no busybox `reboot <cmd>` has ever passed a command. The
  Nura `reboot recovery` hang in EVIDENCE 28 Test B may have been a plain
  reboot. Not checked (Nura's reboot is not busybox).

**Change.** `a5-diag reboot CMD` (CMD = download, recovery or bootloader) calls
`reboot(RESTART2, CMD)` directly. Built as
`out/a5y17lte-mainline-m5f-boot.img`, sha256 `de085b24…a60f24`. The tracked
m5e image is unchanged. NOT yet run on the phone.

**Planned test.** (1) `a5-diag reboot recovery` from rescue (control; the Power
hold already reaches TWRP). (2) `a5-diag reboot download`: Odin screen means
S-Boot download mode works; a hang at the logo means S-Boot or the hardware
state after Linux, not the mode string.

**Result (Jimmy, 2026-10-25): `a5-diag reboot download` PASSED.** From rescue mode on
`m5f`, the phone went to S-Boot's real Odin screen ("Downloading... Do not turn
off target", teal background, download icon). So RESTART2 "download" ->
reboot-mode (INFORM3 = 0x12345671) -> syscon-reboot (PMU+0x400) works, and
S-Boot runs normally after our Linux has run.

Photo: `evidence/hw-mainline-m5f-odin-download-2026-10-25.jpg` (the Odin download screen).

**Reading.** A warm reset after our Linux is not a general hardware or S-Boot
failure: S-Boot starts and acts on INFORM3 (download here, recovery -> TWRP in
EVIDENCE 27/28). The hang (plain reboot, `reboot -f`, INFORM3 = 0x12345670 in
EVIDENCE 28) is on the path where S-Boot boots our own boot.img (our U-Boot and
Linux), after Linux has run. TWRP -> Reboot -> System starts the same image
fine, so what matters is the hardware state our Linux leaves behind, or what
S-Boot does for a normal boot given that state. Not yet separated: whether the
hang is in S-Boot before it hands over, in U-Boot, or in the kernel early boot.
The control `a5-diag reboot recovery` (run before the download test, Jimmy) landed in TWRP, same as the Power-hold path.

**Odin exit test (Jimmy, 2026-10-25): hangs too.** On the "Downloading..." screen
Volume Down did nothing (that screen has no cancel; the cancel key belongs to
the warning screen, which `reboot download` skips). Volume Down + Power held
together rebooted the phone to the Samsung logo, where it stayed about 5
minutes; forced off again. Matrix addition:

| Where | Command | Result |
|---|---|---|
| Odin (S-Boot download mode) | Volume Down + Power | hangs at the logo |

Caveat: the keys may still have been held when S-Boot sampled them after the
reset, so this is not a clean "normal boot" test. What it does show: the hang
does not need our Linux's shutdown path, since S-Boot itself was running between
our Linux and this boot. Whatever our Linux leaves behind survives an Odin
session (PMIC/MUIC registers, eMMC card state, DRAM contents, PMU INFORM
registers), or S-Boot's normal-boot path breaks after any reset that is not a
power cut. A forced power-off followed by power-on boots fine.

**Baseline PMU dump after a clean cold boot into rescue (Jimmy, 2026-10-25),
`pmu-peek`:** 0x404 = 0x20000000, 0x800 = 0, 0x804 = 0, INFORM2 0x808 =
0x12345678, INFORM3 0x80c = 0, 0x810 = 0, 0x814 = 0x00003100 (new, meaning
unknown), 0x818 = bus error (pmu-peek died there; 0x81c not read). The fault is
probably a secure-world-only register; not verified.

**Unplugged-cable test (Jimmy): hangs too.** USB cable pulled before
`reboot -f` from rescue: still the Samsung logo for the usual time. So cable
and charger detection are not the cause. The MUIC *register* state (manual mode
set by `a5-diag muic`) survives a reset and is not excluded by this.

**Next: U-Boot-only reset test** (`mainline/boot-reset-test.cmd`: banner, 8 s,
`reset`, no Linux). Loops back to the banner: Linux leaves the bad state, then
bisect drivers. Hangs at the logo: U-Boot or the reset is at fault.

**U-Boot-only reset test v1 (Jimmy, 2026-10-25): Samsung logo from the first
reset on, banner never seen. INVALID as a test, my mistake.** The built U-Boot
(`build/u-boot/.config`) has no video driver (no CONFIG_VIDEO), `# CONFIG_SYSRESET
is not set`, and on arm64 `reset_cpu()` in `arch/arm/mach-exynos/soc.c` is empty
(it only writes SWRESET `#ifdef CONFIG_CPU_V7A`). So `echo` shows nothing on the
screen, and `reset` did not reset: the phone most likely sat in U-Boot with the
Samsung logo still up. The conclusion "not Linux" does not follow. (Not run
after: whether the phone was really still in U-Boot.)

**Consequence for the real hang.** In the real image the screen also stays on
the Samsung logo until Linux draws its console, so "logo for 5 minutes" does
not say where the boot stopped: S-Boot, our U-Boot, or early Linux.

**v2 (`mainline/boot-reset-test.cmd`, image `ubreset2`, not yet flashed).** Draws
a red band into S-Boot's framebuffer (0x67000000), `dcache off` (CMD_CACHE is
enabled only for test builds, via BOOTCMD), waits 8 s, then does the PMU
SWRESET write (`mw.l 0x106b0400 1`). A band after the logo means our U-Boot ran.

**v2 result (Jimmy, 2026-10-25): no red band at all, only the Samsung logo.
INVALID again: my script never triggered DECON.** The panel is MIPI command mode
(EVIDENCE 11): it only shows new framebuffer content after a DECON trigger
(`HW_SW_TRIG_CONTROL` 0x14830070 = 0x10 in `a5-diag panel`, proven EVIDENCE 13).
v2 wrote the framebuffer and flushed the cache but did not trigger, so no band
could appear whether or not U-Boot ran. It says nothing about the hang. EVIDENCE
11 already had this; I should have applied it.

**v3 (`ubreset3`).** Same script plus `mw.l 0x14830070 0x10` after the draw. Same
reading of outcomes as v2. Note the panel keeps its own frame memory: if S-Boot
never redraws after the reset, a stale band stays instead of the Samsung logo.

**v3 result (Jimmy, 2026-10-25): an orange/red band at the top, together with the
Samsung logo, completely static for about 5 minutes; never cycled, never
vanished, never came back.** Forced off. Reading, with the limits: U-Boot ran
(the band exists) and `dcache off` + DECON trigger work, so the framebuffer
method is good. But the picture does not tell whether the reset happened. (a)
The SWRESET write from U-Boot did not reset and U-Boot sat at its prompt after
the script ended; or (b) it reset, S-Boot drew its logo without clearing the
framebuffer in DRAM (which still held our band), and then never started our
U-Boot again. Both look identical. Not concluded.

**v4 (`ubreset4`)** separates them: green stripes every second for 10 s after the
reset write (stripes = no reset), and a DRAM marker that turns the next boot's
band blue (blue = S-Boot started our U-Boot again).

**v4 result (Jimmy, 2026-10-25): red band, no green stripes, then frozen; no second
boot and no logo return. Forced off.** The mapping in my table ("reset happens,
S-Boot never restarts us, no Linux involved") is too strong. Established: U-Boot
ran and the SWRESET write stopped U-Boot (no stripes). Not established: that
the SoC reset. A frozen picture is also what a command-mode panel shows after a
dead reset path, or after an abort in U-Boot's write. One detail does not match
the Linux hangs: there the Samsung logo came back (S-Boot ran), here it did not.

Contrast to keep in view: step 1b's uniLoader did a plain PMU SWRESET from EL1
every 30 s and S-Boot started it again each time (EVIDENCE 12). So a plain
SWRESET is not poison by itself; U-Boot and Linux both leave something
different behind. Candidates: DRAM use (U-Boot relocates to the top of DRAM and
`dram_init()` probes every 256 MB bank with `get_ram_size()`, which writes;
uniLoader stays at 0x47000000), MMU/cache state, secure-monitor calls. Not
tested.

**v5 (`ubreset5`)**: stage stripes, then INFORM3 = recovery before SWRESET.

**v5 result (Jimmy, 2026-10-25): red band, one green stripe, then frozen; no blue,
no white stripes, no reboot; Samsung logo visible with the stripes. Forced off.**

**Correction: v3, v4 and v5 told us nothing about reset, and my v5 guide was
wrong.** The green stripe is drawn *after* the INFORM3 write, so it proves the
PMU write works; "stripes up to green = INFORM3 write failed" was my error. What
stops U-Boot after green is the next command, `sleep 3`. This U-Boot has no
working timer: CNTFRQ_EL0 = 0 at EL1 (EVIDENCE 11), `CONFIG_ARMV8_CNTFRQ_BROKEN`
is not set (`build/u-boot/.config:222`), and `get_tbclk()`
(`arch/arm/cpu/armv8/generic_timer.c:19-28`) just returns the register. So the
tick-to-time division gives 0, `get_timer()` never advances, and `sleep` loops
forever (derived from the source, not observed). That fits every run: v3 (band +
logo, static, sat in `sleep 8`), v4 (no stripes, `sleep 8`), v5 (hung in `sleep
3`). The SWRESET write was never executed in any of them, and the Samsung logo
stayed because no reset ever happened. The earlier "S-Boot never restarts us" and
"MMU mapping problem" readings are both withdrawn.

Side finding: U-Boot's `sleep`/`udelay` do not work on this board. Harmless
while U-Boot only boots Linux; fix later with `CONFIG_ARMV8_CNTFRQ_BROKEN` and
a rate from the DT.

**v6 (`ubreset6`)** has no `sleep`: it waits by filling scratch DRAM, then
INFORM3 = recovery and SWRESET as in v5.

**v6 result (Jimmy, 2026-10-25): red, green, blue stripe, then the phone RESET and
landed in TWRP; no hang.** So a PMU SWRESET written from our U-Boot (no Linux),
with INFORM3 = 0x12345674, works, and S-Boot runs the recovery path. Timing:
the `for` loop delays work where `sleep` did not.

Limits of this result. It was a *named* reset (recovery). The plain reset from
U-Boot is not tested, so "plain reset hangs in S-Boot" is still the old
Linux-side observation, not something v6 shows. And uniLoader's plain SWRESET
loop worked (EVIDENCE 12), so a plain reset is not poison for every payload.

**v7 (`ubreset7`)**: same as v6 but INFORM3 = 0 (plain), with the DRAM marker
(second boot draws blue). Loops if it works.

**v7 result (Jimmy, 2026-10-25): PLAIN reset from our U-Boot, no Linux, LOOPS.**
The red band, the green stripe and the white stripe appeared fresh on every
cycle (about one cycle per second, which is faster than I expected; Jimmy
confirmed the bands, so it is our U-Boot running each time, not S-Boot looping
alone). The Samsung logo showed between cycles; no hang. The marker did not
turn the band blue, so S-Boot or the reset clears that DRAM/framebuffer word.

**Conclusion so far.** A plain SWRESET (INFORM3 = 0) into our U-Boot image works
every time when Linux has not run. The hang in EVIDENCE 27/28 needs Linux to
have run before the reset. The reset mechanism, S-Boot's normal-boot path and
our U-Boot image are not the cause by themselves. What Linux changes is open:
hardware registers that survive SWRESET (PMIC/MUIC, eMMC card state, PMU
scratch), DRAM contents, or the way Linux stops.

**Bisect (this entry onward).** `mainline/rootfs/init` has `stage_check N`:
with `a5.stage=N` on the kernel command line, init stops after step N and runs a
plain `reboot -f`. Build with `STAGE=N MILESTONE=stageN ./build-mainline.sh`
(adds `setenv bootargs "<DT bootargs> a5.stage=N"` to the boot script).
Stages: 0 = kernel up + `a5-diag panel` only; 1 = after `a5-diag info/system`
(eMMC read, lock); 2 = after `a5-diag usb` (USB PLL/PHY pokes); 3 = after
`a5-diag android`; 4 = after the USB gadget bind; 5 = after muic + usbregs.
Reading: loops every few seconds = this stage is fine; Samsung logo for a
minute or more = the hang is at or before stage N. Start with 0: if it hangs,
the kernel's own drivers are enough and userspace is innocent; if it loops, go
to 2, then 4, then 5.

**Stage 0 result (Jimmy, 2026-10-25): "STAGE 0 reached" printed, then the plain
`reboot -f` HUNG at the Samsung logo (no loop, no recovery). Forced off.** So the
hang needs only the kernel plus init's mounts and `a5-diag panel`; the later
userspace steps (`usb`, `muic`, ...) are not needed. (Not strictly "userspace
innocent": init's mounts and the panel trigger are still in the picture, but
they touch no hardware that survives a reset, as far as I know.)

**Kernel bisect, by `initcall_blacklist=` (no DT change).** `build-mainline.sh`
now takes `BARGS` (extra kernel arguments, added to the DT bootargs by the boot
script) next to `STAGE`. Names from `upstream/linux/System.map`. Stage 0 needs
no disk, so the storage and USB drivers can be left out.

Test `bl1` (STAGE=0): blacklist `dw_mci_exynos_pltfm_driver_init`,
`dw_mci_pltfm_driver_init` (eMMC), `dwc3_exynos_driver_init`,
`dwc3_driver_init`, `exynos5_usb3drd_phy_init` (USB), `gpio_keys_init`,
`i2c_gpio_init`. Loops = the cause is among those; then split (eMMC / USB /
rest). Still hangs = core drivers (pinctrl, CMU, PMU, power domains, timers) or
DRAM use; then DT node bisect and a `mem=` test.

**bl1 result (Jimmy, 2026-10-25): "STAGE 0 reached" printed, then the plain reboot
HUNG at the Samsung logo again.** eMMC (dw_mci), USB (dwc3 + PHY), gpio-keys
and i2c-gpio are not needed for the hang (their initcalls were blacklisted).

What is left at stage 0 is small (`mainline/dts/exynos7880*.dts*`): 8 Cortex-A53
CPUs brought up by PSCI v0.1 (calls into Samsung's secure monitor), GIC, arch
timer, the two pinctrl blocks, CMU_FSYS, the PMU syscon (syscon-reboot and
-mode), fixed clocks/regulators, simpledrm/fbcon, and all of DRAM.

Next, one change at a time, cheapest first:
- `nosmp` (image `nosmp`): no secondary CPU is started, so no PSCI CPU_ON.
  Linux stops the other cores with an IPI at restart but never powers them off
  through PSCI, which Samsung's own kernel may do. Loops = SMP/PSCI is the cause.
- `mem=512M` (image `mem512`): Linux only touches the first 512 MB of DRAM.
  Loops = Linux scribbles on a DRAM area the next boot needs; then bisect by size.
  Caveat: a named reset (recovery/download) works after Linux, so any theory
  must explain why only the plain path breaks.

**nosmp result (Jimmy, 2026-10-25): "STAGE 0 reached" printed, then the plain reboot
hung at the Samsung logo (about 5 minutes, forced off). Same as stage 0 and bl1.**
Photo: `evidence/hw-mainline-nosmp-2026-10-25.jpg`.

**Correction to my earlier message.** Jimmy first described the photo as a
"second boot attempt" (a second KERNEL MODE banner and an mmc_host line). I
treated that as a change in behaviour. That was wrong. The photo shows ONE boot:
init draws a banner at the top and pins a second copy at the bottom
(`mainline/rootfs/init`, banner comment), the "STAGE 0 reached: plain reboot -f
in 3 s" line sits below the top banner, and the mmc_host line is stamped
`[ 0.304065]`, an early kernel message of the same boot, shown on the last row of
the scroll region. The frame was taken before the reboot; no Samsung logo is in
it. So `nosmp` did not change anything: the first plain reboot hangs.

**Reading.** Starting no secondary CPU does not help, so PSCI CPU_ON and the
parked secondaries are not the cause (the boot CPU still runs, and the
PSCI/monitor path is otherwise unchanged). Still open: DRAM use (`mem512`,
built), and the core drivers (CMU, pinctrl, PMU, GIC/timer, simpledrm/fbcon,
regulators).

**mem512 result (Jimmy, 2026-10-25): the usual single-boot screen (STAGE 0 line +
pinned banner), then the plain reboot hung at the Samsung logo.** So Linux using
DRAM above 512 MB is not the cause. (It still uses the first 512 MB, including
S-Boot's own area around 0x43e00000, EVIDENCE 11; that stays open.)

**Why bisect inside the kernel instead of by driver.** Facts so far: plain reset
from U-Boot alone loops (v7); a named reset (recovery, download) after Linux
works; a plain reset after Linux hangs, with every driver group tried so far
removed (eMMC, USB, keys, i2c, SMP) and with DRAM use cut to 512 MB. One more
hint from the code: `reboot -f` and the named resets run the same
`kernel_restart()` path in Linux (device shutdown, syscon-reboot), so that path
is not what differs; INFORM3 is.

**Checkpoint bisect.** `mainline/debug/a5-rstlvl.patch` (debug only, applied for
the build when `RSTLVL=N` is set, then reverted; never committed to the kernel
branch) adds `a5rst=N` to the kernel command line: the kernel writes PMU SWRESET
itself (0x106b0400 = 1, after a 300 ms delay) at checkpoint N: 10 = start of
`kernel_init_freeable()`, 0-7 = after initcall level N (core, postcore, arch,
subsys, fs, device, late), 8 = after all initcalls, before userspace. INFORM3 is
left alone (plain reset). Build: `RSTLVL=N MILESTONE=rstN ./build-mainline.sh`.
Loops (logo, then the console banner again, repeatedly) = fine up to there;
hang at the logo = the cause is at or before checkpoint N. Start with 8
(everything in the kernel, no userspace, no `reboot` syscall).

**rst8 v1 result (Jimmy, 2026-10-25): nothing readable on screen, a brief
flash/flicker, then straight to the Samsung logo, stuck (about 5 minutes, forced
off). INVALID, the same trap as v2 of the U-Boot test.** The panel is
command-mode and only updates after a DECON trigger (EVIDENCE 11, 13); that
trigger is set by `a5-diag panel` in init, and checkpoint 8 runs before
userspace. So nothing could be seen whether or not the checkpoint was reached,
and a logo that stays does not show a reset happened.

**rst8 v2** (image `rst8`, sha256 `cc11bd9a…fa18d4`, replaces v1): at the
checkpoint the kernel draws a white stripe into S-Boot's framebuffer
(0x67000000, row 130 + 70 * N, 40 rows; row 60 for checkpoint 10), writes the
DECON HW trigger (0x14830070 = 0x10), waits 2 s, then writes PMU SWRESET. A
stripe on screen = the checkpoint was reached. A loop = a stripe, the logo,
the console, a stripe, ... (the U-Boot band does not appear here; only the
kernel's stripe).

**rst8 v2 result (Jimmy, 2026-10-25): checkpoint 8 REACHED, then the plain reset
hung at the Samsung logo (no loop).** Photo `evidence/hw-mainline-rst8-checkpoint8-2026-10-25.jpg`:
the kernel log shows `[0.179777] a5rst: checkpoint 8, PMU SWRESET in 2 s`, after
all initcalls (`clk: Not disabling unused clocks` just before it) and before any
userspace. The eMMC was probed on another CPU meanwhile (0.24-0.31 s: "new high
speed DDR MMC card", mmcblk0 p1-p25). No white stripe is visible in the photo;
likely the console scroll repainted over it (not checked), so the log line is
the proof. The whole kernel boot, from entry to this point, takes 0.18 s.

**Conclusion.** Booting the kernel to the end of its initcalls, with no
userspace and no `reboot` syscall, is enough: a plain SWRESET after it hangs,
while a plain SWRESET from U-Boot alone loops (v7). The cause is therefore
inside the kernel's own boot, between U-Boot's `bootm` and checkpoint 8.
INFORM3 is not the issue for a plain reset here; the `reboot -f` path is not
either.

**Next checkpoints** (`mainline/debug/a5-rstlvl.patch` extended): 11 = in
`start_kernel()` right after `setup_arch()` (fixmap mappings, counter-based
2 s wait), 10 = start of `kernel_init_freeable()`, 0-7 = after initcall level N.
Images `rst10` and `rst11` are built. Order: 10 first (midpoint). Hang at 10:
test 11, then head.S. Loop at 10: bisect levels 0-7 (all within about 0.15 s of
boot).

**rst10 result (Jimmy, 2026-10-26): white stripe appeared (checkpoint 10
reached, start of `kernel_init_freeable()`), then the plain reset hung at the
Samsung logo, no loop.** So the cause is at or before checkpoint 10, i.e. in
head.S / `start_kernel()` / `setup_arch()` up to `rest_init()`. Next: `rst11`
(checkpoint right after `setup_arch()`). Hang at 11: cause is in head.S or
`setup_arch()` (or what U-Boot leaves behind); loop at 11: cause is between
`setup_arch()` and `kernel_init_freeable()`.

**rst11 result (Jimmy, 2026-10-26): white stripe appeared (checkpoint 11, right
after `setup_arch()`), then the plain reset hung at the Samsung logo, no loop.**
The cause is at or before checkpoint 11.

**Next: `rststub`, U-Boot's hand-off without any kernel.** v7 reset from the
start of U-Boot, before `bootm`, so it never tested the state `bootm` leaves
(device removal, cache/MMU cleanup, DT fixups). `rststub` (`STUB=1 MILESTONE=rststub
./build-mainline.sh`) replaces the kernel Image in the FIT with a 192-byte arm64
stub (`mainline/debug/rst-stub.S`): `bootm` runs unchanged and jumps to it; the
stub draws a white stripe (row 600), triggers DECON, waits 2 s and writes PMU
SWRESET (plain). Loops (stripe, logo, console, stripe, ...) = the hand-off is
fine, the cause is in the kernel's early boot (head.S, `setup_arch()`). Hangs at
the logo = `bootm`'s hand-off state is the cause; the kernel is innocent.

**rststub result (Jimmy, 2026-10-26): white stripe appeared, then the plain reset
hung at the Samsung logo, no loop.** With no Linux code at all, U-Boot's `bootm`
hand-off followed by a plain SWRESET hangs, while a plain SWRESET from U-Boot
before `bootm` loops (v7). So the cause is in what `bootm` does: the
kernel is innocent (all the kernel checkpoints 8, 10, 11 hang for the same reason).

**Bisect inside `bootm`** (`mainline/debug/a5-ubrst.patch`, debug only, applied for
the build when `UBRST=N` is set, then reverted): `boot_jump_linux()` resets itself
(stripe at row 130 + 70 * N, DECON trigger, 2 s counter wait, PMU SWRESET) at point
N. 1 = start of `boot_jump_linux()` (everything before it is done: FIT load and
hash, DT fixups, ramdisk relocation); 2 = after `bootm_final()` (device removal,
`board_quiesce_devices`); 3 = after `cleanup_before_linux()` (interrupts, icache
and dcache off, MMU off). Not covered by 1-3: PSCI/EL switch and the jump (the
stub reached its stripe, so they work). Build: `UBRST=N MILESTONE=ubrstN
./build-mainline.sh`. Loop = fine up to there; hang = the cause is at or before N.
Order: 2 first (midpoint). Hang at 2: test 1; loop at 2: test 3.

**ubrst2 result (Jimmy, 2026-10-26): stripe appeared (higher on screen, as
expected for row 270), the screen went briefly black, then the plain reset hung
at the Samsung logo, no loop.** The cause is at or before `bootm_final()`: the
cache/MMU cleanup and the jump are not needed. Next: `ubrst1` (start of
`boot_jump_linux()`, i.e. after FIT load, hash, DT fixups and ramdisk
relocation). Hang at 1: the cause is in `bootm`'s preparation (memory written
by the load/relocation, DT fixups). Loop at 1: the cause is in `bootm_final()`
(`board_quiesce_devices`, `dm_remove_devices_active`).

**ubrst1 result (Jimmy, 2026-10-26): stripe (higher up, as expected), black,
then the plain reset hung at the Samsung logo, no loop.** The cause is in
`bootm`'s preparation, before `boot_jump_linux()`: FIT load and hash, DT fixups
or ramdisk relocation. (v7 never loaded or relocated anything.)

**Bisect of the preparation, from the boot script** (`bootm` has sub-commands, so
no U-Boot patch is needed): `mainline/boot-prep{C,A,B}.cmd` run part of `bootm`,
then the v7 reset test (stripes, plain SWRESET), all with the stub FIT (STUB=1).
C = `bootm start` + `loados` (FIT parse, hashes, kernel copy to 0x48000000);
A = C + `ramdisk` (relocation); B = A + `fdt` (copy, fixups). Loop = fine up to
there; hang = cause at or before. Order: A first. Hang at A: test C. Loop at A:
test B. Build: `STUB=1 BOOTCMD=boot-prepX.cmd MILESTONE=ubprepX ./build-mainline.sh`.

**ubprepA result (Jimmy, 2026-10-26): red, green and white bands appeared, then
the plain reset hung at the Samsung logo, no loop.** The cause is in `bootm
start`, `loados` or `ramdisk` (relocation); the DT step is not needed. Next:
`ubprepC` (start + loados). Hang at C: FIT parse/hash or the kernel copy; loop at
C: the ramdisk relocation.

**ubprepC result (Jimmy, 2026-10-26): red, green, white bands, then the plain
reset hung at the Samsung logo, no loop.** The cause is in `bootm start` or
`loados` (FIT parse and hash, kernel copy to 0x48000000). `ramdisk` and `fdt` are
not needed.

**Next: what does `loados` touch?** Candidates: (1) the write to DRAM at
0x48000000 (the FIT's load address; DRAM may hold something that survives a
reset, or secure/S-Boot data); (2) the FIT parse and hash (reads only). Images
(v7 reset test after one command, `mainline/boot-mem{1,2,3}.cmd`):
`ubmem1` = `mw.l 0x48000000 ... 0x30` (192 bytes, like the stub), `ubmem2` = the
same at 0x50000000, `ubmem3` = `iminfo` of the FIT (hashes, read only). Order: 1.
Hang at 1: DRAM at 0x48000000 is off limits, then 2 shows whether it is only that
region. Loop at 1: test 3 (hash) and then `bootm start` alone.

**ubmem1 result (Jimmy, 2026-10-26): red, green, white bands, then the plain
reset hung at the Samsung logo, no loop.** A 192-byte DRAM write at 0x48000000,
with no `bootm` at all, is enough to make the next plain reset hang. This is the
cause of the whole EVIDENCE 27-29 hang (the kernel's own first words land at
0x48000000 too). Next: `ubmem2` (same write at 0x50000000) to see whether only
that region is affected.

**ubmem2 result (Jimmy, 2026-10-26): LOOPS (red, green, white bands repeating,
no hang).** The same 192-byte write at 0x50000000 is harmless; the one at
0x48000000 is not. Side note: with the loop running, Volume Down + Power did not
power the phone off (it did on the hanging images) and the loop just carried on;
a loop can only be left through the recovery key combo at the logo (or by
flashing from another way).

**Mapping the edges** (same test, other addresses; `mainline/boot-mem{4..7}.cmd`):
`ubmem4` 0x4c000000, `ubmem5` 0x49000000, `ubmem6` 0x48100000, `ubmem7`
0x47fff000 (just below the known-bad address). Known: 0x48000000 bad, 0x50000000
ok. Order: 4 first, then bisect the bracket with 5/6; 7 shows whether the region
starts exactly at 0x48000000.

**ubmem4 result (Jimmy, 2026-10-26): LOOPS.** The write at 0x4c000000 is fine, so
the bad region's upper edge is between 0x48000000 and 0x4c000000. Next: `ubmem5`
(0x49000000).

**ubmem5 result (Jimmy, 2026-10-26): LOOPS.** The write at 0x49000000 is fine, so
the bad region lies within 0x48000000-0x49000000. Next: `ubmem6` (0x48100000).
How the loop was left: Download mode on purpose, then Volume Down (cancel); that
reboot stuck on the plain Samsung logo (no bands, same kind of hang); forced off
with Volume Down + Power, then Volume Up + Power into TWRP.

**ubmem6 result (Jimmy, 2026-10-26): LOOPS.** The write at 0x48100000 is fine, so
the bad region lies within 0x48000000-0x48100000 (1 MiB). The recovery combo
(Volume Up + Home + Power) at the logo left the loop cleanly into TWRP this time.
Next: `ubmem7` (0x47fff000) for the lower edge.

**ubmem7 result (Jimmy, 2026-10-26): LOOPS.** The write at 0x47fff000 is fine, so
the bad region starts at 0x48000000 or just above it, and ends before
0x48100000.

**Bisect inside 0x48000000-0x48100000** (`mainline/boot-mem{8..11}.cmd`): `ubmem8`
192 B at 0x48001000, `ubmem9` 192 B at 0x48010000, `ubmem10` 192 B at
0x48080000, `ubmem11` only 96 B (0x18 words) at 0x48000000 (is the bad part within
the first half of the 192 bytes?). Order: 9 first. Loop at 9: the region is
below 0x48010000 (then 8, then narrow); hang at 9: it extends past 64 KiB (then
10).

**ubmem9 result (Jimmy, 2026-10-26): HANG** (bands, then the plain Samsung logo,
no loop; forced off with Volume Down + Power). The write at 0x48010000 is bad, so
the bad region reaches past 64 KiB; it ends before 0x48100000 (ubmem6 loops).
Next: `ubmem10` (0x48080000).

**ubmem10 result (Jimmy, 2026-10-26): LOOPS.** The write at 0x48080000 is fine, so
the bad region ends between 0x48010000 and 0x48080000 (it starts at or just
above 0x48000000; 0x47fff000 is fine). Probes: `ubmem12` 0x48040000 (midpoint),
`ubmem13` 0x48020000, `ubmem14` 0x48060000. Order: 12 first.

**ubmem12 result (Jimmy, 2026-10-26): LOOPS.** The write at 0x48040000 is fine, so
the bad region ends between 0x48010000 and 0x48040000. Next: `ubmem13`
(0x48020000).

**ubmem13 result (Jimmy, 2026-10-26): LOOPS.** The write at 0x48020000 is fine, so
the bad region ends between 0x48010000 and 0x48020000. Probes: `ubmem15`
0x48018000 (midpoint), `ubmem16` 0x48014000, `ubmem17` 0x4801c000. Order: 15 first.

**ubmem15 result (Jimmy, 2026-10-26): LOOPS.** The write at 0x48018000 is fine, so
the bad region ends between 0x48010000 and 0x48018000 (0x48010000 itself is bad,
ubmem9). Next: `ubmem16` (0x48014000).

**ubmem16 result (Jimmy, 2026-10-26): HANG** (bands, then the plain logo; forced
off). The write at 0x48014000 is bad, so the bad region ends between 0x48014000
and 0x48018000. Known: bad at 0x48000000, 0x48010000, 0x48014000; fine at
0x47fff000, 0x48018000, 0x48020000, 0x48040000, 0x48080000, 0x48100000,
0x49000000, 0x4c000000, 0x50000000.

**Don't pin the last 16 KiB; use the result.** Linux's memory node covers this
region, so the kernel was writing there anyway (not only through the load
address). Change (`reloc0`): a no-map reserved-memory entry
`sboot-reset@48000000` (128 KiB, covers the bad region with margin) in the DT, and
the FIT's kernel load/entry moved from 0x48000000 to 0x48200000 (the arm64 Image
is relocatable at any 2 MiB-aligned address). `reloc0` = `STAGE=0` (kernel up +
panel, then a plain `reboot -f`): if the DRAM theory is complete, it LOOPS (the
first time a plain reset after Linux does). Hang = another bad region exists
(e.g. in the initrd/FDT placement or in what Linux writes elsewhere); then bisect
with the checkpoints again.

**reloc0 result (Jimmy, 2026-10-27): LOOPS. THE RESET HANG IS FIXED.** "STAGE 0
reached: plain reboot -f in 3s" on the console, then the phone rebooted by itself
and cycled logo, console banner, logo, repeatedly. Left with Volume Up + Home +
Power into TWRP.

**Root cause (EVIDENCE 27-29).** A write into DRAM at 0x48000000-about 0x48016000
(bad at 0x48000000, 0x48010000, 0x48014000; fine at 0x47fff000, 0x48018000 and
every other probe up to 0x50000000) makes the next plain SWRESET hang S-Boot at
the Samsung logo. The same reset after a named reset (recovery, download) worked,
and a plain reset from U-Boot without any write worked, which is why the hunt went
through the kernel, U-Boot's `bootm` and the drivers first. What lives there is
unknown (S-Boot state that it re-reads after a warm reset). Fix: a no-map
reserved-memory entry (128 KiB at 0x48000000) in the DT, and the kernel loaded at
0x48200000 instead of 0x48000000. Not yet checked: the exact end (between
0x48014000 and 0x48018000, covered by the 128 KiB margin), other bad regions that
a full boot did not touch.

Next: `m5f` = the normal Nura boot image with this fix; test `sudo reboot` in Nura.

**m5f result (Jimmy, 2026-10-27): PASSED.** `sudo reboot` from Nura restarted the
phone cleanly, no hang at the Samsung logo, and `ssh user@172.16.42.1` worked right
after (welcome banner). The reboot-hang fix is confirmed on the real boot image
(`out/a5y17lte-mainline-m5f-boot.img`, sha256 `8a560001…5f84`, commit 8381b93).
The root cause stays as written under reloc0 (DRAM at 0x48000000-~0x48016000).

## 30. Persistence of route, DNS and clock (2026-10-27)

What the rootfs already has: `swclock` (boot runlevel; restores the time of the
last clean shutdown), `chronyd` (default; `pool pool.ntp.org iburst`, `makestep 1
-1`, so it steps the clock whenever NTP is reachable), `routewrangler` and
NetworkManager (usb0 unmanaged). `/etc/resolv.conf` is the systemd-resolved stub
(`nameserver 127.0.0.53`) with nothing behind it under OpenRC.
So the clock needs only a route and DNS: chronyd sets it by itself once the
network works. Package `device-samsung-a5y17lte` 3-r3 (in git, rootfs not yet
rebuilt): `a5y17lte-usbnet` (+ OpenRC service, default runlevel, after
unudhcpd.usb0, before chronyd) adds `default via 172.16.42.2 dev usb0 metric 600`
whenever usb0 is 172.16.42.1 and nothing else provides a default route (polls
every 5 s, so a cable plugged in later works); post-install writes real
nameservers (1.1.1.1, 9.9.9.9) when resolv.conf is the stub; `build-pmos.sh`
repeats that on the image (post-install ordering trap, like hwclock).

**Applied live and tested (2026-10-27, Jimmy OK'd).** On the running Nura rootfs
(files only, via `sudo`): `/usr/bin/a5y17lte-usbnet`, `/etc/init.d/a5y17lte-usbnet`,
`rc-update add a5y17lte-usbnet default`, `/etc/resolv.conf` with 1.1.1.1/9.9.9.9
(the same files as package 3-r3; the rootfs image on the PC is still 3-r2 and
gets them at the next `build-pmos.sh`). The PC's NAT rules from EVIDENCE 26 had
been lost (PC side, runtime only) and were re-added (MASQUERADE 172.16.42.0/24 ->
wlp2s0, DOCKER-USER ACCEPT for enx02a517000002).
Result: the service added the default route by itself within 5 s; ping 1.1.1.1
and name resolution worked; `chronyd` stepped the clock by 604069 s (the stale
swclock date, 19 Oct) as soon as it could reach NTP. Then `sudo reboot` (clean
restart, thanks to m5f): after boot, with no manual step, `default via
172.16.42.2 dev usb0`, resolv.conf kept, clock right (swclock + chronyd), service
started, DNS and ping to dl-cdn.alpinelinux.org OK. PERSISTENCE PASSED.
Remaining caveats: the PC's NAT rules are not persistent (PC side); a phone
without the cable gets no time beyond swclock's last-shutdown time; the user "user"
has a password (placeholder 147147), `sudo` asks for it, root login by SSH is off.

**PC side persistent (2026-10-27, Jimmy OK'd).** `tools/install-pc-nat.sh` (sudo)
installs `/usr/local/sbin/a5y17lte-pc-nat` (from `tools/pc-usbnet-nat.sh`:
`ip_forward=1`, MASQUERADE for 172.16.42.0/24 to anywhere outside it, so any uplink
works, and ACCEPTs for enx02a517000002 in DOCKER-USER, or FORWARD if Docker is not
running; idempotent), `/etc/systemd/system/a5y17lte-nat.service` (oneshot, enabled,
`After=docker.service`, `PartOf=docker.service` so a Docker restart re-applies it),
and `/etc/sysctl.d/99-a5y17lte-forward.conf`. Checked: the manual rules deleted,
the service recreated them, a restart adds no duplicates, the phone pings
1.1.1.1 and resolves names. Not tested: an actual PC reboot (the service is
enabled; check after the next one with `systemctl status a5y17lte-nat`). Undo: see
the header of the install script.

## 31. Touchscreen: findings from the vendor tree (2026-10-27, no hardware yet)

Sources: `upstream/k7880` `exynos7880-a5y17lte_common.dtsi`, `..._eur_open_0N.dts`,
mainline v7.2 `drivers/input/touchscreen/zinitix.c`.

- **The touch IC depends on the board revision.** Rev 00 (`_00.dts`): Samsung
  `sec,sec_ts` at I2C 0x48 (firmware `mc44_a7x.bin`, supply LDO34 "vdd_ldo34");
  the Zinitix node is disabled there. Revs 01-03 (and the common dtsi):
  Zinitix ZT7538, `zinitix,bt532_ts_device` at 0x20, supply LDO34 (rev 01/02) or
  a fixed "tsp_avdd" regulator (rev 05/08). Which revision our phone is: unknown.
- **Mainline has the driver for Zinitix**: `zinitix.c` lists bt532 and bt538
  (and bt541). No mainline driver for `sec_ts`. So a Zinitix revision is a
  DT-only job; a `sec_ts` revision would need a new driver.
- **Wiring** (vendor DT): touch I2C = `i2c@13890000` (`i2c_2`, pins gpc2-0 SDA,
  gpc2-1 SCL, function 2; pin controller NFC), IRQ gpa0-5 (ALIVE, already in our
  DT), supply = S2MPU05 LDO34 (3.3 V), the PMIC at 7-bit 0x66 on
  `hsi2c@10610000` (HSI2C_CCORE_AP, gpm0-0 SDA, gpm0-1 SCL; pin controller CCORE),
  IRQ gpa0-2. Mainline v7.2 has S2MPU05 support (`sec-i2c.c`, `s2mps11.c`,
  `samsung,s2mpu05.yaml`), so the PMIC can be a normal node. The pin driver
  already has the banks (gpm0 in CCORE, gpc2 in NFC); our DT has only the ALIVE
  and TOP controllers, so CCORE and NFC need nodes. No CMU_PERI driver yet
  (I2C clocks): as with the MUIC, bit-banged `i2c-gpio` buses are the first step.
- **Plan**: (a) `i2c-gpio` buses on gpm0-0/1 (PMIC) and gpc2-0/1 (touch) in the
  DT with the CCORE and NFC pin controllers; in Nura use `i2c-tools`: read the
  PMIC ID, read/enable LDO34 (RAM only, a PMIC register write), then scan the
  touch bus for 0x20 or 0x48 to learn the revision. (b) If 0x20: Zinitix node
  (`zinitix,bt538`, size 1080x1920, IRQ gpa0-5, supply from the PMIC node) with
  the mainline driver, then `evtest`. If 0x48: decide between a new driver and
  something else. (c) Then Phosh.

**m6a probe (Jimmy booted it, 2026-10-27): the bit-banged buses work.** The kernel
registered `i2c-gpio-muic` (i2c-0), `i2c-gpio-pmic` (i2c-1), `i2c-gpio-touch`
(i2c-2); the new CCORE and NFC pin controllers probed without a hang (the
gate_pinctrl clock was left on by S-Boot).
- **PMIC found**: 0x66 on i2c-1; register 0x00 (ID) = 0x82. Register numbers are the
  `enum S2MPU05_reg` indices: L32CTRL 0x4e, L34CTRL 0x50, L35CTRL 0x51. At boot
  all three read 0x28 / 0x3c / 0x3c (the voltages 2.8 / 3.3 / 3.3 V are already
  set, the enable bits [7:6] are 00 = off). Writing 0xe8 / 0xfc / 0xfc turned
  them on, and the touch bus still showed nothing at 0x08-0x77; the values were
  written back afterwards (RAM state only).
- **The touch IC is powered by a GPIO, not the PMIC**: with gpd1-4 driven high
  (the "tsp_avdd" regulator-fixed of the rev 05/08 DTs, 3.3 V) a device
  ACKs at **0x20** on i2c-2 (gpc2-0/1). So this phone has the rev >= 05 wiring and
  the Zinitix address 0x20 (not Samsung's `sec_ts` at 0x48).
- **Odd readback**: register reads return the pointer then 1, 2, 3 (little-endian 16-bit words):
  `w2 0x80 0x00 r8` -> `80 00 01 00 02 00 03 00`, `w2 0x34 0x12 r4` -> `34 12 01 00`,
  and every register tried (0x00, 0x11, 0x12, 0x14, 0xc0, 0xc1, 0xcc, 0x121) gave
  its own address. Not a real Zinitix register file (chip revision, resolution
  would differ). Possibly the IC is in its bootloader/ROM state or needs a
  reset/enable sequence first; the mainline driver's init (SWRESET, then
  reads) is the real test. Tools used: `i2c-tools`, `libgpiod` (installed on the phone
  with apk). Next: `m6b` = Zinitix node (bt538, 1080x1920, IRQ gpa0-5), tsp_avdd
  regulator-fixed on gpd1-4, kernel config TOUCHSCREEN_ZINITIX.

**m6b result (Jimmy, 2026-10-27): TOUCHSCREEN WORKS.** `Zinitix-TS 2-0020` probed on
the first try (the only message: "supply vcca not found, using dummy regulator",
harmless); input device "Zinitix Capacitive TouchScreen" on /dev/input/event0
(1080x1920, 5 slots, BTN_TOUCH + multitouch), IRQ `bt538` on gpa0-5. The odd
register readback seen with `i2ctransfer` did not matter: the driver's own init
works. Jimmy tapped, dragged and used two fingers for 30 s; `evtest` captured
3751 lines: 630 ABS_MT_POSITION_X events, X 52-1030 and Y 30-1599 (the bottom
strip was not touched, the physical home key lives there), BTN_TOUCH down/up pairs,
tracking ids counting up, and MT slots 0 (72 events) and 1 (73): real two-finger
multitouch. The DT node, `tsp_avdd` (gpd1-4) and `CONFIG_TOUCHSCREEN_ZINITIX=y` are
in the tree and in `out/a5y17lte-mainline-m6b-boot.img` (commit 867fa24); the
pmOS kernel package gets them at the next `build-pmos.sh`.

**What Phosh still needs.** A display. Today the screen is S-Boot's framebuffer
(`simple-framebuffer`) on a command-mode panel that only updates after a DECON
trigger (EVIDENCE 11, 13), set once by `a5-diag panel`. Phosh/phoc need DRM/KMS
(at least simpledrm) and a way to trigger the panel after every frame. See
CLAUDE.md roadmap: the DECON/DSI display driver was planned after the CMU driver.

## 32. Phosh on Nura (2026-10-27)

**Correction to the plan: no "trigger helper" is needed.** The panel refreshes by
itself in HW/TE continuous mode once `a5-diag panel` writes DECON 0x14830070 =
0x10 at boot (EVIDENCE 13), and simpledrm + fbdev emulation leave that intact
(EVIDENCE 20). `/dev/dri/card0` is simpledrm on the S-Boot framebuffer.

**Install** (on the running Nura, apk over the PC's NAT): `phosh phoc squeekboard
elogind greetd mesa-dri-gallium mesa-egl mesa-gles dbus-openrc polkit-elogind`
(411 packages, SYSTEM 545 MB -> 1.3 GB used). NOT `postmarketos-ui-phosh`: it
depends on `postmarketos-usb-moded`, which would take over the USB gadget (our
SSH link). Problem met: `postmarketos-mkinitfs`'s apk trigger fails ("No kernel
found in /boot", we boot from the FIT in the BOOT partition) and aborts the
transaction; worked around by replacing `/usr/bin/mkinitfs` with a stub and
`apk fix --no-scripts` (apk state clean afterwards). OPEN: the trigger fails again
on any later apk transaction that touches /lib/firmware; needs a permanent fix in
the device package.

**Session**: `/usr/local/bin/a5-phosh-session` (phoc with `WLR_RENDERER=pixman`,
GTK `GSK_RENDERER=cairo`, Mesa swrast) and `/etc/greetd/config.toml`
(`initial_session` = autologin `user` on vt 1). `elogind` and `polkit` added to the
default runlevel; `greetd` started by hand (not yet enabled at boot). After 25 s:
`phoc`, `gnome-session`, `phosh`, `squeekboard` running, loginctl sessions for
`user` active.

**Touch under Phosh: diagnosis (2026-10-27).** With Phosh running the touchscreen
was dead from the start, though `evtest` without Phosh worked. Findings:
- The touch driver powers the IC off when nobody has the input node open
  (`tsp_avdd` disabled, 0 users): normal. Under Phosh the node is open.
- Dead state = the IC holds INT (gpa0-5) LOW with an unread event pending
  (status register 0x80 = e.g. `0x09 0x00 0x00 0x00 <x> <y>`) and the driver's
  falling-EDGE interrupt never fires again (IRQ count frozen, line low). Writing the
  clear command by hand (`i2ctransfer -f -y 2 w2@0x20 0x03 0x00`) released the line
  and zeroed the status at once. After a fresh driver start touches worked for
  a while (Jimmy swiped, typed the passcode, reached the launcher) and then
  stopped again; the dead state showed exactly that stuck status. So an event arrives
  while INT is already low (or the clear command races a new event), no new falling
  edge, driver stuck.
- Fix: level-triggered interrupt (`IRQ_TYPE_LEVEL_LOW`; the driver requests a
  threaded `IRQF_ONESHOT` handler, so it is re-run until the IC releases INT).
  Image `m6c` (DT only, commit follows).
Session fixes made on the way: `/etc/inittab` tty1 getty commented out (backup
`/etc/inittab.orig-a5`; greetd owns tty1), `agreety` installed, `greetd` added to the
default runlevel, greetd's `initial_session` only runs once per boot
(`/run/greetd.run`), Phosh passcode = the `user` password (147147).

## 33. Rootfs rebuilt with Phosh and persistence (2026-10-27)

`./build-pmos.sh` with device package 3-r4 (Phosh session, greetd, phoc scale 2,
elogind/polkit/greetd/usbnet services, `replaces=greetd-agreety-config` for
/etc/greetd/config.toml; extra_packages: doas, i2c-tools, libgpiod, evtest).
First attempt failed with "trying to overwrite etc/greetd/config.toml owned by
greetd-agreety-config" (fixed by `replaces`). The image checks in `build-pmos.sh`
had a bug (`debugfs stat` exits 0 for missing files); fixed, and it then
added the missing polkit runlevel link. Result:
`build/pmos/samsung-a5y17lte-root.img`, 1483735040 B, sha256 `fee7ccdb…4d52`,
e2fsck clean (pmOS_root, 18290 files). Contents verified with debugfs: runlevel
default has a5y17lte-usbnet greetd elogind dbus polkit; tty1 getty commented out in
inittab; resolv.conf 1.1.1.1/9.9.9.9; /usr/bin/a5-phosh-session, /etc/phosh/phoc.ini,
/etc/greetd/config.toml (autologin `user`), phoc, phosh, squeekboard, swrast
Mesa, agreety, evtest, i2c-tools. (The `mkinitfs` apk trigger issue on a
running phone is still open.)

**Rootfs written to SYSTEM (2026-10-29, Jimmy OK'd, TWRP):** `adb exec-in "dd of=by-name/SYSTEM
bs=1M conv=fsync" < samsung-a5y17lte-root.img` (7.5 min). Read-back (`adb exec-out dd
bs=1M count=1415`, hashed on the PC) differed from the image in exactly the last
~880 KB, twice, identically: that tail of the image is zeros (free space), and the
phone kept its old data there (adb's stream does not deliver the trailing zero
run). Fixed by zeroing the last MiB on the phone (`dd if=/dev/zero ... seek=1414
count=1`). Final check: the first 1415 MiB of SYSTEM read back, sha256
`fee7ccdb01ca…4d52` = the image, `cmp` 0 differences. Do not use TWRP's own
sha256sum on >1 GiB ranges as the only check (it disagreed with the PC).
Lesson for any later SYSTEM write: pull it back and compare on the PC.
Next: boot (m6c boot image + this rootfs), grow with `ext4-grow`, check Phosh.

## 34. The mkinitfs apk trigger (2026-10-29)

Cause: `postmarketos-mkinitfs` runs from apk triggers (/lib/firmware, /usr/share/kernel,
...) and `boot-deploy` aborts with "No kernel found in /boot" because pmbootstrap
`--split` leaves the rootfs /boot empty (our kernel is in the BOOT FIT). The abort fails
the whole apk transaction. Dropping the package is impossible (`apk del --simulate`:
required via `postmarketos-initramfs` by postmarketos-base, -ui-console, our device
package, sudo-rs, doas). Fix (tested live on the phone first): an empty
`/boot/vmlinuz`; then `mkinitfs` exits 0 (3.5 s, writes an unused 9 MB
`/boot/initramfs`) and apk transactions complete (`apk fix --reinstall
postmarketos-mkinitfs`, `apk add`/`del` all exit 0). `build-pmos.sh` now adds the
placeholder to new images with debugfs (dry-run on a copy of the current image:
fsck clean). The phone has the placeholder and an initramfs in /boot now; they
could be deleted but would be regenerated.

## 35. Wi-Fi: what the chip is and what mainline has (2026-10-29, investigation only, no code)

**Chip: Qualcomm QCA9377 (Rome-TF family) Wi-Fi/Bluetooth combo, Wi-Fi over SDIO.** Not
Broadcom. Evidence in the vendor tree (`upstream/k7880`):
- `exynos7880-a5y17lte_eur_open_05.dts` (and _00): node `wlan { compatible =
  "qcom,cnss_sdio"; wlan-en-gpio = <&gpd3 6>; wlan-host-wake = <&gpa2 2>;
  vdd-wlan-supply = <&cnss_dcdc_en>; }`, regulator-fixed `cnss_dcdc_en` on gpd3-3
  (startup 4 ms), and `bluetooth { compatible = "qca,qca6174"; BT_EN gpd4-2, BT_WAKE
  gpd5-1, BT_HOST_WAKE gpa1-6; same vdd-wlan supply }`.
- `hybris-a5y17lte_defconfig`: `CONFIG_WLAN_QCA9377_DUAL_BDF=3`, `CONFIG_QCA_CLD_WLAN=m`
  (qcacld-2.0).
- The original SYSTEM backup (`~/a5-backup/SYSTEM-p19.img`, read with debugfs) has
  `/etc/firmware/{qwlan30.bin, qwlan30_ibss.bin, bdwlan30.bin (8124 B),
  bdwlan30_OLD.bin, bdwlan32.bin, otp30.bin, Data.msc, nvm_tlv_tf_1.1.bin,
  rampatch_tlv_tf_1.1.tlv}` and `/vendor/etc/wifi/{indoorchannel.info, wpa_supplicant*.conf}`:
  Qualcomm Rome firmware and BT patch/NVM files.
- SDIO host: `dwmmc1@13550000` (`mshc1`), 4-bit, pins sd1_clk gpr2-0, sd1_cmd gpr2-1,
  sd1_bus1 gpr3-0, sd1_bus4 gpr3-1..3 (FSYS pin controller `pinctrl@13750000`, bank
  gpr*, already in the pin driver as pin-controller 4), sdr104 / use-fine-tuning in the
  vendor DT, `card-detect-type-external`.

**Mainline v7.2 support:**
- `drivers/net/wireless/ath/ath10k/sdio.c`: `ath10k_sdio` with `SDIO_DEVICE_ID_ATHEROS_QCA9377`
  (0x0701) and AR6005 (0x050A); the driver forces `dev_id = QCA9377_1_0_DEVICE_ID` and
  chip_id 0 ("don't know yet how to get chip_id with SDIO"), high-latency mode.
- Firmware in linux-firmware (the PC has it): `ath10k/QCA9377/hw1.0/firmware-sdio-5.bin`
  (390 KB zst), `board.bin` / `board-2.bin`. Samsung's own `bdwlan30.bin` could serve
  as board data if the generic one does not fit this module (unverified).
- Bluetooth (separate, later): `drivers/bluetooth/hci_qca.c` knows `qcom,qca6174-bt`; the
  vendor files are the Rome-TF 1.1 patch (`rampatch_tlv_tf_1.1.tlv`, `nvm_tlv_tf_1.1.bin`).

**Not yet in our DT:** the FSYS pin controller node and its sd1 pin states, `mmc1` (CMU_FSYS
clocks exist: mmc0 already uses the same block), the WLAN_EN gpio (gpd3-6), the DCDC_EN
regulator (gpd3-3), the host-wake interrupt (gpa2-2), maybe a 32 kHz sleep clock
(the vendor `cnss` stack is not mainline; whether the S2MPU05's 32 kHz output must be on is
unknown), `CONFIG_ATH10K`, `CONFIG_ATH10K_SDIO`, `CONFIG_MMC` SDIO support, `CFG80211`,
`MAC80211`, and the firmware in the rootfs (linux-firmware-ath10k).

**Risks:** ath10k SDIO support is little used and "experimental" (no chip_id, high-latency
mode, known low throughput); the QCA9377 hw1.1 (Rome-TF 1.1) revision named by the BT
files may not match the hw1.0 firmware; the board data may need Samsung's file. Safe first
step: DT + kernel config only, boot, read `dmesg`/`/sys/bus/sdio/devices` to see the SDIO
ID and ath10k's complaints; WLAN_EN/DCDC_EN are only GPIO writes.

**m8a: SDIO probe image (built 2026-10-29, not flashed yet).** `FWEMBED=1 MILESTONE=m8a
./build-mainline.sh`: DT adds the FSYS pin controller (`pinctrl@13750000`, SPI 250,
gpr0-gpr4, sd1 pin states as in the Exynos7870 boards using the same pins), `mmc1`
(`samsung,exynos7870-dw-mshc`, BIU gate = CMU_FSYS `CLK_GOUT_FSYS_MMC1_ACLK`, CIU = a fixed
399.75 MHz stand-in because CMU_MIF's FSYS_MMC1 is not supported; if S-Boot left that
clock gated the controller will time out), `cnss_dcdc_en` regulator-fixed (gpd3-3) as vmmc, a
`mmc-pwrseq-simple` that releases WLAN_EN (gpd3-6) 100 ms after power-on, 4-bit,
`cap-sdio-irq`, max 25 MHz. Kernel config: CFG80211, MAC80211, ATH10K, ATH10K_SDIO,
PWRSEQ_SIMPLE, built in (CONFIG_MODULES is off). The Qualcomm firmware
(`firmware-sdio-5.bin`, `board-2.bin`, `board.bin` from the PC's linux-firmware) is built
into the image with `CONFIG_EXTRA_FIRMWARE` only when `FWEMBED=1`, so the probe runs
before the rootfs exists; that image is therefore NOT committed. Reading it: `dmesg | grep
-i -E "mmc1|sdio|ath10k"`, `ls /sys/bus/sdio/devices`, `cat /sys/bus/sdio/devices/*/device`.

**m8a result (Jimmy, 2026-10-29): the QCA9377 is on the SDIO bus and ath10k binds to it.**
Booted fine, Phosh and touch unaffected. dmesg: `mmc1: new high speed SDIO card at address
0001` (25 MHz, 4-bit, CIU about 99.9 MHz after the div-3, so the fixed 399.75 MHz stand-in
works), `/sys/bus/sdio/devices/mmc1:0001:1`: vendor 0x0271, device 0x0701
(`SDIO_ID=0271:0701`, DRIVER=ath10k_sdio) = Atheros QCA9377, the ID ath10k_sdio supports.
Then `ath10k_sdio: unable to enable sdio function: -5`, `could not power on hif bus (-5)`,
`could not probe fw (-5)`. Cause: `cnss_dcdc_en` stayed disabled (gpd3-3 low, 0 users):
`dwmmc_exynos: Failed getting OCR mask: -22` / `could not set regulator OCR (-22)` /
`failed to enable vmmc regulator`, since the fixed regulator had no voltage range. The SDIO
interface answers without the DCDC, the chip core does not. WLAN_EN (gpd3-6) is high
(the pwrseq released it). Also: `cfg80211: failed to load regulatory.db` (rootfs file,
needed later). Fix in `m8b`: `regulator-min/max-microvolt = <3300000>` on `cnss_dcdc_en`.

**m8b result (Jimmy, 2026-10-29): WI-FI WORKS (scan).** Booted fine (Phosh, touch intact).
`cnss_dcdc_en` now enabled by the MMC core. dmesg: `ath10k_sdio mmc1:0001:1: qca9377 hw1.1 sdio
target 0x05020001 chip_id 0x00000000`, `firmware ver WLAN.TF.1.1.1-00061-QCATFSWPZ-1 api 5
features ignore-otp`, `failed to fetch board data for bus=sdio,vendor=0271,device=0701,...
from ath10k/QCA9377/hw1.0/board-2.bin` then `board_file api 1` (the generic board.bin), `htt-ver
3.32 wmi-op 4 htt-op 3 ... hwcrypto 1`. `wlan0` (permanent address 00:03:7f:50:00:01, a
placeholder from the firmware/board data) registered; NetworkManager manages it. `nmcli dev
wifi list` showed 5 APs on 2.4 GHz and 5 GHz: home AP (signal 89, ch 40, 5200 MHz), neighbour AP A
(75, ch 36 / 72, ch 3), neighbour AP B (47), neighbour AP C (22). Stable over 2+ minutes. The extra
card re-enumerations at 5.6 s and 15.8 s are ath10k powering the chip down after probing the
firmware and up again when wlan0 comes up. Not done: connecting to a network (needs
credentials), throughput/stability tests, the real MAC address, board data matched to the
module (generic board.bin in use; Samsung's bdwlan30.bin is in the SYSTEM backup), regulatory.db
(`cfg80211: failed to load regulatory.db`, a rootfs file), the host-wake interrupt and the
32 kHz clock (neither was needed to scan), Bluetooth (UART, hci_qca, `qca,qca6174`).
**Packaging question:** ath10k is built in and CONFIG_MODULES is off, so the firmware
must come from the kernel: `FWEMBED=1` embeds it (works, image not committable because of the
Qualcomm firmware); alternatives are enabling modules with the rootfs firmware package
(`linux-firmware-ath10k`) or an initramfs firmware path.

**Connected (Jimmy, 2026-10-29, through the Phosh settings on m8b):** `wlan0` joined
"home AP" (5200 MHz ch 40, link rate 1170 Mbit/s, signal 87), DHCP 192.168.2.29/24, 3 of 3 pings to
1.1.1.1 through wlan0 in about 10 ms. Two default routes at metric 600 (usb0 from
a5y17lte-usbnet and wlan0): usbnet now uses metric 700 so Wi-Fi is preferred and the USB/PC route
stays as a fallback (package 3-r5).

**Option 2 (modules), Jimmy's decision 2026-10-29:** `CONFIG_MODULES=y`; cfg80211, mac80211, ath10k,
ath10k_sdio (and their crypto helpers) are modules; `FW_LOADER_COMPRESS_ZSTD=y` for Alpine's
`.zst` firmware; the firmware comes from `linux-firmware-ath10k` and the regulatory database
from `wireless-regdb`, both now dependencies of the device package. `build-mainline.sh` also
builds and installs the modules into `build/modules/usr/lib/modules/7.2.0-a5y17lte` (about
2.4 MB) so the same tree can be tested on the phone, and the kernel package
(`linux-postmarketos-exynos7880` 7.2-r2) does `modules_install` (with depmod) instead of
shipping an empty modules.order. Image `m8c` (no embedded firmware, so it can go in git).
Plan: stage the modules, firmware and regdb in the live rootfs, flash m8c, check that udev
loads ath10k_sdio at boot, then rebuild the rootfs with all of it.

**m8c result (Jimmy, 2026-10-29): Wi-Fi works as MODULES.** Flashed m8c (Wi-Fi as modules, no
embedded firmware), booted, Phosh and touch fine. The first boot after the flash had no USB link:
the PC saw a full-speed device that failed enumeration (`device descriptor read/64, error -71`,
`unable to enumerate USB device`); a cable replug and a restart from Phosh fixed it (cause not
found; keep an eye on it). Then, over USB: copied `build/modules/usr/lib/modules/7.2.0-a5y17lte`
into the rootfs, `depmod -a`, `apk add linux-firmware-ath10k wireless-regdb iw` (the mkinitfs trigger
now passes, EVIDENCE 34), module vermagic `7.2.0-a5y17lte SMP preempt aarch64` = uname -r.
`modprobe ath10k_sdio` loaded ath10k_core, ath, mac80211, cfg80211 and friends; the firmware came
from /lib/firmware (zstd), regulatory.db loaded (no more "failed to load regulatory.db"),
wlan0 appeared and NetworkManager rejoined "home AP" on its own. After `reboot`, udev loaded
ath10k_sdio automatically, wlan0 connected by itself (192.168.2.29), 2/2 pings through wlan0
at about 11 ms, Phosh up. The board data is still the generic board.bin (board-2.bin has no
entry for this module). The live a5y17lte-usbnet is updated to metric 700.

## 36. Rootfs with Wi-Fi as modules written to SYSTEM (2026-10-29)

`./build-pmos.sh` (device package 3-r5 + kernel package 7.2-r2 with modules): image
`build/pmos/samsung-a5y17lte-root.img`, 1500512256 B (1431 MiB), sha256 `4a511b94…db38`,
e2fsck clean (18389 files). The build checks again added the polkit runlevel link and the
`/boot/vmlinuz` placeholder. Verified in the image with debugfs: ath10k_sdio.ko, cfg80211.ko,
modules.dep, `firmware-sdio-5.bin.zst`, regulatory.db, iw, a5-phosh-session, usbnet metric 700,
no build/source links in /usr/lib/modules/7.2.0-a5y17lte. Written from TWRP with `adb exec-in dd
bs=1M conv=fsync` (7 min), then the last 2 MiB zeroed on the phone (the image's tail is zeros, which
adb's stream does not deliver; see EVIDENCE 33), then pulled back (1431 MiB) and compared on the PC:
`cmp` 0 differences, sha256 `4a511b94…db38` = the image. Boot image on the phone: m8c.
Next: boot, grow with ext4-grow (1241088 blocks), check Wi-Fi/Phosh/touch, reboot test.

**Booted, grown, reboot-tested (2026-10-29):** the new rootfs came up with Wi-Fi by itself
(ath10k_sdio autoloaded by udev, wlan0 connected to "home AP", 192.168.2.30), the USB route
as fallback at metric 700, then `ext4-grow / 1241088` (4.6G, 3.2G free). After `reboot`: 4.6G
kept, ath10k loaded, wlan0 connected, 2/2 pings through wlan0 about 10 ms, phoc and squeekboard
running; only `sleep-inhibitor` shows failed (known, boot-time ordering). The board-data
message (`failed to fetch board data ... board-2.bin`, then generic board.bin) is expected.

## 37. sleep-inhibitor (2026-10-29, from the paused list)

It showed "failed" after every boot. Cause (error log): `System does not support any sleep
states, quitting.` The pmOS tool exits 1 when `/sys/power/state` does not exist, and our kernel
has `# CONFIG_SUSPEND is not set` (no suspend/hibernate at all), so it can never run; OpenRC's
supervisor respawned it 5 times in about 10 s and gave up. Not an ordering problem: `rc_need=elogind`
changed nothing, and `respawn_max=0` only produced an endless 5-second failure loop (both
reverted). Fix: take it out of the default runlevel (`rc-update del sleep-inhibitor default`) in
the device package post-install/post-upgrade (3-r6) and in `build-pmos.sh`'s image check; live
phone done and reboot-tested: 0 failed services. Bring it back together with suspend support
(`CONFIG_SUSPEND`, which needs the PMIC/wake sources worked out first).

## 38. Home, back and menu keys (2026-10-29)

**Home** (gpio-keys, gpa1-7): arrives in the kernel as `KEY_HOMEPAGE` (172), 8 presses ->
8 down/up pairs (evtest); volume keys fine; back and menu give no events at all.
Phosh does nothing with it: its binary handles `Super_L`/`Super_R` (overview) plus the power and
volume keys; gsd-media-keys only binds `XF86Explorer` (home folder) and `XF86WWW`. Jimmy's
choice: `KEY_LEFTMETA` in the DT (image `m9a`), to be checked on the phone.
**Back/menu**: an Abov A96T316 touch-key MCU (vendor `abov,mc96ft18xx`) at I2C 0x20 on a
bit-banged bus (vendor i2c@20: SDA gpd4-5, SCL gpd4-3), IRQ gpa1-4, firmware
`abov/a96t316_a5y17.fw` (only used for updates), `abov,boot-on-ldo`. Mainline has no driver; the
vendor driver (`abov_touchkey_ft1804.c`) only exists in the Sailfish tree, which the project rules
keep us out of, so Jimmy chose empirical probing (as for the touch IC): `m9a` adds an `i2c-gpio-tkey`
bus, no driver; supply switched on from userspace via the PMIC bus.

**Probing the touch-key MCU (m9a, 2026-10-29).** The bit-banged `i2c-gpio-tkey` bus is i2c-2
(with the touch bus i2c-3). The MCU does not answer at 0x20 until PMIC LDO32 (`vtouch_2.8v`,
register 0x4e, write 0xe8) is on; LDO35 alone (3.3 V) is not enough, LDO34 not needed. With it on,
`i2cdump` of 0x00-0xff gives a stable register map: `00=00 01=0d 02=96 03=96 04=11 05=22 06=33 ...`
(0x01 looks like a firmware version, as in other Abov chips); raw-looking data at 0x0e-0x11 drifts
by itself. Pressing the menu key gives large swings in 0x0a/0x0b (e.g. 0x0152, 0x01f5; 0x0c/0x0d stay
0) while register 0x07 flips through 0x02/0x00/0x01/0x00 and the IRQ line (gpa1-4, normally high)
dips low for short moments, so the first sensor channel is the MENU key and the second (0x0c/0x0d)
is presumably BACK. Register 0x00 (a button status byte in similar chips) never left 0 in any
capture. Mistakes to avoid: a plain 1-byte read (`i2ctransfer r1@0x20`) returns whatever the
register pointer points at, and it left the MCU stuck with IRQ low and the register reads dead
until its supply is cycled; capture windows must not be fixed-length (use a stop file).
Not decoded: how the MCU reports "key down/up" (status register or some command to
enable it), the key-LED control, the IRQ clearing rule. The LDOs were put back to their boot
values (0x28 / 0x3c / 0x3c) afterwards.

**Touch-key protocol decoded from the vendor driver (Jimmy OK'd reading that one file read-only
from a Timeshift copy of the Sailfish tree, 2026-10-29; nothing copied into the repo, nothing
modified).** On the interrupt (gpa1-4, low while an event is pending) read register 0x07
(`ABOV_BTNSTATUS_NEW`): bits 1:0 recent-apps key, bits 3:2 back key, 0b10 = press, 0b01 = release
(menu_press = !(data % 2)); vendor keycodes KEY_RECENT (a Samsung code, not in mainline) and
KEY_BACK. Register 0x00: write 0x10/0x20 = key backlight on/off; 0x08/0x09 = LED dimming of the
recent and back keys (1-0x1f); 0x0a diff data, 0x0e raw data, 0x01 firmware version; power-up
delay 45 ms, reset 150 ms. This matches the 1/2/4/8 values seen with i2cdump. Home key
check: `KEY_LEFTMETA` makes Phosh toggle its overview (Jimmy, 2026-10-29).

**m9b (built, not flashed):** two new kernel patches, `0008` (binding `abov,a96t316` and the
`abov` vendor prefix) and `0009` (driver `drivers/input/keyboard/abov-a96t316.c`: SMBUS
byte reads, level-low threaded IRQ, `vdd` regulator, 150 ms power-on delay, a firmware-version
read as a presence check, input device with `linux,keycodes` defaulting to KEY_APPSELECT
(mainline's "recent apps"; KEY_RECENT does not exist) and KEY_BACK, and an LED class device
`a96t316::kbd_backlight`); checkpatch clean except the "Abov" spelling hits and the MAINTAINERS note
(no Signed-off-by, per the AI rules). DT: the S2MPU05 PMIC (`samsung,s2mpu05-pmic`, 0x66 on the
bit-banged i2c-gpio-pmic bus, IRQ gpa0-2 level low) with only `ldo32` ("vtouch_2.8v",
2.8 V) described, the touch-key controller at 0x20 on i2c-gpio-tkey (IRQ gpa1-4 level low,
vdd-supply = ldo32), and `regulator_ignore_unused` on the command line so the regulator
core does not switch off the rails S-Boot left on. Config: MFD_SEC_I2C, REGULATOR_S2MPS11,
LEDS_CLASS, KEYBOARD_ABOV_A96T316 built in. Not run: dt_binding_check (dtschema is not installed
here). Kernel package 7.2-r3 lists the two new patches.

**m9b result (Jimmy, 2026-10-29): the PMIC and the touch-key driver work.** Booted first try,
Phosh/touch/Wi-Fi unaffected. dmesg: `Not disabling unused regulators`
(regulator_ignore_unused), `a96t316 2-0020: firmware version 0x0d`, input "A96T316 touch keys",
LED `a96t316::kbd_backlight`; sysfs: `vtouch_2.8v` enabled (1 user, PMIC ldo32), `tsp_avdd`, `cnss_dcdc_en`
enabled. evtest on the new device: menu key -> KEY_APPSELECT (580), back -> KEY_BACK (158), clean
down/up pairs 70-300 ms apart (9 presses each, no duplicates). Pending: whether the key
backlight lights (brightness 1 written), what Phosh does with APPSELECT/BACK (nothing is bound
by default), the LED default state/trigger.

**Key backlight (2026-10-29, live tests on m9b, Jimmy watching the buttons).** Writing the LED-on
command (reg 0x00 = 0x10) alone lights nothing. The backlight needs **PMIC LDO35**
(`vtouch_3.3v`, 3.3 V; the vendor `KEY_LED_3P3`) switched on (reg 0x51 = 0xfc), plus non-zero
dimming levels in registers 0x08/0x09 (they read 0 after boot; the vendor DT uses 0x05, range
1..0x1f); with LDO35 off the lights go out even though the LED command and dimming are still set.
Jimmy's wish: the three buttons (Home, recents, back) light only when pressed and stay lit about
1 s. `m9c`: driver patch 0009 reworked: `led-supply` (enabled only while lit), LED brightness
0..31 = dimming level written to both registers, 0 = LED-off command and supply off; binding
0008 gains `led-supply`; DT: `ldo35` ("vtouch_3.3v") in the PMIC node, `led-supply = <&ldo35>`,
back key = KEY_ESC (Jimmy's choice), recents stays KEY_APPSELECT. Userspace: `a5y17lte-keyled`
(python3, /usr/bin; OpenRC service `a5y17lte-keyled`, settings in /etc/conf.d/a5y17lte-keyled:
brightness 16, timeout 1.0 s) watches the touch-key device and the Home key on gpio-keys
(KEY_LEFTMETA) and lights the LED on a press with a 1 s re-armed timer. Device package 3-r7
(depends python3) and build-pmos.sh's runlevel check know the service.

**m9c result (Jimmy, 2026-10-29): the button backlight behaves as asked.** Booted fine; with
`a5y17lte-keyled` installed on the live rootfs (copied from the device package, enabled in
the default runlevel), pressing Home, recents or back lights the buttons and they go dark after about
1 s (brightness 16 of 31). At rest `vtouch_3.3v` (ldo35) is disabled with 0 users, so the key LEDs draw
nothing. After `reboot`: 0 failed services, the service starts by itself, wlan0 connected, Phosh up.
The rootfs image on the PC does not have the service/touch-key setup yet (device package 3-r7 and the
kernel package 7.2-r3 contain it): the next `build-pmos.sh` + SYSTEM write picks it up.
Not checked: what Esc (back) does in Phosh.

## 39. Rootfs rebuilt with the key service (2026-10-29)

`./build-pmos.sh` (device package 3-r7, kernel package 7.2-r3 with patches 0008/0009):
`build/pmos/samsung-a5y17lte-root.img`, 1500512256 B (1431 MiB), sha256 `fd3c63ce…5fe8`, e2fsck
clean (18392 files). Verified with debugfs: `/usr/bin/a5y17lte-keyled`, `/etc/init.d/a5y17lte-keyled`
and `/etc/conf.d/a5y17lte-keyled` (brightness 16, 1.0 s), python3, Phosh session files, ath10k modules,
firmware, regulatory.db, the `/boot/vmlinuz` placeholder; runlevel default has a5y17lte-keyled, usbnet,
greetd, elogind, polkit, dbus, NetworkManager, chronyd (no sleep-inhibitor, no tty1 getty). The last
2 MiB of the image are zeros (zero them on the phone after the adb write, EVIDENCE 33/36).
Not yet written to SYSTEM.

**Written to SYSTEM (2026-10-29/30, Jimmy OK'd, TWRP):** `adb exec-in dd bs=1M conv=fsync` (8 min; the
first try failed at once because adb had not yet enumerated TWRP, nothing was written), last 2 MiB
zeroed on the phone, pulled back 1431 MiB and compared on the PC: `cmp` 0 differences, sha256
`fd3c63ce…5fe8` = the image. Next: boot, grow with ext4-grow, re-enter the Wi-Fi password, check the
key service and a reboot.

**Booted, grown, reboot-tested (2026-10-30):** the rebuilt rootfs (fd3c63ce…5fe8) came up with the key
service started by itself (enabled in the image), 0 failed services, ath10k loaded, wlan0 connected
to the home AP, then `ext4-grow / 1241088` (4.6G, 3.2G free). After `reboot`: 4.6G kept, 0 failed
services, a5y17lte-keyled started, wlan0 connected (192.168.2.31) with the USB route as fallback at
metric 700, 2/2 pings through wlan0 about 10 ms, phoc running, key LED dark at rest.

## 40. Display survey (2026-10-30, read-only; nothing flashed or changed)

**Panel** (vendor `exynos7880-a5y17lte_common.dtsi`, `s6e3fa3`): Samsung S6E3FA3 AMOLED,
1080x1920, MIPI **command mode**, 4 lanes, 65x115 mm, 60 Hz, porches h 2/2/2 v 15/3/1, DSI HS clock
997 Mbit/s (PMS 3/230/1 from the 26 MHz crystal), escape clock 16 MHz; DSIM0 only (DSIM1 unused);
reset gpd3-4; power via regulators `LCD_3P0` and `LCD_1P8` (PMIC LDOs). Brightness of an AMOLED is a
DCS command to the panel: there is no backlight chip or PWM, which is why Phosh logs "No backlight".

**Blocks** (vendor `exynos7880.dtsi`; same addresses as mainline's Exynos7870 DT): DSIM
`0x14800000` (SPI 207), DECON_F `0x14830000` (SPI 201-206, 360), four VPP/IDMA blocks idma_g0/g1/g2/vg0 at
`0x14821000/22000/23000/24000`, sysmmu `0x14860000` (SPI 193), display pin controller `0x148c0000`
(SPI 68), CMU_DISPAUD `0x148d0000`, MIPI PHY control `0x148f100c`, power domain `pd_disp`.

**Mainline fit, by block:**
- **DSIM: reusable.** `samsung-dsim` has Exynos7870 data (`exynos7870_reg_ofs`); the live DSIM
  registers read from the phone fit that map exactly: MDRESOL (`0x20`) = `0x07800438`
  (1920x1080), CONFIG (`0x30`) = `0xa480707f`, PLLCTRL (`0x94`) = `0x00806731`, PHYTIMING (`0xb4`..)
  = `0x082b0d09 00090f0c`. A 7880 compatible and binding patch would be enough.
- **DECON: NOT supported by mainline.** The live DECON registers (`0x14830000`): `0x00` = `0x113`,
  `0x04` = 1, `0x30` = `0x20000000`, `0x40` = `0x11101101`, `0x70` = `0x10` (the HW trigger we set in
  `a5-diag panel`), `0xa0` = `0x07800438` (1920x1080), `0xa4` = `0x001fa400` (pixel count),
  `0xc0..0xd8` small per-window values, `0xf0` = `0xf31000ff`; everything from `0x600` up is zero.
  `exynos7_drm_decon.c` (also used for the Exynos7870) expects VIDCON0/VIDTCON at `0x00/0x610/0x704/
  0x710` and `exynos5433_drm_decon.c` has SHADOWCON at `0xa0` and TRIGCON at `0x2040`; neither
  matches. It is the vendor "exynos8-decon" family (ip_ver 2), so a **new DRM driver** is needed.
- **Panel: derivable.** `panel-samsung-s6e3fa7.c` is a close sibling (1080x1920 AMOLED, DCS
  brightness). The S6E3FA3 init and brightness sequences are in the vendor panel driver, which lives in the
  Sailfish tree.
- **Clocks / power:** CMU_DISPAUD exists for the 7870 in mainline (`exynos7870-cmu-dispaud`); whether
  the 7880 block is register-compatible (offsets from S5E7880-cmusfr.h) is still to be checked.
  S-Boot leaves the display powered and clocked (the panel works), so fixed-clock/ignore-unused
  stand-ins are possible as for the SD card and USB.

**Why backlight control is not a separate job:** it is a panel command over DSI. A DSI host driver
(samsung-dsim) can only run behind a DRM encoder/CRTC chain, and re-initialising the DSIM
would disrupt the working S-Boot link. So brightness arrives with the display driver, or via a
userspace register hack with an unknown command set.

**Open questions for the next step:** DECON register meanings beyond the above (needs the vendor
register driver or careful probing); how S-Boot's framebuffer reaches the DECON (which IDMA, IOMMU
on or off); the S6E3FA3 brightness/init commands; the PMIC LDO numbers behind LCD_3P0/LCD_1P8 (the PMIC
driver is live now).

**AOD (Jimmy, 2026-10-30): the phone has an always-on display for the clock.** Relevant to the panel
and DECON work: (1) the S6E3FA3 can show the clock by itself in a low-power mode, so the panel driver
will have more than on/off: a doze/LPM state with its own DCS commands and its own (low)
brightness levels, to be taken from the vendor panel driver; (2) the command-mode panel keeps its
image without the DECON refreshing it (PSR, the DECON hardware-trigger mode `a5-diag panel` sets), which
fits AOD; (3) the touch IC advertises it in the vendor DT (`zinitix,aod`, `zinitix,spay`: wake gestures
while the screen shows only the AOD clock), so our Zinitix setup may later want its wake/idle
modes, although the mainline driver has none; (4) mainline has no AOD support at all, so
this is a feature for after the basic display driver works. Not in the vendor DT otherwise (grep).

**Vendor sources read read-only (Jimmy OK'd, 2026-10-30; from the Timeshift copy of the Sailfish tree,
`kernel/samsung/universal7880/drivers/video/fbdev/exynos/decon_7880/`; nothing copied into the repo):**
`decon_reg_7880.c` + `regs-decon.h` (DECON register driver) and the panel files
`panels/s6e3fa3_a5y17_param.h`, `panels/s6e3fa3_a7y17_lcd_ctrl.c` (only the A7 control file exists; A5 has the
param and dynamic-AID headers).

**DECON_F register map (offsets from 0x14830000):** GLOBAL_CONTROL 0x00 (bit28 SRESET, bit8 1 = I80/command
mode, bit6 URGENT, bit5 IDLE, bit4 RUN, bit1 DECON_EN, bit0 DECON_EN_F), INTERRUPT_ENABLE 0x40 / PENDING 0x48
(bit28 DPU0, bit24 VSTATUS, bit12 FRAME_DONE, bit8 FIFO_LEVEL, bit0 INT_EN), SHADOW_REG_UPDATE_REQ 0x60
(bit31 global, bit28 DPU, bit(n) window n), HW_SW_TRIG_CONTROL 0x70 (bit12 AUTO_MASK_TRIG, bit8 SW_TRIG,
bit5 HW_TRIG_MASK, bit4 HW_TRIG_EN), DISPIF0_DISPIF1_CONTROL 0x80 (bit4 FREE_RUN, [13:12] underrun scheme,
[10:8] RGB order), DISPIF0 timing 0x90..0x9c, DISPIF0 size 0xa0 (height<<16|width) and 0xa4 (pixel count),
CLOCK_CONTROL_0 0xf0 (mask 0xF31000FF), VCLK divider 0xf4, blender BG size 0x110/0x114, per-window block
WIN_CONTROL(n) = 0x130 + 0x20*n (EN bit0, MAPCOLOR bit1, CHMAP [14:12], alphas), +0x4 START_POSITION,
+0x8 END_POSITION, +0xc COLORMAP, +0x10 START_TIME, +0x14 PIXEL_COUNT, DATA_PATH_CONTROL 0x230, splitter
0x240.., FRAME_FIFO 0x24c.., FRAME_ID 0x2a0, shadow copy at +0x7000. Window channel map: IDMA_G1 = 0,
IDMA_G2 = 1, IDMA_G0 = 2, IDMA_VG0 = 3. The vendor `decon_reg_init_probe()` is the bootloader hand-off path
(keeps what S-Boot started, only re-writes the registers); command mode uses HW trigger, "disable" =
mask bit set, "enable" = unmask.

**Live state decoded against that map (phone, S-Boot's setup):** command mode, running; HW trigger enabled and
unmasked (0x70 = 0x10); interrupts enabled (0x40 = 0x11101101); window 0 enabled, CHMAP 0 = IDMA_G1,
window 1920x1080 (end 0x077f0437), pixel count 0x1fa400; windows 1-3 idle; data path 1 (no compression,
splitter bypass); IDMA_G1 at 0x14822000: size 0x07800438 and **frame buffer address 0x67000000 at +0x74**;
IDMA_G0, G2, VG0 idle. Pipeline: 0x67000000 -> IDMA_G1 -> window 0 -> DISPIF0 -> DSIM0 -> panel.

**S6E3FA3 panel (vendor control driver):** power on = sleep out (0x11), 20 ms, test keys (F0, FC) on, then
common settings (OSC/FFC, PCD, ERR_FG, AVC, partial mode and area), brightness, test keys off, TE on, 80 ms;
display on (0x29), 10 ms, normal mode on (0x13); power off = display off, 10 ms, sleep in (0x10), 150 ms.
**Brightness is NOT a simple DCS 0x51:** it is Samsung "dynamic AID": per-level gamma (computed from the
panel's own MTP calibration read over DSI, 0xC8-style, plus HBM tables), AID/aperture, TSET (temperature),
ELVSS, ACL/OPR, all written between test keys and followed by a gamma update (F7). **AOD** = ALPM/HLPM
modes (alpm doze: ALPM/HLPM select 0.2 nit or 60 nit, source-load-enable, gamma update). DSI helpers
used: DCS short write (1/2 bytes), DCS long write, DCS read.

**Sizing for a mainline display driver (rough):** (1) a DECON "take-over" DRM driver: CRTC and one plane
(window 0 / IDMA_G1: update the base at IDMA +0x74, shadow update request, HW-TE trigger handling, vblank from the
frame-done interrupt), about 600-900 lines; (2) 7880 glue for samsung-dsim; (3) an S6E3FA3 panel driver with
the init sequence, power (LCD regulators + reset gpd3-4) and a dynamic-AID brightness implementation (or fixed
tables), about 500 lines plus tables; (4) DT, clock stand-ins, PMIC LDOs for LCD_3P0/LCD_1P8. A sensible order:
first the DECON driver alone (same behaviour as simpledrm, but with the right physical size 65x115 mm,
vblank, page flipping), later DSIM + panel for brightness, power and AOD.

**Backlog (Jimmy, 2026-10-30): double-tap-to-wake with the screen off.** Needs: the touch IC's low-power
"wake" mode (the vendor DT's `zinitix,aod`/`zinitix,spay` flags; the mainline Zinitix driver has none), a
way to keep the IC scanning while suspended or blanked, and a wake path (the IC interrupt on gpa0-5 is already a
wake-capable EINT bank) plus display power-on; depends on the panel/DECON work and on suspend.

## 41. Phosh polish: dark mode and the black home screen (2026-10-30, Jimmy's reports)

**Causes found on the phone.** (1) The session ran with `XDG_CURRENT_DESKTOP=GNOME`. Phosh's defaults
(`00_mobi.Phosh.gschema.override`: `color-scheme='prefer-dark'`, the wallpaper, on-screen keyboard,
idle delay) are overrides for the `Phosh` desktop, so none applied: `gsettings` said
`color-scheme 'default'` and the background was the generic `adwaita-l.jxl`. (2) The wallpaper files
(`/usr/share/backgrounds/gnome/*`) were never installed (and `.jxl` also needs the glycin loader, not
packaged here): the background was missing, hence black. (3) No XDG desktop portal was installed, so
apps could not read `org.freedesktop.appearance`. (4) No regular apps are installed: the app grid only
holds Settings panels and system helpers (no terminal, browser, editor, files, ...).
**Fix (live, then packaged in device pkg 3-r8):** `a5-phosh-session` exports
`XDG_CURRENT_DESKTOP=Phosh:GNOME`; `99_a5y17lte.gschema.override` points the background and lock screen at
`/usr/share/phosh/backgrounds/byzantium-abstract-720x1440.jpg` (phosh-wallpapers); installed
`xdg-desktop-portal`, `xdg-desktop-portal-phosh`, `phosh-wallpapers` (the package depends on them now).
After a session restart: color-scheme `prefer-dark`, background set, the portal answers
`org.freedesktop.appearance color-scheme = 1` (dark). The SSH-side `gsettings` needs
`XDG_CURRENT_DESKTOP=Phosh:GNOME` in its environment to see the overrides. Open: which apps to install.
Display driver (same day): `m10a` builds (exynos7880-decon linked in) but is not flashed; see below.

## 42. Haptics and lock-screen auto-unlock (2026-10-30, Jimmy's reports)

**Haptic feedback.** feedbackd (+ `feedbackd-device-themes`, default theme) is installed and running, but the
kernel has no force-feedback device (`capabilities/ff` = 0 on all three input devices). The vendor DT
(`eur_open_05.dts`): `sec_vib { sec_vib,ldo_type = <1>; sec_vib,ldo_en = <&gpd2 3 1>; max_timeout 10000 }`, a plain
on/off motor behind a GPIO (on revision 00 the same pin is the touch supply, `vtouch_3.3v`; our phone has
the rev>=05 wiring). Mainline has `gpio-vibra` (`compatible = "gpio-vibrator"`, `enable-gpios`, optional
`vcc-supply`; a force-feedback device through `INPUT_FF_MEMLESS`), which feedbackd can drive. Image `m9d`:
DT node `vibrator` (enable gpd2-3) and `CONFIG_INPUT_MISC=y` + `CONFIG_INPUT_GPIO_VIBRA=y` (the first build
silently lacked the driver: INPUT_MISC was off). The display node is now disabled by default
(`DECON=1 ./build-mainline.sh` enables the DECON hand-off driver, which stays untested). Limits: an
on/off motor has no strength control, so feedbackd's strength/duration profiles become plain pulses.
**Auto-unlock on the lock screen.** The Phosh 0.58 lock screen has an entry and an "Unlock" button
(`btn_submit`, enabled once something is typed); the only gsettings keys are `shuffle-keypad` and
`require-unlock`. There is no auto-submit option. Submitting on every digit would be wrong (each wrong
attempt counts as a failed PAM login), so the safe design is a Phosh patch that submits automatically once
the entry reaches a configured PIN length (a new gsetting, 0 = off), 6 for the current passcode. That
needs a patched `phosh` package (the aport builds with meson; the C change is in `src/lockscreen.c`,
`input_changed_cb`).

## 43. Apps (2026-10-30, Jimmy's list)

Installed on the running phone (101 packages, +190 MB, 1.4G of 4.6G used): gnome-console (terminal),
gnome-text-editor, gnome-calculator, gnome-clocks, gnome-contacts, gnome-calendar, loupe (images), papers
(documents), nautilus (files), gnome-maps and epiphany (GNOME Web; chosen over firefox-esr because it is the
Phosh-native adaptive browser and much lighter under software rendering). They are in `build-pmos.sh`'s
`extra_packages` for fresh rootfs builds. The mkinitfs `boot-deploy` step now also complains that
`exynos/exynos7880-a5y17lte.dtb` is missing from `/boot/dtbs` (not fatal, apk still finishes OK); an
empty placeholder fixes it, added on the phone and in `build-pmos.sh`.

## 44. Is USB-C DisplayPort Alt Mode wired? (2026-10-30, Jimmy's check-first request)

**No.** From the vendor DT (`exynos7880.dtsi`, `..._common.dtsi`, `..._eur_open_05.dts`): (1) no DisplayPort, HDMI or MHL
controller or PHY node anywhere (the only trace is a generic comment on the DECON `out_type` property, "0: DSI, 1: eDP,
2: HDMI", of which only DSI is used); (2) no USB-C PD/CC controller (no ccic/pdic/typec/S2MM001 nodes): the
port is handled by the S2MU004 MUIC (already used for the USB D+/D- switch, EVIDENCE 18) which has no alt-mode
mux; (3) the USB controller is limited to `maximum-speed = "high-speed"` (USB 2.0; the SuperSpeed PHY of the
generic DWC3 binding is not used), so there are no SuperSpeed lanes to repurpose for DisplayPort; (4) the DECON has
one DSI output (dsim0) and no external-display path in the live setup (`dsi_mode = <1>`, windows 1-3 idle). The
Galaxy A5 (2017) is a USB 2.0 Type-C phone without video out or DeX. What is possible: USB host/OTG for a
keyboard, mouse or storage (`dr_mode = "otg"`, VBUS from LDO3), and wireless display over the QCA9377.

**Haptics bring-up result (m9d, Jimmy, 2026-10-30): the motor does not buzz.** `gpio-vibrator` probes and creates
`input1 "gpio-vibrator"` with FF_RUMBLE (EV_FF set); a rumble effect written to it (python EVIOCSFF, 0.5-1.5 s)
plays without errors, gpd2-3 goes high for exactly the effect length (debugfs `enable out hi`, hardware registers
CON pin3 = 1 output, DAT bit3 = 1, 0 afterwards), but Jimmy feels nothing. Tests that did NOT make it buzz: the same
with PMIC LDO35 ("VDD_MOTOR_3P3" in the revision-00 DT, `vtouch_3.3v` elsewhere) on; LDO34 alone; LDO34 + LDO35
together. PMIC LDO state map read (enable bits [7:6]): fully off are LDO2, 7, 10, 14, 15, 28, 34, 35; most others
on. All revisions >= 01 of the vendor DT use the same node (`sec_vib`, `ldo_type = 1`, `ldo_en = <&gpd2 3 1>`);
revision 00 uses `ldo_type = 0` with regulator `VDD_MOTOR_3P3` (= LDO35). In TWRP (Samsung's vendor kernel) the
vendor driver's own interface, `echo 1500 > /sys/class/timed_output/vibrator/enable`, counts down and logs
`[VIB] sec_vib_enable/sec_vib_work` but the motor does not buzz there either; the vendor kernel's gpio
list shows no claim on gpd2-3 (only gpd2-0 `mcu_boot0`), and `vtouch_2.8v` and `vtouch_3.3v` are enabled by
other users. So either the TWRP kernel's DT does not drive the motor on this unit, the vendor driver switches
something other than gpd2-3, or the motor/its circuit is dead. Not settled; needs the vendor `sec_vib` driver
source (one more vendor file) or a check that the phone vibrates in another OS (Android, Sailfish).

## 46. Lock screen auto-unlock: patched Phosh (2026-10-30, Jimmy's request: default length 6)

The phone's Phosh is Alpine's `phosh-0.58.0-r0` from edge/community (not pmOS's systemd fork). Source
`phosh-0.58.0.tar.xz` from sources.phosh.mobi, SHA-512 `df60a8e1…e95d` = the checksum in Alpine's APKBUILD
(checked). Change: `src/lockscreen.c` `input_changed_cb` reads a new setting and calls the same code as the
Unlock button (`submit_cb`) when the entry length equals it; new key `sm.puri.phosh.lockscreen autosubmit-length`
(uint, 0 = off, range 0..64) in `data/mobi.phosh.shell.gschema.xml`. Patch `pmos/phosh/0001-lockscreen-add-autosubmit-length.patch`;
package `pmos/phosh/APKBUILD` = Alpine's recipe with pkgrel 1 (so apk prefers it), the patch added, `options="!check"`
(the tests need xvfb), maintainer Jimmy. It is built with `pmbootstrap build phosh` from `upstream/pmaports/temp/phosh`
(copied from pmos/phosh). Default stays 0 in the schema; the device package's gschema override sets
`autosubmit-length=6` (the passcode is 6 digits). Each automatic try counts as one failed login if the entered passcode is
wrong, so the value must be the passcode length (a longer passcode would be tried at its first 6 characters).

**Result (Jimmy, 2026-10-30): the lock screen unlocks by itself after the sixth digit.** Build story: the recipe
needs Alpine's companion file `phosh.trigger`; plain `pmbootstrap build` builds for the host (x86_64), so
`--arch aarch64`; emulated native builds fail (`cc: cannot execute 'cc1'`), the cross mode `pmb:cross-native`
did not give meson a cross file, `pmb:cross-native2` did; then the GObject-introspection step for `libphosh`
had to run target binaries, so the bindings library is not built (`-Dbindings-lib=false -Dintrospection=false
-Dvapi=false`, subpackages libphosh/libphosh-dev removed; neither was installed on the phone). The patch applied
cleanly. Built `phosh-0.58.0-r1` (+ schemas, portalsconf, lang, dev, doc, dbg, systemd); the binary and the compiled
schema both contain `autosubmit-length`. Installed on the phone with `apk add --allow-untrusted`
(phosh, phosh-schemas, phosh-portalsconf, phosh-lang), the device package's schema override
(`autosubmit-length=6`) added and compiled, session restarted: `gsettings` reads `uint32 6`. `build-pmos.sh`
now copies pmos/phosh to pmaports/temp and builds it (`pmb build --arch aarch64 phosh`) before the image.
Not yet tried: a wrong six-digit code (expected: refused once, entry cleared).

**Display driver saved as kernel patches (2026-10-30).** `0010` (binding `samsung,exynos7880-decon`) and `0011`
(`drivers/gpu/drm/tiny/exynos7880-decon.c` + Kconfig `DRM_EXYNOS7880_DECON` + Makefile) in `mainline/patches`,
made from commits on the `a5y17lte` branch of `upstream/linux`, `checkpatch --strict` clean (apart from the MAINTAINERS
note and spelling false positives), `Assisted-by: Claude:claude-sonnet-5-5`, no Signed-off-by. Kernel package 7.2-r4 lists
them. The driver is still untested on the phone (image `m10a`, `DECON=1 ./build-mainline.sh`; the node is disabled by
default). The DT node stays in `mainline/dts` (`&decon`, status disabled).

## 47. Display driver test, first step (2026-10-30)

Wrong lock-screen code stays locked, the correct one unlocks by itself (Jimmy, confirmed). Display test image
`m10b` = `DECON=1 MILESTONE=m10b ./build-mainline.sh` (DT node `&decon` enabled; driver built in; all the other
current features). To read after the boot: `dmesg` ("adopted the bootloader pipeline"), `/dev/dri/card0` driver name
`exynos7880-decon`, connector name (`DSI-1`, physical size 65x115 mm), Phosh/phoc starting, touch, no hang. `phoc.ini`
now sets scale 2 for both connector names (`Unknown-1` = simpledrm, `DSI-1` = the new driver); device package 3-r10.
Recovery if the display stays dark: Volume Down + Power, TWRP (Volume Up + Home + Power), flash `m9d`.
Note: the PC lost the USB link to the phone during this work (phone's usb0 NO-CARRIER, no adb, no `enx` on the PC)
while Wi-Fi kept working (192.168.2.31); TWRP and flashing need the USB cable, so check it first.

**m10b result (Jimmy, 2026-10-30): the phone boots normally, but the new driver did not take over.** Console text,
Phosh lock screen, touch and UI size all as before, on `simpledrm` (`card0`, DRIVER=simple-framebuffer, connector
`Unknown-1`, 1080x1920). dmesg: `exynos7880-decon 14830000.display-controller: error -16: failed to acquire the
framebuffer`. So the driver passed every hardware check (DECON running in command mode, 1080x1920, window 0 fed by IDMA_G1,
framebuffer address read) and failed at `devm_aperture_acquire_for_platform_device()`: that call claims free
memory but does not evict the owner (simpledrm already held 0x67000000), hence -EBUSY. The safe fallback worked: the
screen never went away. Fix (patch 0011 amended): call `aperture_remove_conflicting_devices()` instead,
as late as possible, right before `drm_dev_register()`, so an earlier failure still leaves simpledrm running. Image
`m10c`. Note: this also gave the first test of the "probe on a running pipeline" checks on the real hardware.

**m10c result (Jimmy, 2026-10-30): the driver took over but the display froze.** The screen stays on the
last simpledrm console line ("simpledrm … frame buffer device"); the phone itself runs fine (SSH over Wi-Fi). dmesg:
`[drm] Initialized exynos7880-decon 1.0.0 … on minor 1`, `*ERROR* fbdev: Failed to setup emulation (ret=-12)`,
`adopted the bootloader pipeline: 1080x1920, framebuffer at 0x67000000`; sysfs: `card1` (driver exynos7880-decon),
`card1-DSI-1 connected 1080x1920`, `enabled=disabled`; 16.8 s after boot phoc triggers a WARNING in
`__drm_atomic_helper_set_config` (via drm_client_modeset_commit, drm_release). Cause: ENOMEM, an 8.3 MB framebuffer
must be one physically contiguous DMA block, and without CMA the largest block is 4 MB (MAX_PAGE_ORDER 10), so neither
fbdev emulation nor phoc's dumb buffers could be allocated, and nothing new was ever scanned out (the panel keeps
showing the last simpledrm frame, DECON/IDMA untouched). Fix: `CONFIG_CMA=y`, `CONFIG_DMA_CMA=y`,
`CONFIG_CMA_SIZE_MBYTES=64` (default area). Image `m10d`. Hardware facts confirmed: the pipeline adoption
(checks, aperture eviction, DRM registration, connector with the DT mode) works on the real phone.

**m10d result (Jimmy, 2026-10-30; the phone itself was fine, SSH over USB worked): display still frozen, Wi-Fi gone;
recovered by flashing `m9d` again.** With CMA (64 MiB at 0xfb800000, CmaFree 56860 kB) the driver now sets up
fbdev emulation (`fb0: exynos7880-deco frame buffer device`), `card1`/`card1-DSI-1` connected 1080x1920 (enabled=disabled),
phoc and phosh run, but the panel keeps showing the last simpledrm frame: nothing the new driver draws
reaches the screen. Open question for the next session: the plane update only writes the IDMA_G1 base
address (+0x74), as in the manual test; what is different when the driver does it (the buffer address is in CMA, 0xfb...: does the
IDMA reach that range, is the SMMU/AXI window limited to a range around 0x4... to 0x6..., is the buffer written at
all, does the commit even run `update` as the crtc is `enabled=disabled`)? Check with `modetest`/a manual
EVIOC-style test: write a CMA buffer address to IDMA +0x74 by hand. Also found: **CONFIG_CMA changes
`struct device`, so every module built without it misbehaves** (ath10k_sdio "unable to enable sdio function: -62";
no version-check error: CONFIG_MODVERSIONS is off). Rule: the boot image's config and the rootfs modules must come from the same
config; after a config change that touches core structures, install fresh modules (`build/modules`, built by
build-mainline.sh) or rebuild the rootfs. Consider enabling CONFIG_MODVERSIONS to make such mismatches fail loudly.

## 48. Display driver: the page flip works (2026-10-30, Jimmy watching the screen)

**Result: m10f takes over the panel from simpledrm; the Phosh lock screen shows and reacts to touch (Jimmy: "works but
a bit laggy").** Image `out/a5y17lte-mainline-m10f-boot.img`, sha256 `365518e8…89087f`, 15362064 B; patch 0011
amended. Two separate causes had to be found; the m10c/m10d "frozen panel" came from both.

1. **Reach (m10e, Jimmy's flip tests):** pointing IDMA_G1 at low RAM (0x50000000) changed the picture, the upper bank
   (0x80000000, 0xc0000000, 0xfb800000) did not show. The default CMA had landed at 0xfb800000. Fix: a DT
   `reserved-memory` node `linux,cma` (shared-dma-pool, reusable, linux,cma-default) at 0x5c000000, 64 MiB (free low
   RAM is 0x52a00000-0x66ffffff). m10e: CMA at 0x5c000000, driver buffers at 0x5c100000/0x5c900000/0x5d100000, but
   the panel was still frozen.
2. **Latching (this was the real bug):** writing IDMA_G1 +0x74 alone never changed the picture on any boot
   (m9d, m10e, driver unbound, with a DECON SHADOW_REG_UPDATE_REQ at +0x60 or a HW-trigger mask/unmask pulse).
   The vendor VPP code (decon_7880/vpp, read-only, Jimmy allowed it: `vpp_reg_set_in_buf_addr`) writes the base
   address (VG_BASE_ADDR_Y(0) = +0x74) and then **VG_PINGPONG_UPDATE (+0x1c) bit 0 = 1**; the hardware clears it
   when the address is latched (cleared within 2-8 ms here). With that write a manual flip to 0x673F4800 gave
   half green/half black, to 0x67800000 a solid green picture, 0x67700000 mostly green (green buffer filled by
   hand at 0x67800000; Jimmy's description). Painting the buffer that is already scanned (0x67000000, red) always
   worked, which is why the display looked "half alive" and the earlier flips were misread.
   Driver: `pipe_update` waits (up to 20 ms, `readl_poll_timeout_atomic`) for the bit to clear, writes the address,
   writes BIT(0) to +0x1c. At boot dmesg shows one `previous buffer address was not latched` from the first two
   back-to-back fbdev commits (the bit takes about a frame to clear); harmless so far, to look at.
- Checked and not the cause: DECON/IDMA/DSIM register snapshots of m9d and m10e differ only in counters and the IDMA
  address; FRAME_ID (+0x2a0) counts 60 per second; the DRM atomic commits were all accepted; simple-framebuffer has
  no clocks or power domains to release at eviction.
- Pitfalls: (a) `/dev/mem` maps RAM that Linux manages (the CMA buffers) cacheable, so writes there may sit in the
  CPU cache and not reach what the DMA reads: for scanout tests use reserved no-map memory (0x67000000+) or the driver's own
  buffers through DRM. (b) Stopping phoc makes the kernel switch the scanout back to the fbcon buffer (IDMA changed
  from 0x5c900000 to 0x5c100000), so a "paint the current address" test must re-read the register first.
  (c) `/dev/fb0` does not exist (no CONFIG_FB_DEVICE), the fbdev emulation has no device node.
- Module/boot-image pairing again: m10 images have CONFIG_CMA, so SYSTEM now has the CMA module set in
  `/usr/lib/modules/7.2.0-a5y17lte` (copies kept as `.cma`, `.m9d`, `.bak-m9d`; swap them with `cp -a` + `depmod` when
  going back to an image without CMA, e.g. m9d). A rootfs rebuild will pick up the CMA modules by itself.
- Open: the lag (software rendering by pixman in phoc and the vblank emulated by an hrtimer; the real DECON frame
  interrupt would give true vblank timing), the first-commit latch warning, then the DSIM + panel driver
  (brightness, power, AOD).

## 49. Display driver: lag fixed by shadow buffers; top-edge artifacts (2026-10-30, Jimmy watching)

- **Lag measured, not guessed.** m10f (DRM buffers scanned out directly, `dma_alloc_wc` memory): during touch interaction
  phoc used about 95 % of a core and produced one frame every 0.79 s (buffer flips at 0.81, 1.60, 2.38, 3.16 s), 271
  input events in 60 s. Cause: userspace maps the DMA GEM buffers write-combined and pixman *reads* the destination pixels
  when it blends; reads from write-combined memory are very slow. (Recording tool: a python script on the phone that
  samples IDMA +0x74 and the evdev devices with timestamps.)
- **Fix (m10h, patch 0011 rewritten):** shmem GEM objects with shadow planes (cached memory, like simpledrm) and
  `drm_fb_memcpy` of the damaged rectangles into the bootloader's framebuffer (0x67000000, `devm_ioremap_wc`); no address
  flips any more, so neither CMA nor the PINGPONG_UPDATE write is needed (both stay in the tree and in the config: dropping
  CONFIG_CMA would change `struct device` again and need new modules). phoc then used 25-73 % of a core while Jimmy swiped.
  `pipe.disable` blanks the framebuffer: the m10f bottom-of-screen lines came from the idle blank (Phosh turns the CRTC off
  after a while, power button wakes it, touch does not) leaving the DMA on a freed buffer (m10g fixed that by pointing the
  DMA at memory the driver owns; m10h has the same property by construction).
- Images: m10g (flip-based, blank buffer) `b896bc83…268b80`, m10h `3ce9d08a…ac3c68`; both flashed and read back on the phone.
- **Open: artifacts along the top edge** (Jimmy: small lines, present on the lock screen, the passcode screen and on a flat
  white/grey test picture, "persistent during normal use"; at the first boot of m10g/m10h they were there during the spinner).
  The scanout buffer itself is clean (dumped through /dev/mem: the top 160 rows show a normal status bar), and a flat grey
  picture written into the whole buffer still showed them, so the data and the CPU copy are not the cause: it is somewhere
  after the DMA (DECON, DSIM, panel). The same artifacts may be there under simpledrm (m9d); not checked yet. A flat grey
  picture (0x808080) looked fully white on the panel (no brightness/gamma setup yet).

## 50. Bisect of the top-edge artifacts; phone left on m9e (2026-10-30, end of the evening)

- **m9e** = the current config and DT (CONFIG_CMA, the low `linux,cma` pool) with the display driver OFF (`DECON` unset,
  simpledrm drives the panel), sha256 `873ca78e…6844b1`. Jimmy: "no artifacts", and "the screen really responsive and
  fast". With m10h (same config and DT, driver on) the top-edge artifacts are there, so **the artifacts come from my
  driver, not from CMA, the DT or the kernel config.** `DECON=1` only sets `&decon { status = "okay"; }`.
- Register snapshots (DECON 0..0x3ff, IDMA, DSIM) of m9d and m10h differ only in status bits that change from sample to
  sample in command mode (IDMA +0x000 OP_STATUS, DECON +0x08c, FIFO levels at +0x260..0x26c): no configuration difference.
  The driver only reads registers at probe, so the difference is in the pixels it writes.
- Best hypothesis, not tested yet: **simpledrm converts XRGB8888 to the framebuffer's native ARGB8888 with alpha 0xff
  (`drm_fb_blit`), my driver copies the pixels raw (`drm_fb_memcpy`)**, so the unused X byte reaches the DECON as pixel
  alpha. Next step: use `drm_fb_blit(&dst, NULL, DRM_FORMAT_ARGB8888, shadow_state->data, state->fb, &rect,
  &shadow_state->fmtcnv_state)` in `pipe_update` (and make the plane formats XRGB8888 only), build m10i, compare. Caveat:
  a flat grey picture I wrote by hand (alpha 0xff, whole buffer) still showed the artifacts on m10h, which argues
  against alpha; if the blit does not help, look at what else differs from simpledrm (mapping type of the scanout
  memory: simpledrm uses `devm_ioremap_wc` too; the damage copy pattern; fbcon).
- **State left on the phone:** BOOT = m9e (read back and verified), SYSTEM = the Nura rootfs with the CMA module set active
  (it matches m9e and m10*); leftover module copies `/usr/lib/modules/7.2.0-a5y17lte.{cma,m9d,bak-m9d}` (about 2.4 MB each,
  harmless). 0 failed services, Wi-Fi connected, no oops. No eMMC write is half done.

## 51. Top-edge artifacts fixed: the damage copy ignored the rectangle position (2026-10-31, Jimmy watching)

**Result: m10i (`out/a5y17lte-mainline-m10i-boot.img`, sha256 `056c1a57…fe9ee28`) has no artifacts and is as fast as m10h
(Jimmy: "the artifacts are gone").** Kernel side on the phone: driver bound without warnings, 0 failed services, Wi-Fi up,
CRTC enabled with a framebuffer.
- **Root cause** (found by reading how simpledrm/sysfb does the same copy, not by the alpha hypothesis of EVIDENCE 50): my
  `pipe_update` called `drm_fb_memcpy(&dst, NULL, src, fb, &rect)` with `dst` at the START of the scanout buffer for every
  damage rectangle. The helper copies the clip region of the source to `dst` as given and does not offset `dst`, so every
  partial update (spinner, clock, keypad digits, the lock screen's small changes) was drawn at the top-left corner of
  the panel instead of in place: lines along the top edge that grew with every press on the keypad. Full-screen updates
  (the first frame) were fine, which is why a clean screen could come back and why my flat grey test, written behind the
  driver's back, did not explain it.
- **Fix:** `iosys_map_incr(&dst, drm_fb_clip_offset(dst_pitch, dst_format, &rect))` per rectangle with an explicit
  `dst_pitch` (width * 4), as sysfb does, plus `drm_fb_xrgb8888_to_argb8888()` for XRGB8888 framebuffers (alpha 0xff, like
  simpledrm; the bootloader's framebuffer is ARGB8888) and `drm_fb_memcpy()` for ARGB8888 ones. v7.2 has no `drm_fb_blit`.
- Lesson: `drm_fb_memcpy`/`drm_fb_xrgb8888_to_*` take a destination that already points at the clip's position.

## 52. DSIM command path on the live S-Boot link: the host sends, the panel answers with errors (2026-10-31)

Goal: send DCS commands (brightness, display on/off) through S-Boot's running DSIM (0x14800000) without re-initialising
it. Jimmy allowed reading the vendor DSIM files read-only (`dsim_reg_7880.c`, `regs-dsim.h`, `dsim_drv.c`, nothing copied).
Tools: python on the phone writing the DSIM registers through /dev/mem (header at +0x3c, payload at +0x40, RX FIFO at +0x44).
- **Live state:** command mode (CONFIG 0xa480707f: 4 lanes, burst, non-continuous clock, no video bit), `CMD_ALLOW` already 0xf
  (MVPORCH 0xf002000f), CMD_CONFIG (+0x78) = 0, ESCMODE 0x01400000 (commands in HS), FIFOCTRL 0x1547. The D-PHY status
  alternates between "data lanes in stop state" (0x10f/0x40f) and "transmitting HS" (0x7fc00): the link is busy with the
  60 Hz frame transfers a good part of the time.
- **Vendor register facts (differ from mainline's `samsung-dsim` names):** FIFOCTRL bit 12 = RX empty, bit 11/10 = SFR header
  full/empty, bit 9 = payload full, init bits 2 (RX) and 1 (SFR) (clear then set); INTSRC bit 28 = header FIFO empty, bit 18 = RX
  done, bit 25 = BTA done; packet header = id | data0 << 8 | data1 << 16 (ECC by hardware), same as mainline; the vendor
  sends the header then waits for the header-FIFO-empty interrupt; a read = init RX FIFO, clear RX_DONE, set-max-return-size
  (0x37), DCS read (0x06), wait RX_DONE, pop words until RX empty; the vendor panel code reads the ID with a plain DCS read
  (retry twice). `CMD_CONFIG` has PKT_GO bits (21/22) but nothing special is needed for plain commands (vendor option).
- **What happens:** the packets are accepted and the header FIFO empties (INTSRC bit 28), but every read request is answered
  with `0x13800402` = DSI "acknowledge and error report", error bits 0x8004 (bit 2 EoT sync error + bit 15 DSI protocol
  violation), nothing else in the RX FIFO. The same in all four combinations of HS/LP command mode and HW trigger running/masked,
  with a properly drained RX FIFO. DCS display off/on (0x28/0x29, short writes, HS and LP) never blanked the screen, so the
  panel does not execute what the host sends. The one exception: the very first read (`0x04`, after a BTA force) returned a
  real long-read response (header 0x1c, 3 bytes: 46 00 12); it never repeated, cause unknown.
- **Reading:** the S-Boot link state is good enough for frames (HS, TE trigger) but commands from Linux are not understood by the panel
  there (EoT/framing as the panel sees it). A clean re-initialisation of the DSIM + PHY + panel (mainline `samsung-dsim` + a
  panel driver with the vendor init sequence) would give a known state; it is the larger, riskier step (the screen goes
  black until it is right). Not started.
- Test scripts were scratch files (not in the repo); the phone was left as it was (ESCMODE and the DECON trigger restored after
  each test, display unchanged, m10i still booted).

## 53. Panel reset experiments and why S-Boot's DSIM state is not enough (2026-10-31, Jimmy watching)

- **gpd3-4 is the panel reset** (vendor DT `gpio_lcd_rst = <&gpd3 4 0x1>` in every a5y17/a7y17 variant; Linux: `gpiochip10` line 4;
  vendor sequence: 1 ms, high, 5 ms, low, 5 ms, high, 10 ms; high = run). Driving it low blanked the panel (Jimmy: "black
  screen"), so the pad really controls it. S-Boot leaves the pad as an input; once it has been driven, the line must be held
  high by software (a `gpioset` process held it; after the process died the pad stayed low and the panel stayed in reset;
  a reboot restores S-Boot's state). `gpioset -p 6ms` does NOT release the line after the period (it needed a timeout to die),
  and starting a second `gpioset` while the first holds the line fails silently (an early "reset" test therefore never pulsed).
- **Commands only leave the DSIM while the panel provides TE (or the main image path is in standby):** after the panel was in
  reset/sleep the SFR packets stayed in the header FIFO (FIFOCTRL bits 21:16 = 6 pending, INTSRC 0x23480000 with
  ABNORMAL_CMD_ST bit 22 and HSYNC_TOUT bit 19), even with SFR_CTRL standby cleared and the FIFO re-initialised. The link was
  then stuck until a reboot. So a panel re-init on top of S-Boot's DSIM state does not work.
- **Vendor behaviour:** with "LCD ON UBOOT" the vendor driver does not re-initialise the DSIM (only `dsim_reg_set_standby(0)`) and
  keeps S-Boot's PLL and D-PHY timings, yet its commands work. Our commands on the running panel were answered with
  `0x13800402` (ack with error report 0x8004: EoT sync error + protocol violation) in all HS/LP and trigger combinations, with
  clean FIFO handling, so the cause is still unknown. Possible differences from the vendor boot: its LPD / PACKET_GO handling
  around commands, or state S-Boot leaves that the vendor kernel changes when it enables the DECON (not found in the read files).
- **Conclusion for the takeover:** do the whole bring-up in our own code: panel power (regulators LCD_3P0/LCD_1P8 on the S2MPU05,
  numbers still unknown; they were never switched in these tests), reset pulse, DSIM software reset and re-init (vendor
  `dsim_reg_init`/`dsim_reg_set_clocks`/`set_lanes`: PLL PMS 3/230/1 from 26 MHz, escape clock 16 MHz, D-PHY timing table,
  config, command-mode TE registers), panel init sequence (sleep out, test keys, common settings, brightness, TE on, display on:
  vendor `s6e3fa3_init`/`s6e3fa3_displayon`), then hand the DECON trigger back. Prototype it as scripts on the live phone first
  (a failure only costs a reboot), then as a driver. Not started beyond the experiments above.

## 54. DSIM + panel bring-up prototype: what works, what is missing (2026-10-31, Jimmy watching)

Prototype = python on the phone through /dev/mem (DSIM 0x14800000, DECON 0x14830000) plus `gpioset` for the panel reset
(gpd3-4); a reboot always restores the screen (S-Boot re-initialises everything).
- **S-Boot's DSIM configuration is the vendor driver's, bit for bit:** PLL 0x806731 (P/M/S 3/230/1, 26 MHz in, ~997 Mbit/s), PLL_CTRL1
  0x40000000, PLLTMR 0x13880, PHYCTRL 0x04000000, M2 0x10000000, D-PHY timing 0x70c / 0x082b0d09 / 0x00090f0c (the 1000 Mbit/s row of the
  vendor table: clk_prepare 8, zero 43, post 13, trail 9, hs_prepare 9, zero 15, trail 12, lpx 7, hs_exit 12), CLKCTRL 0x00131f08 (esc
  divider 8), timeouts 0xffffffff / 0x3f, ESCMODE 0x01400000 (stop-state count 10), TE controls 0x00400061 (stable VFP 64, vsync timeout 97). The
  hardware config therefore needs no re-derivation; only the SEQUENCE matters.
- **Vendor power-on order (`dsim_enable`):** panel regulators on, PHY power, PLL + D-PHY timing + wait lock, lanes on + wait stop
  state, `dsim_reg_init` (esc/byte clocks, config, timing, TE regs), panel reset pulse (1 ms, high, 5 ms, low, 5 ms, high, 10 ms), then
  `dsim_reg_start` (HS clock request, standby on, interrupts), then the panel commands. With "LCD ON UBOOT" the vendor driver skips all of that
  (only standby off).
- **Prototype result (full DSIM stop, sw reset, vendor order, reset pulse):** PLL locks, lanes reach stop state, and on the fresh panel
  **every init command drains instantly** (sleep out 0x11, test keys F0/FC 5A 5A and A5 A5 as long writes, display on 0x29, normal mode 0x13,
  TE on) *provided the HS clock request (CLKCTRL bit 20) is on*; with HS requested off nothing leaves the FIFO.
- **Packet type finding:** the DSIM stalls (header FIFO fills, BTA with the error report `13800402` = EoT sync + protocol violation) on short
  writes WITH a parameter (type 0x15, e.g. TE on `35 00`) and on the max-return-size (0x37) / read request (0x06) pair, i.e. every read
  fails the same way. No-parameter short writes (0x05) and long writes (0x39) are fine: **TE on sent as a long write `39 | len 2 | 35 00` works** and
  nothing stalls. Cause unknown (the header layout is the vendor's `id | d0 << 8 | d1 << 16`). Consequence: panel reads (needed for the vendor
  brightness, which uses the MTP calibration data) are still unsolved; display on/off and fixed settings are fine.
- **Missing piece:** after the DSIM was reset the DECON no longer starts frames: FRAME_ID (+0x2a0) stays frozen, GLOBAL_CONTROL reads 0x133 (idle
  flag set) instead of 0x113, a global shadow update request and manual SW triggers (+0x70 bit 8) do nothing, so the panel (command mode, frames
  on TE) stays black. The full takeover therefore needs the DECON re-initialised in the vendor order (`decon_reg_init`/`decon_reg_start`, allowed
  file `decon_reg_7880.c`) after the DSIM. The 2 s "picture back" Jimmy saw in the middle of the experiments was without TE and did not last.
- Scratch scripts only (not in the repo). Pitfalls: `gpioset -p` does not exit after the period, a second `gpioset` on a held line fails silently,
  the reset pad must be held high by a process once it was driven, and `/tmp` on the phone is cleared by every reboot.

## 55. DECON re-init added to the bring-up prototype: frames work, the panel stays black (2026-10-31, Jimmy watching)

Same method as EVIDENCE 54 (python via /dev/mem, `gpioset` for the reset, reboot to recover). Sequence tested end to end:
DECON stop (mask trigger, direct off, shadow update, run off, SRESET) -> DSIM stop + sw reset -> PLL/D-PHY/lanes/clocks/config
(vendor `dsim_enable` order, values from the S-Boot snapshot) -> panel reset pulse -> DSIM start (HS clock, standby) -> panel start-up
commands -> DECON init (registers restored from the S-Boot snapshot with the trigger masked) -> DECON start (direct on, global shadow update,
wait run, unmask trigger).
- **Everything the host controls works:** DECON stops and resets cleanly; the DSIM PLL locks and the lanes reach stop state; every
  start-up command (sleep out, test keys, the vendor common settings CE/CC/ED/FD/12/30, gamma CA, AID B2, TSET B8, ELVSS B6, gamma update F7,
  ACL B5/55, TE on, display on, normal mode; all as 0x05 or 0x39 packets) drains from the header FIFO with no stall; the restored DECON
  registers equal S-Boot's (only status/counter registers differ); the DECON reports run (GLOBAL 0x133, the idle flag set while it waits).
- **Frames:** after the re-init the DECON sends **no frame on its own** (FRAME_ID stays at 0, no HS activity on the D-PHY): the HW trigger path
  (panel TE -> DSIM -> DECON) is dead, which means the panel is not producing TE. A **software trigger works**: put the DECON in SW trigger
  mode (`+0x70` = 0, HW_TRIG_EN off) and write `+0x70` bit 8; each pulse advances FRAME_ID by one, drives the D-PHY into HS bursts and raises
  FRAME_DONE (INT pending 0x01001000). SW triggers while HW_TRIG_EN is set are ignored.
- **Panel:** stays black in every variant (commands in HS or LP, EoT packet enabled or disabled via `DSIM_CONFIG` bit 28 which mainline sets for S6E3
  panels, TE on as a long write). No command ever had a visible effect on this panel from Linux, not even display off (`0x28`) on S-Boot's own state,
  and every read request is answered with the DSI error report `13800402` (EoT sync error + protocol violation). The panel therefore seems to
  discard the host's command packets; sending as long writes, in LP, or without EoT did not change that. The lanes' status register cannot show the short
  command bursts, so "packets on the wire" is unverified.
- **Unverified ideas for the next session:** (1) the panel's own supplies (LCD_3P0 / LCD_1P8 on the S2MPU05, LDO numbers unknown) and the PCD pad
  pinctrl (`disp_pcd`) are never touched by the prototype; the vendor switches both; (2) the D-PHY reset bit (`SWRST` bit 8, vendor
  `dsim_reg_dphy_reset`) and the MIPI PHY power/isolation (PMU) are not toggled; (3) a different gamma/ELVSS than the fixed template (no MTP read is
  possible while reads fail); (4) a logic analyser/scope is not available, so DSI traffic cannot be observed. A full mainline takeover
  (`samsung-dsim` + panel driver + DECON driver) would face the same unknown.
- Nothing from the prototype is in the repo (scratch scripts); the phone is back on S-Boot's display state (m10i, rebooted).

## 56. Software dimming: a backlight device and Phosh's brightness slider (2026-10-31, Jimmy watching)

**Result: m10j (`out/a5y17lte-mainline-m10j-boot.img`, sha256 `1d328a78…d99802`) gives the phone a brightness slider in Phosh that dims
the whole screen smoothly and cleanly (Jimmy: "I see the brightness slider and it works").** Chosen after the panel bring-up
(EVIDENCE 52-55) failed to get real panel commands working.
- **Driver (patch 0011, same file):** `devm_backlight_device_register("exynos7880-decon")`, type raw, 0-255, default 255. `update_status` maps
  the level (clamped to 8..255, never fully black) to a pixel scale `dim = int_sqrt(b * 65536 / 255)` (256 = full: pixels are gamma encoded,
  the square root makes the steps feel even); when `dim < 256` the damage copy uses `exynos7880_decon_copy_dimmed()` (two multiplies per
  pixel: R/B together, G alone; alpha forced to 0xff, written with `__raw_writel` into the write-combined scanout), otherwise the fast
  `drm_fb_xrgb8888_to_argb8888()`; a level change repaints with `drm_atomic_helper_dirtyfb(fb, NULL, 0, 0, NULL, 0)` (full damage commit). Kconfig
  selects BACKLIGHT_CLASS_DEVICE. Dimming pixels also lowers the AMOLED's power.
- **Phosh 0.58 drives it itself** (`backlight-sysfs.c`, `brightness-manager.c` are in the binary; gnome-settings-daemon 51.1's `gsd-power`
  exports no `Power.Screen` interface for it, which does not matter). The quick-settings slider appears and works; before, phoc logged "No backlight".
  Manual test: `echo 128 > /sys/class/backlight/exynos7880-decon/brightness` dims to about half, 40 is dark, 255 is full, picture clean.
- **Hardware dimming through the DECON blender did not work** (live /dev/mem tests; kept here so nobody repeats them): changing the alpha bytes of
  `WIN_CONTROL(0)` has no effect (window 0 is the bottom layer, FUNC = COPY, ALPHA_SEL = 2 = by pixel alpha); a black map-colour window 1
  (`WIN_CONTROL(1)` = alpha | CHMAP | FUNC 0xd | MAPCOLOR_EN | EN, `WIN_COLORMAP(1)` = 0, start/end/pixel count copied from window 0, global shadow update)
  showed nothing for CHMAP 0 and for CHMAP 1. Vendor constants: PD_FUNC_COPY 1, SOURCE_OVER 3, LEGACY 0xd, LEGACY2 0xe; ALPHA_SEL 0/1 = plane alpha 0/1,
  2 = by pixel alpha, 4 = multiplied.
- Not done: the brightness is not saved over a reboot (255 at boot); no ambient-light auto brightness (no sensors yet); real panel brightness/AOD
  still wait for the panel command path (EVIDENCE 55).

## 57. Battery: fuel gauge and charger status work (2026-10-31, Jimmy watching)

**Result: m11b (`out/a5y17lte-mainline-m11b-boot.img`, sha256 `d852e339…8078`) gives the phone a working battery in Phosh/UPower:
`s2mu004-fuel-gauge` (Battery) and `s2mu004-charger` (USB line power, online while the cable is in).** Jimmy: "the battery icon shows and
charging". The kernel side was checked over SSH (values below); m11a (`1404669c…0ab6`) was the gauge-only step.
- **Chip layout (vendor sources read read-only with Jimmy's OK: `drivers/battery_v2/s2mu004_{charger,fuelgauge}.c` + headers,
  `include/linux/mfd/samsung/s2mu004-private.h`, `board dtsi exynos7880-a5y17lte-common-battery.dtsi`):**
  - Charger/MUIC/flash chip at 0x3d on the MUIC bus (i2c-0): register enum SC_INT1 0x00, SC_INT2 0x01, INT masks 0x02/0x03, SC_STATUS0..5 0x0a..0x0f,
    SC_CTRL0.. 0x10... (matches the live dump). STATUS0 bits 7:5 = input status (3 or 5 = valid input; live 0xa3), STATUS1 bit 7 = discharging, bits 1:0 =
    done/top-off, STATUS3 bit 0 = battery detected. CTRL2 (0x12) = input current limit (value x 25 + 50 mA), CTRL9 (0x19) = fast charge current
    ((value + 1) x 25 mA). Live: CTRL2 = 0x12 (500 mA, the USB default), CTRL9 = 0x11 (450 mA).
  - **The fuel gauge is NOT on the MUIC bus:** vendor `i2c@13830000` (I2C channel 4, pins gpc4-0 SDA / gpc4-1 SCL, here bit-banged as `i2c-gpio-fg` =
    i2c-4), `s2mu004-fuelgauge@3B`, alert interrupt gpa0-3 (falling edge in the vendor driver, pull-up). Its registers are the S2MU005 gauge's
    (RVBAT 0x04, RCUR_CC 0x06, RSOC 0x08, MONOUT 0x0a, MONOUT_SEL 0x0c, ...), with the same scaling: mV = RVBAT * 1000 >> 13, current from RCUR_CC two's complement * 1000 >> 12
    (raw negative = charging), SOC = RSOC * 100 >> 14 percent, temperature = MONOUT (after MONOUT_SEL = 0x10) in 1/256 degree. S-Boot has already loaded the
    vendor's SDI battery model (the 4.35 V tables of the vendor DT are readable at 0x90..0xff and 0x0e..0x11), so no model loading is needed.
  - `usbpd-s2mu004@3C` (sec-usbpd) is listed on the gauge's bus in the vendor DT (not probed; EVIDENCE 44's "no PD chip" should be rechecked).
- **Kernel patches (mainline/patches 0012-0016, each with Assisted-by, no AI Signed-off-by):** 0012/0013 add the `samsung,s2mu004-fuel-gauge`
  binding value and an S2MU004 variant to the mainline `s2mu005-battery` driver (temperature property); 0014 reports the battery description
  (monitored-battery: design capacity, design voltages, technology, charge-now computed from RSOC); 0015/0016 are a new binding and driver
  `s2mu004-charger` (USB power supply, online from STATUS0, polled every 2 s because the chip's interrupt line is shared with the user-space
  MUIC handling; read-only, it does not control the charging). `a5-diag muic` is unaffected (it uses I2C_RDWR, which does not claim the address).
  DT: `i2c_fg` bus + `fuel-gauge@3b` (gpa0-3 falling edge, `fuel_irq` pins) + `charger@3d` under the MUIC bus + a `simple-battery`
  node (3000 mAh, 4.35 V max, 3.4 V min, Li-ion) in `mainline/dts/exynos7880-a5y17lte.dts`; config `CONFIG_POWER_SUPPLY`, `CONFIG_BATTERY_S2MU005`, `CONFIG_CHARGER_S2MU004`.
- **Live values (m11b, USB plugged, battery nearly empty):** gauge 3.53 V, capacity 1 % (RSOC 0x0064 is 0.6 %: the battery really is at the bottom, 3.48 V is below
  the vendor's empty point), current +175 mA charging, 31.1 deg C, charge_now 33 mAh of 3000; charger online=1, health Good. UPower: state charging, energy-full 13.05 Wh,
  "time to full 10.5 hours", icon battery-caution-charging-symbolic, plus a line_power_s2mu004_charger device.
- **Why charging is slow:** the charger input limit is the USB default 500 mA (2.5 W, minus what the running phone uses). The vendor raises it per cable type (its table:
  USB 500/800 mA input/fast, dedicated charger 900/1200, mains adapter 1800/2100, CDP 1500/1500, max input 3 A) from the MUIC's device-type registers, but after the
  boot-time manual MUIC switch those registers read 0 (DEV_TYPE1 0x62, TYPE2 0x63, CHG_TYPE 0x68, TYPE3 0x64 = 1), so the cable type is unknown and raising the limit blindly could overload a PC port.
  Open: find out what the MUIC/charger report with a wall adapter (test with one plugged in, reading over Wi-Fi), possibly use the AICL feature, then set CTRL2/CTRL9 per type (and handle the charger watchdog and top-off like the vendor).
- **Not verified:** the unplug/replug transition (a recording over Wi-Fi was started at the end of the session but produced no output; the cable was unplugged and replugged by Jimmy). Next session:
  repeat it (`online` should go 1 -> 0 -> 1, battery status Discharging while unplugged, UPower line-power device following).
- Other open points: gauge alert interrupt (the driver does not configure the thresholds, the IRQ is requested but not used), ambient charge-control, battery low shutdown policy
  (UPower acts on a discharging battery at its critical percentage; at about 1 % it will power the phone off when unplugged), the S2MU004 flash LED and the MUIC itself as proper drivers.

## 58. Charger probe: unplug transition verified; cable type is invisible after the manual MUIC switch (2026-11-01)

Tool: `tools/charge-probe.sh` (read-only logger: charger and MUIC registers, input/charge limits, battery; run detached on the phone, log in RAM).
The battery was dead overnight (about 1 % and 3.5 V at the end of 2026-10-31; the phone powered off by itself), charged and booted again at 3.30 V,
then died again once more on USB and had to be charged powered off; at 3.4 V and 0 % every short loss of input power browns the phone out.
- **Unplug/replug works end to end** (from the 2 s polling of the log): `online` 1 -> 0 -> 1, battery status Charging -> Discharging (about -370 mA) ->
  Charging, `STATUS0` 0xa3 (PC USB, input status 5) -> 0x03 (no input) -> 0x63 (input status 3, valid; the cable after the swap).
- **Cable type is not observable at runtime:** with the PC port and after the swap the MUIC's `DEVICE_TYPE1` (0x62), `DEVICE_TYPE2`, `CHG_TYPE` (0x68) and `ADC`
  all read 0x00 (`TYPE3` 0x01, `DEVICE_APPLE` 0x06 with a charger/PC attached, 0x04 unplugged); the input current limit (`CTRL2` 0x12 = 500 mA) and the fast-charge
  setting (`CTRL9` 0x11 = 450 mA) never changed. Cause: `a5-diag muic` clears `CTRL1` bit 2 (manual switching) and forces D+/D- to USB so that the PC link works; in manual mode the
  MUIC does not refresh the detection registers. The `STATUS0` input field (5 on the PC port, 3 later) is not a reliable cable indicator (it may also depend on the VBUS level and the charge state).
- **The charging speed is limited to 500 mA input** (a wall charger gives the same ~230-270 mA to a battery at 3.4 V while the phone runs); the vendor driver sets 900-3000 mA by cable type
  (dedicated charger = `DEVICE_TYPE1` 0x40/0x44/0x60, CDP 0x20, USB 0x04, valid VBUS in `DEVICE_APPLE`).
- **Idea, not built:** at boot `a5-diag muic` reads these registers before the manual switch, i.e. S-Boot's detection of the cable that was attached at power-on. Log them to the kernel
  log (or /run) there, check them once with the PC cable and once with a wall charger attached at power-on, and if they distinguish the types, let a service raise the charger input limit
  accordingly (the charger driver would need a writable `input_current_limit` that falls back to 500 mA when the cable goes away). Hot-plugged wall chargers would stay at the safe default.
  Alternative: the USB gadget state (`/sys/class/udc/*/state`: configured = a host is there, no enumeration after a few seconds = plain charger), which needs VBUS handling that mainline dwc3 does not have here.
- The test must be done with a battery that has some charge (suggest above 30 %), charged with the phone powered off.
- **Brownout aftermath (Jimmy, 2026-11-01):** after the crash during the cable swap at about 3.4 V the phone would not power on: it hung at the Samsung logo, needed two
  download-mode cooldowns and a TWRP -> Reboot -> System to recover; then it was left powered off on the wall adapter. Lesson: with the battery below about 3.5 V never interrupt the
  input power on purpose (no live cable swaps, no flashing without a charger attached), and charge it powered off before any test that touches the cable. A phone that dies at the logo is
  a battery-level problem, not a boot-image problem, so do not suspect the image first.

## 59. Boot-time MUIC logging prepared, NOT yet tested (2026-11-01)

Built only, nothing flashed or booted (the battery is being charged powered off, EVIDENCE 58).
- `mainline/init/a5-diag.c`, `cmd_muic()`: before the manual switch it now reads the MUIC (ADC 0x61, DEVICE_TYPE1..3 0x62..0x64, CHG_TYPE 0x68, DEVICE_APPLE 0x69) and the charger
  `SC_STATUS0` (0x0a), together with CTRL1 and SW_CTRL, and writes one line to the kernel log through `/dev/kmsg`:
  `a5-diag: muic before the switch: rev .. adc .. dev1 .. dev2 .. dev3 .. chg .. apple .. st0 .. ctrl1 .. sw ..`. The switching logic and the console output are unchanged.
  Compiles with the build's `-Wall -Werror`. Image `out/a5y17lte-mainline-m11c-boot.img`, sha256 `a1376eb4…923f5e`, same kernel/config/DT as m11b (the installed module set matches).
- **Test plan** (battery above about 30 %, charged powered off first): flash m11c from TWRP; boot 1 with the phone on the PC's USB port, read `dmesg | grep 'a5-diag: muic'`;
  boot 2 with the wall charger plugged in at power-on and no PC (SSH over Wi-Fi), read the same line. If `dev1` differs (0x04 USB versus 0x40/0x44/0x60 charger, or the apple/chg bytes), a service can
  raise the charger input limit for the charger case; otherwise the cable cannot be told apart at boot either and the limit stays at 500 mA.

## 60. Boot-time MUIC cable detection: no difference between PC port and wall charger (2026-11-02)

m11c (EVIDENCE 59) flashed to BOOT from TWRP (battery 100 %, 4.37 V; sha256 `a1376eb4…923f5e` verified on the phone before the write and by pull-back after it) and booted twice.
- **Boot 1, PC USB port:** `a5-diag: muic before the switch: rev 29 adc 00 dev1 00 dev2 00 dev3 01 chg 00 apple 06 st0 a0 ctrl1 17 sw 00`.
- **Boot 2, wall charger plugged in at power-on, no PC (SSH over Wi-Fi):** `rev 29 adc 00 dev1 00 dev2 00 dev3 01 chg 00 apple 04 st0 00 ctrl1 17 sw 00`.
- The cable-type registers (adc, dev1, dev2, dev3, chg) are identical, so S-Boot's detection left no cable-type information to use. Only `apple` (06 vs 04) and `st0` (a0 vs 00) differ; `st0` read 00 at 0.76 s
  although the charger reports online a little later, so both look like settling time, not cable type. Conclusion: the MUIC registers cannot tell the cable at boot either; the idea in EVIDENCE 58 fails.
- With the wall charger and no host: `/sys/class/udc/13600000.usb/state` = `not attached`, `usb0` NO-CARRIER. With a PC port it is expected to be `configured` (not yet logged).
- **Next idea, not built:** use the gadget state as the signal (VBUS present but no host enumerates within N seconds = plain charger), with a writable `input_current_limit` in the charger driver that falls back to
  500 mA when the state changes or VBUS drops. To check first with `tools/udc-probe.sh` (read-only, 1 s log of UDC state, usb0 carrier, charger online, battery current, STATUS0, DEVICE_APPLE): PC port gives `configured`?
  a PC port that does not enumerate (suspended host) and a hot-plugged wall charger give what, and does the state stay stable?
- **Observation (Jimmy), cause unknown:** at 100 % battery, pressing Power from fully off did nothing; the phone only came up after download mode and Volume Down, and stuck briefly at the Samsung logo first. Not a battery-level
  problem this time. It may be an S-Boot or PMIC state left over from the earlier brownouts (EVIDENCE 58); the m11c image has the same kernel, config and DT as m11b and only runs after S-Boot, so it is not suspected.
  Watch whether it repeats.

## 61. USB gadget state as a cable-type signal: PC and charger look the same when enumeration fails (2026-11-02)

One 420 s run of `tools/udc-probe.sh` with hot-plugs (battery 100 %), plus a final check. Logs live in the phone's RAM, not in git.
- Wall charger present at boot: UDC `not attached`, usb0 carrier 0. PC port plugged in (first time): `default` for ~1 s, then `configured`, carrier 1, stable. Wall charger hot-plugged: `default`, carrier 0, stable for
  over 4 minutes. Unplug: online 0 within a second, `st0` 0x00, `apple` 0x04. Contact bounces gave online dips of 1-3 s while the UDC state stayed put.
- **Final check, PC cable plugged in (hot-plug), wall charger removed:** the phone read online 1, `udc=default`, carrier 0, exactly like the wall charger; the PC's dmesg showed `device not accepting address .., error -71`,
  `unable to enumerate USB device` (the known hot-plug enumeration failure, EVIDENCE 35-36). So a PC port whose enumeration fails is indistinguishable from a plain charger by the gadget state.
- **Conclusion:** the gadget state cannot be used to raise the input limit automatically: a failed enumeration on a PC port (500 mA maximum) would be taken for a charger. Not built. The MUIC registers do not help either
  (EVIDENCE 60). Options left: an explicit user opt-in (writable `input_current_limit` in the charger driver plus a toggle, wall chargers only), or fixing the hot-plug enumeration and the MUIC auto-detection
  (the boot-time manual switch to USB disables the hardware cable detection). Until then the input limit stays at the safe 500 mA.

## 62. Bluetooth: which UART, and what is missing (2026-11-02, investigation only, no code, nothing flashed)

Read from the project's vendor reference (`upstream/k7880`, as in EVIDENCE 35) and our tree.
- Vendor `bluetooth { compatible = "qca,qca6174"; BT_EN gpd4-2, BT_WAKE gpd5-1, BT_HOST_WAKE gpa1-6; vdd-wlan-supply = cnss_dcdc_en; pinctrl bt_hostwake/bt_btwake/bt_en }` names no UART. The vendor
  `exynos7880.dtsi` has two: `serial_0` at 0x13810000 (pins `uart0_bus` = gpb0-0..3, four wires, i.e. with RTS/CTS flow control) and `serial_2` at 0x13820000 (`uart2_bus` = gpa1-0/1, two wires, the debug console;
  U-Boot uses it, EVIDENCE 2). `uart1_bus` (gpd2-0..3) is a third set but gpd2-3 is the vibrator pin on this board. Both nodes are enabled in the A5's common dtsi; serial_0 is the only one with flow-control
  pins, so **the Bluetooth HCI UART is almost certainly 0x13810000 on gpb0-0..3** (to confirm by probing: a HCI reset at 115200 8N1, then 3 Mbaud after the firmware download).
- Our tree: `mainline/dts/exynos7880.dtsi` has no UART nodes and only CMU_FSYS (plus fixed clocks for the MMC cards), so a UART node needs a clock. The 7870 CMU driver already has CMU_PERI (cmu_peri at 0x101f0000
  with UART0..2 gates, `CLK_GOUT_PERI_UART0_PCLK/EXT_UCLK`, mainline `exynos7870.dtsi` serial0 at 0x13800000, serial1 at **0x13810000**, serial2); the vendor 7880 numbering "serial_0" is mainline's `serial1`.
  Open: whether the 7880 PERI clock layout matches the 7870 one (the FSYS CMU did, with the `exynos7880-cmu-fsys` compatible falling back to 7870), the UART input clock rate, and whether S-Boot left the UART0/1 gates open.
- Mainline `hci_qca` handles `qcom,qca6174-bt` (QCA_ROME) over a serdev UART. Firmware names for Rome 3.2/TF 1.1 (`rampatch_tlv_tf_1.1.tlv`, `nvm_tlv_tf_1.1.bin`, from the SYSTEM backup) must be checked
  against what the driver requests (`qca/rampatch_*`, `qca/nvm_*`); the QCA9377's BT ID may need a quirk.
- Plan (each step reversible, no eMMC write until a rootfs change is wanted): (1) add a `cmu_peri` node and the UART1 (0x13810000) node with `uart0_bus` pins, serdev child `bluetooth` with
  shutdown-gpios gpd4-2, device-wakeup gpd5-1, host-wakeup gpa1-6, vddio from `cnss_dcdc_en`; (2) kernel config `CONFIG_BT`, `BT_HCIUART`, `BT_HCIUART_QCA`, serial8250-free Samsung UART (`SERIAL_SAMSUNG`);
  (3) new boot image, test with `hciconfig`/`bluetoothctl`; (4) firmware files into the rootfs (or `/lib/firmware/qca/`) by hand first, packaged later.

## 63. Bluetooth: UART clock measured, m12a built (2026-11-02), NOT flashed, untested

- **UART_BTWIFIFM clock (read on the phone via /dev/mem, read only):** CMU_CCORE (0x10680000) `CLK_CON_MUX_CLKCMU_PERI_UART_BTWIFIFM` (+0x2a4) = 0x00200000 (select bit 12 = 0 = BUS_PLL/2 = 799.5 MHz,
  gate bit 21), `..DIV..` (+0x4a4) = 5 (4-bit field, /6), `..ENABLE..` (+0x8a4) = 1; CMU_PERI `CLK_ENABLE_CLK_PERI_UART_BTWIFIFM` (0x101f0830) = 1 (PCLK/EXT_UCLK gates open).
  So the baud-source clock is 799.5 / 6 = **133.25 MHz** (same BUS_PLL/2 arithmetic as the eMMC clock, EVIDENCE 17b). The vendor mapping is serial_0 (0x13810000) = btwififm, serial_2 (0x13820000) = debug (console), the third = sensor.
  (A combined read of more registers in one run gave a bus error; single reads work.)
- **m12a** (`DECON=1 MILESTONE=m12a ./build-mainline.sh`, sha256 `99c1d588…865974`, 16027664 B): `mainline/dts/exynos7880.dtsi` gets two TEMPORARY fixed clocks (133.25 MHz) and `serial_bt: serial@13810000`
  (`samsung,exynos7870-uart` + `exynos8895-uart` fallback, SPI 422, FIFO 256); `exynos7880-a5y17lte.dts` gets the pin groups (`gpb0-0..3` function 2; BT_EN `gpd4-2` output) and
  `&serial_bt { bluetooth { compatible = "qcom,qca6174-bt"; enable-gpios = <&gpd4 2 ACTIVE_HIGH>; max-speed = <3000000>; } }`. BT_WAKE (gpd5-1) and BT_HOST_WAKE (gpa1-6) are not wired yet; the chip's supply is
  `cnss_dcdc_en` (stays on only while the Wi-Fi card is bound; not handled by hci_qca). Config: `SERIAL_SAMSUNG`, `SERIAL_DEV_BUS`, `BT`, `BT_HCIUART`, `BT_HCIUART_SERDEV`, `BT_HCIUART_QCA`, `BT_QCA` all built in (=y).
- Not yet checked: that the rootfs module set (cfg80211, mac80211, ath10k..., 11 modules) still matches the m12a build (the phone was off the network when I tried; compare sha256 of `/usr/lib/modules/7.2.0-a5y17lte` against `build/modules` before flashing).
- Test plan: flash from TWRP (Jimmy's OK), boot, `dmesg | grep -iE 'bluetooth|hci|qca|ttySAC'`, `ls /sys/class/bluetooth`, `hciconfig -a`/`bluetoothctl list`; expected first problems: wrong UART clock, firmware file names (`qca/rampatch_*`, `qca/nvm_*`), chip power. Fallbacks: m11c, m11b.

## 64. Bluetooth works on m12a (2026-11-02)

m12a flashed to BOOT from TWRP (sha256 `99c1d588…865974`, read-back verified), booted on Wi-Fi.
- UART at 0x13810000 comes up as `ttySAC0`/`serial0-0` with the measured 133.25 MHz clock; `hci_uart_qca` finds the chip: `QCA Product ID 0x08, SOC 0x23, ROM 0x302, patch 0x111, controller 0x00230302`.
- **Power timing:** the first two setup attempts (0.26 s and ~8 s after boot) time out (`command 0xfc00 tx timeout`), the third at ~21 s works. Cause (likely): the chip's supply `cnss_dcdc_en` (gpd3-3) is only switched on when the Wi-Fi SDIO card is probed. Fix for a later image: give the bluetooth node its own reference to the regulator (it is not handled by `hci_qca` for this chip), or make `cnss_dcdc_en` `regulator-boot-on`.
- **Firmware:** `qca/rampatch_00230302.bin` and `qca/nvm_00230302.bin` are in linux-firmware (pmOS package `linux-firmware-qca`, 20260916-r0); installed on the phone with `apk add linux-firmware-qca` (a SYSTEM write, with Jimmy's OK; not in git, not in the rootfs image build yet).
  After that, unbinding/rebinding the serdev driver (`/sys/bus/serial/drivers/hci_uart_qca/{unbind,bind}`) ended with `QCA setup on UART is completed`.
- **Radio test:** `apk add bluez` (also with Jimmy's OK), `rc-service bluetooth start`, `bluetoothctl power on` and `scan on` for 15 s found 10 nearby devices (own test device included), so TX/RX and the antenna path work.
  The controller address is the placeholder `00:00:00:00:5A:AD` (the chip has no BD address programmed; a real one needs the NVM tag or `btmgmt public-addr`/`bdaddr` quirk, to be decided).
- **Still open:** pairing/audio test with a real device; the power-timing fix; `CONFIG_RFKILL` (no `/dev/rfkill`, so Phosh's Bluetooth switch cannot work); `rc-update add bluetooth` and the service ordering; BT_WAKE/BT_HOST_WAKE (low power); a stable BD address; package the firmware and bluez in `build-pmos.sh`.

## 65. m12b built: rfkill and early Bluetooth supply (2026-11-02), NOT flashed, untested

- Changes on top of m12a: `CONFIG_RFKILL=y` (gives `/dev/rfkill`, needed for Phosh's Bluetooth switch) and `regulator-boot-on` on `cnss_dcdc_en` (gpd3-3, the combo chip's supply), so the supply is on before `hci_qca`
  makes its first setup attempt (the unwired Wi-Fi-side probe used to switch it on ~8 s into boot; EVIDENCE 64). `regulator_ignore_unused` keeps it on. The Wi-Fi card's power sequencing (WLAN_EN, gpd3-6) is unchanged.
- Image `out/a5y17lte-mainline-m12b-boot.img`, 16027664 B, sha256 `10b9303b…990e4b`. Module set: only `net/wireless/cfg80211.ko` differs from the installed one (new sha `62f40ac2…187b23`; the other 10 are
  bit-identical), because cfg80211 grows rfkill support. mac80211/ath10k are unchanged, so the exported symbol versions should not have moved; to be safe the new cfg80211.ko is copied to the phone's
  `/usr/lib/modules/7.2.0-a5y17lte/kernel/net/wireless/` (a SYSTEM write, needs Jimmy's OK) before or right after flashing; the installed old one should still load, without the rfkill hook (to check).

## 66. m12b flashed: Bluetooth comes up by itself at boot (2026-11-02)

m12b (sha256 `10b9303b…990e4b`) flashed from TWRP and read back; the new `cfg80211.ko` (sha `62f40ac2…187b23`, old one kept as `cfg80211.ko.m12a-old`) copied into the rootfs; Wi-Fi works.
- `regulator-boot-on` on `cnss_dcdc_en`: the chip answers the very first setup (0.26 s) instead of timing out twice. `/dev/rfkill` exists (`rfkill list` shows hci0 soft/hard unblocked).
- The first setup still fails with -2 (firmware not found: the rootfs is not mounted yet at 0.3-0.9 s). Fix: OpenRC service `a5y17lte-btfw` (`pmos/device-samsung-a5y17lte/a5y17lte-btfw.initd`, installed as
  `/etc/init.d/a5y17lte-btfw`, `rc-update add ... default`, plus `rc-update add bluetooth default`) rebinding the serdev driver once the rootfs is up (SYSTEM writes, with Jimmy's OK).
- **Race found:** the first version rebound at ~11.7 s while the Wi-Fi firmware was still starting (ath10k firmware 9.0 to 13.9 s on the same combo chip); setup finished but the controller then hung at bluez's first
  command (`Opcode 0x0c24 failed: -110`, "crash the soc", repeated `Reading QCA version information failed (-110)`). A manual rebind with Wi-Fi up worked. The service now waits for `wlan0` and 5 s more.
- **Result after the second reboot:** setup at 20.9 s ("QCA setup on UART is completed" at 21.9 s), `bluetoothctl show` = Powered: yes, a 10 s scan found 7 devices, no hci0 timeout in dmesg, service started on its own.
- Still open: pairing/audio, BT_WAKE/BT_HOST_WAKE, a real BD address (placeholder 00:00:00:00:5A:AD), Phosh's Bluetooth switch (not tried), put firmware + bluez + the service + cfg80211/rfkill config into `build-pmos.sh`/the device package.

## 67. Bluetooth pairing check (2026-11-02)

Jimmy's own phone (A55) was already paired with the A5 (pairing done by Jimmy earlier, from the A55 or Phosh). `bluetoothctl info` on the A5: Paired yes, Bonded yes, Trusted yes, Connected yes; a new `pair` returned
`org.bluez.Error.AlreadyExists`. So pairing, bonding and the encrypted ACL link work through the UART HCI path on m12b. Not tested: audio profiles (the A5 has no audio driver yet), file transfer, reconnect after reboot,
the Phosh Bluetooth switch. The pairing keys live in `/var/lib/bluetooth` on SYSTEM.

## 68. Bluetooth packaged for the rootfs build (2026-11-02), build NOT run

- Device package `device-samsung-a5y17lte` 3-r11: depends `linux-firmware-qca` and `bluez`; the openrc subpackage depends `bluez-openrc`, installs `a5y17lte-btfw.initd` as `/etc/init.d/a5y17lte-btfw`; the post-install and
  post-upgrade scripts add `bluetooth` and `a5y17lte-btfw` to the default runlevel; `build-pmos.sh` also checks those two services are in the image's default runlevel (the same ordering trap as the others).
- Kernel package `linux-postmarketos-exynos7880` 7-r5: lists patches 0012-0016 (battery and charger; `build-pmos.sh` refuses to run when a patch is missing from the APKBUILD, so the previous 7-r4 could not be built any more). Its kernel `.config` (RFKILL, BT, SERIAL_SAMSUNG,
  battery) and the DT (UART, regulator-boot-on) come from the same files as the boot image, so the packaged `cfg80211.ko` is the rfkill one (sha `62f40ac2…187b23`, to be verified in the built image).
- Not done: the `./build-pmos.sh` run itself (needs sudo, long), so nothing is built or flashed. The package DT does not switch the display driver on (`&decon` is only added by `DECON=1` in build-mainline.sh), the same as before.
  Pending for the rootfs rebuild: the CMA modules for the display driver, the hand-installed extras still on the phone (python tools, etc.), brightness persistence.

## 69. Rootfs rebuilt with Bluetooth (2026-11-02), built and checked on the PC only, NOT written to the phone

`./build-pmos.sh` (sudo, ~9 min: kernel package 7-r5 cross-built in the chroot, device package 3-r11) produced `build/pmos/samsung-a5y17lte-root.img`, 1781530624 B, sha256 `b9a0598e…4b355`, e2fsck clean (22932 files, 370063/434944 blocks).
- In the image (debugfs): default runlevel has `bluetooth`, `a5y17lte-btfw`, `a5y17lte-keyled`, `a5y17lte-usbnet`; `/lib/firmware/qca` has `rampatch_00230302.bin.zst` and `nvm_00230302.bin.zst`; `/usr/lib/bluetooth/bluetoothd`, `/usr/bin/bluetoothctl`, `/etc/init.d/a5y17lte-btfw` exist.
- The packaged modules (`/usr/lib/modules/7.2.0-a5y17lte`) are NOT byte-identical to `build/modules` of the boot-image build (cfg80211 616584 B vs 650096 B, mac80211 856728 vs 908088, different sha256): same input `.config`
  (the pmaports copy equals `upstream/linux/.config`), same vermagic `7.2.0-a5y17lte SMP preempt aarch64`, no CONFIG_MODVERSIONS, but the pmOS chroot compiler/flags differ from the host build. Struct layouts follow the config, so they
  should load (earlier rootfs images used the same arrangement and Wi-Fi worked), but it is unproven for this image.
- **What a rootfs write would wipe:** everything on SYSTEM that is not in the image: the saved Wi-Fi connection (NetworkManager), the Bluetooth pairing in `/var/lib/bluetooth`, the user's home, hand-installed extras (python tools, the m12 `cfg80211.ko` swap), `rc-update` changes, settings. After a write the Wi-Fi password has to be entered again in Phosh and the A55 re-paired.
  Writing it is a SYSTEM write: needs Jimmy's OK, TWRP, a pull-back compare on the PC, then growing the filesystem to 4.6G with ext4-grow (EVIDENCE 25) and a reboot test, as for EVIDENCE 33/39.

## 70. Sensors: what the board has and what is missing (2026-11-02, investigation only, no code)

From the project's vendor DT reference (`upstream/k7880`, `exynos7880-a5y17lte_eur_open_05.dts`); the vendor sensor-hub driver is not in that clone (it only has `include/linux/ssp_platformdata.h`).
- **One sensor hub MCU, no individual sensor chips on the AP buses:** `spi_2: spi@13930000` ("SENSORHUB", pins `spi2_mosi_sck_ssn` + `spi2_miso` = gpc4-2, gpc5-0, gpc5-1, chip select gpc4-3 as a GPIO) with `STM32F@0`
  (`compatible "ssp,STM32F"`, SPI mode 3 (cpol+cpha), max 8 MHz). Control lines: reset gpf4-2 (`ssp,rst-gpio`, active-high flag), boot0 gpd2-0 (`ssp,boot0-gpio`), `ap_int` gpd1-5 (AP to MCU), `mcu_int1` gpa0-7 and
  `mcu_int2` gpa2-5 (MCU to AP interrupts); there is also a pin group `ssp_mcu_resp` on gpd1-6 (rev 0.1). Board data: accelerometer position 5, magnetometer position 7, `ssp-acc-type 1`, `ssp-mag-type 1`, `ssp-pressure-type 1`,
  `ssp-glass-type 0`, proximity thresholds (hi 55, low 40, detect hi 250, detect low 130), AP rev 1. So there are accelerometer, magnetometer (compass), proximity and (type flags) pressure, possibly light and gyro, all behind the MCU.
- **Firmware:** the original SYSTEM backup has no sensor-hub firmware file in `/etc/firmware`, `/vendor/firmware`, `/system/etc/firmware`; Samsung usually builds the MCU firmware into the kernel (`firmware/` of the vendor tree) and the AP flashes it
  over the bootloader when the version differs, so the MCU most likely already holds a working firmware in its own flash and starts it on reset (BOOT0 low). Not verified.
- **Mainline has nothing for it:** no SSP/sensor-hub driver, and our DT has no SPI controller node yet (the Exynos 7870 SPI driver `spi-s3c64xx` exists; the PERI clocks for it are the same unsolved CMU_PERI question as the UARTs, EVIDENCE 63).
- **What is needed:** (1) the hub's message protocol (framing, instruction codes, sensor enable/read, interrupt handling, which only the vendor `ssp` driver documents), (2) an SPI node with fixed clocks like the UART one, (3) an IIO driver (accelerometer/magnetometer/proximity/light) or an iio-sensor-proxy feed for Phosh auto-rotate and proximity.
- **Blocked on a permission:** reading the vendor sensor-hub driver (Timeshift copy, `drivers/sensorhub/...` or `drivers/sensors/ssp`) is not covered by what Jimmy allowed so far; without it only blind probing of the MCU is possible (reset it, clock bytes on SPI mode 3, see what comes back).

## 71. Sensor hub protocol, read from the vendor driver (2026-11-02, read-only, nothing copied, no code written)

Jimmy allowed reading the vendor sensor-hub driver read-only (Timeshift copy, `drivers/sensorhub/stm32/`: `ssp_spi.c`, `ssp.h`, `ssp_data.c`, `ssp_dev.c`, `ssp_sensorlist.h`; the older `brcm/` variant is for another board). Notes in my own words:
- **Firmware:** the MCU firmware is a file in the vendor tree's `firmware/` (`ssp_stmf410_a2017.fw.ihex`, i.e. an STM32F410); the AP only reflashes it when the revision differs (bootloader mode via boot0 + reset). Normal operation: BOOT0 low, pulse reset
  (reset low ~1 ms, then high), wait ~500 ms. So with the firmware already in the MCU's flash nothing needs downloading; if it is blank or too old, a flash tool would be needed (not planned).
- **Wires:** SPI2 (mode 3, 8 MHz max) with chip select as a GPIO (gpc4-3); pins MOSI/SCK/CS gpc4-2, gpc4-3, gpc5-1 and MISO gpc5-0, pin function 3. Handshake lines: `ap_int` gpd1-5 (output, AP asks), `mcu_int2` gpa2-5 (MCU busy/ready level, input),
  `mcu_int1` gpa0-7 (MCU data-ready interrupt, falling edge, input), reset gpf4-2 (output), boot0 gpd2-0 (output, 0).
- **AP to MCU message:** a 9-byte header, little endian, packed: `cmd` (1 byte), `length` (u16), `options` (u16), `data` (u32). Sending: pull `ap_int` low, wait until `mcu_int2` is low (it polls up to ~1.5 s), write the 9 bytes, raise `ap_int`, wait for `mcu_int2` high again,
  then the payload phase happens when the MCU interrupts: on `mcu_int1` falling, the AP reads 4 bytes = `options` (u16) and `length` (u16). The low 2 bits of `options` are the type: 0 AP2HUB_READ (AP then reads `length` bytes: the answer), 1 AP2HUB_WRITE
  (AP then writes the payload), 2 HUB2AP_WRITE (the MCU pushes a data frame of `length` bytes that the AP reads and parses). Options bit 2 (AP2HUB_RETURN, value 4) means "after writing, read one result byte". A reply is matched to the pending request by equal `options`.
- **Commands (cmd byte):** WHOAMI 0x0f (read 1 byte; the MCU answers 0x55, the driver retries twice, 5 ms apart); sensor scan 0xf4 (read 8 bytes: bitmask of present sensors, bit n = sensor type n); firmware revision 0xf0; sensor formation 0xf1;
  proximity thresholds 0xf2; light coefficients 0x49; set time 0xfe, get time 0xff; flush 0xdd, batch count 0xdf; AP state (sleep 0xd2, resume 0xd3, ...). Sensor control: add sensor 0xa1, remove 0xa2, change delay 0xa4; their payload is
  `[sensor type][delay ms u32][batch latency u32][batch flags u8]` (length 10 with the type byte), written with options AP2HUB_WRITE.
- **Sensor type numbers (Android numbers):** accelerometer 1, magnetic field 2, gyroscope 4, light 5, pressure 6, proximity 8, gravity 9, ..., uncalibrated magnetic 14, game rotation 15, uncalibrated gyro 16, significant motion 17, step detector 18, step counter 19,
  tilt 22, pick-up gesture 25, Samsung private ones (light CCT, ...) above. Report sizes: accelerometer 6 bytes (3 x s16), magnetic 7, gyro 6, light 4 (8 for CCT), proximity 1 (+1), pressure 6/14.
- **Data frame from the MCU (HUB2AP_WRITE payload):** a sequence of items, each starting with an instruction byte: 0x37 sensor data (then `type` byte, `count` u16, then `count` x [sample data, 4-byte timestamp delta in microseconds]), 0x01 library data, 0x03 debug text,
  0x04 big data, 0x05 meta data (2 bytes: what, sensor), 0x06 time sync request, 0x07 MCU reset notice (the AP then re-initializes: scan, positions, thresholds).
- **Start-up order in the vendor driver:** GPIOs, IRQ on `mcu_int1` (falling, threaded), reset pulse + 500 ms, check the bootloader/firmware state, WHOAMI, sensor position (accel/mag position values from the DT), sensor scan, magnetometer init, light coefficients, firmware revision;
  then enable sensors by adding them with a delay. Batching, big data, libraries (gesture, calibration) are optional.
- **What mainline needs:** (1) `CONFIG_SPI_S3C64XX` with an `exynos7870`-style SPI node for 0x13930000 (SPI2) with fixed clocks like the UARTs (EVIDENCE 63; the rate has to be measured from CMU_CCORE/CMU_PERI), (2) an `iio`/input driver doing the protocol above.
  A quick first step needs no driver: a `spidev` node plus a userspace probe (python + libgpiod, as for the panel) doing reset, handshake and WHOAMI; if the MCU answers 0x55 and the scan mask lists accelerometer/magnetometer/proximity, a kernel driver is straightforward.

## 72. SPI2 clock measured, m13a and the first sensor-hub probe built (2026-11-02), NOT flashed, untested

- **SPI2 (SPI_SENSORHUB) clock, read on the phone via /dev/mem (read only):** CMU_CCORE (0x10680000) `CLK_CON_MUX_CLKCMU_PERI_SPI_SENSORHUB` (+0x2c0) = 0x00200000 (select bit 12 = 0 = BUS_PLL/2 = 799.5 MHz, gate bit 21),
  `..DIV..` (+0x4c0) = 9 (6-bit field, /10), `..ENABLE..` (+0x8c0) = 1: the SPI source clock is **79.95 MHz**. The PERI-side gate was not looked up (S-Boot left the block usable for the UART case).
- **m13a** (`DECON=1 MILESTONE=m13a ./build-mainline.sh`, sha256 `fca3f4a7…ccf7bca`, 16097296 B): on top of m12b, `exynos7880.dtsi` has two TEMPORARY fixed clocks (79.95 MHz) and `spi_2: spi@13930000`
  (`samsung,exynos7-spi`, which the binding marks deprecated but the driver still matches; SPI 455, PIO only, no DMA node), the board DT enables it with pins (MOSI/SCK gpc4-2, gpc5-1 and MISO gpc5-0 function 3, chip select gpc4-3 as a
  GPIO via `cs-gpios`) and a `sensorhub@0` child (`rohm,dh2228fv` as the stand-in compatible so spidev binds; mode 3, 4 MHz). Config: `SPI`, `SPI_S3C64XX`, `SPI_SPIDEV` built in. The 11-module rootfs set is bit-identical to m12b's, so no module change is needed.
- **Probe tool** `mainline/tools/sensorhub-probe.c` (static aarch64, built with `upstream/userspace/bin/aarch64-musl-cc -static`, no library, not in git as a binary): GPIO character device v2 + spidev; `sensorhub-probe reset` pulses reset with boot0 low,
  waits 500 ms, then sends WHOAMI (0x0f, expects 0x55), the sensor scan (0xf4, 8 byte mask) and the firmware revision (0xf0), printing the handshake levels (mcu_int1/mcu_int2), every message header and any frame the MCU pushes. It only writes the SPI
  headers and the five GPIOs (reset gpf4-2, boot0 gpd2-0, ap_int gpd1-5 outputs; mcu_int1 gpa0-7, mcu_int2 gpa2-5 inputs); the binary is pushed to the phone's RAM (/tmp) only.
- **Test plan:** flash m13a (Jimmy's OK, TWRP), boot, check `ls /dev/spidev0.0` and dmesg for the SPI driver, push the probe, run it once without and once with `reset`. If WHOAMI answers 0x55 the transport is right.
  Risks: the MCU may be silent without the AP's full start-up (an MCU reset notice, then the AP initialization), the handshake polarity may differ from my reading, and a wrong GPIO pull could confuse the MCU (it recovers with a reset).

## 73. m13a SPI probe failed: missing alias; m13b (2026-11-02)

m13a (sha256 `fca3f4a7…ccf7bca`) was flashed and read back, booted on Wi-Fi: `s3c64xx-spi 13930000.spi: error -19: Failed to get alias id`, so no SPI bus and no `/dev/spidev*`. The driver takes its bus number from an `aliases` entry.
- Fix in `exynos7880.dtsi`: `spi2 = &spi_2;` (the device is then `/dev/spidev2.0`; the probe tool now opens that path). m13b: `DECON=1 MILESTONE=m13b ./build-mainline.sh`, sha256 `f5783a93…dd1264`, 16097296 B, same module set as m12b/m13a.
- The probe binary was already pushed to the phone's /tmp (RAM); it has to be pushed again for the new path.

## 74. Sensor hub: first contact, the MCU answers the handshake but not the first request (2026-11-02)

m13b (sha256 `f5783a93…dd1264`) flashed and read back, booted on Wi-Fi: `spi spi2.0` probes, `/dev/spidev2.0` exists. `sensorhub-probe` (static, pushed to the phone's /tmp; `pulse` and `reset` modes, env `SPI_MODE`, `SPI_HZ`, `WATCH_MS`):
- GPIOs: all five lines request fine (the gpiochips are labelled by bank name, numbering goes beyond 16). Idle levels: mcu_int1 = 1, mcu_int2 = 1.
- **Pulling `ap_int` (gpd1-5) low makes `mcu_int2` (gpa2-5) go low** in about 20 ms, so the MCU is alive and watches the line (this only worked when the MCU had been woken by a first pulse; right after a fresh boot of the phone it "never went low" in the first run).
- After sending the 9-byte WHOAMI header and raising `ap_int`, `mcu_int2` goes high again as in the vendor sequence, but `mcu_int1` never falls: no answer within 1 s, and no line change in 4 s afterwards (modes 3 and the others, 1 and 8 MHz).
- **The MCU's own messages do reach us:** once `mcu_int1` was low and a 4-byte read gave `03 00 01 00` = options 3 (AP2HUB_READY) with length 1, a valid header (the 1-byte payload was not read, which then
  desynchronised the next reads). After a reset pulse (`reset`: reset low 2 ms, 500 ms or 3 s wait) `mcu_int1` goes low and the first 4 bytes read `00 00 00 c0`, which is the same message shifted by two bits (3 << 30), so the very first
  read after the MCU starts is misaligned (or is something else, e.g. bootloader traffic).
- Unresolved: whether the header reaches the MCU at all (MOSI/SCK mapping, PIO and the controller's byte swap mode, chip select timing), whether the MCU runs the application or a bootloader (the vendor driver checks this before WHOAMI, and flashes
  the firmware when the version differs), and whether it needs a different start-up (e.g. AP2HUB_READY handling, time sync). Next experiments: scope-free checks of MOSI with a loopback of the SPI controller, reading the bootloader state (STM32 system bootloader answers 0x79 to 0x7f on UART/SPI?), and the vendor `check_fwbl`/`ssp_firmware.c` logic (allowed to read).
The probe tool changes (drain, pulse mode, env overrides, `/dev/spidev2.0`, chip scan up to 64) are in `mainline/tools/sensorhub-probe.c`.

## 75. Sensor hub: vendor SPI modes found, still no clean data (2026-11-02)

Read from the vendor `ssp_firmware.c` / `ssp_dev.c` (allowed, read-only): `check_fwbl` is not a separate bootloader test, it just asks the firmware revision with the normal protocol (cmd 0xf0) and compares it with the expected 18011800 (invalid values
99999 / 0xffffff mean "download needed"). The SPI settings differ by phase: **application = SPI mode 1, 4.8 MHz** (`NORM_SPI_HZ`; the DT's cpol+cpha = mode 3 is overridden by the driver), **STM32 system bootloader = mode 0, 4.8 MHz**, entered with boot0 high, reset low ~1 ms,
reset high, boot0 low again after 1 ms, 30 ms wait, then sync byte 0x5a and polling with 0x00 until 0x79 (ACK) or 0x1f (NACK).
- `sensorhub-probe` now defaults to mode 1 / 4.8 MHz and has a `boot` mode for the bootloader handshake (non-destructive, then restarts the application). Results on m13b:
  - mode 1, from a clean reset: the first header reads `01 00 00 80`, WHOAMI "answers" `2a aa aa aa ...`, the scan `3b 20 a7 f9`, then `mcu_int2` stops going low: junk patterns, no protocol.
  - bootloader handshake: `d2` once, then `3c 3c 3c ...` for 255 polls, never 0x79/0x1f.
  - The pinctrl debugfs shows the pins claimed by the SPI function (gpc4-2/gpc5-1 `spi2-bus-pins`, gpc5-0 `spi2-miso-pins`, gpc4-3 `spi2-cs-pins`); no SPI timeouts in dmesg, so the controller shifts bytes.
- So far: the MCU reacts to `ap_int` (mcu_int2 follows it, so the application is running and powered) and sent one plausible AP2HUB_READY header once, but every other read looks like a floating or mis-sampled MISO line. Suspects, in this order: MOSI/SCK vs MISO pad configuration
  or pull (the vendor MISO pad has pull-down in sleep, pud 0 active), the SPI controller's data sampling (feedback clock delay, the `swap-mode` and DMA mode of the vendor DT, `spi-src-clk`), the chip-select timing (the vendor driver uses a GPIO CS with the controller in manual mode),
  a missing MCU supply or pull-up that S-Boot does not set, and a protocol step I have not seen (time sync / reset notice). Needs a logic analyser or the MCU's reaction to a known pattern to go further; blind probing is slow.

## 76. Packaged rootfs written to SYSTEM from TWRP (2026-11-03), NOT yet booted

With Jimmy's OK (phone in TWRP, battery 99 %, SYSTEM not mounted): `build/pmos/samsung-a5y17lte-root.img` (1781530624 B, sha256 `b9a0598e…4b355`, device package 3-r11, kernel package 7-r5, EVIDENCE 68/69)
streamed with `adb exec-in "dd of=<by-name>/SYSTEM bs=4M conv=fsync"` (8 min 25 s), then the first 1781530624 B pulled back with `adb exec-out dd` (slower than the write, over 10 minutes).
- **Pull-back sha256 `f0357b79…e14d4a0` differs from the image's**, but the difference is confined to the last 240 blocks (4 KiB blocks 434704 to 434943, 918261 differing bytes): everything before byte 1780547584 is identical (`cmp -n`), the image's last 240 blocks are all zero
  and `debugfs testb` says all 240 are "not in use" in the image, so what the partition holds there is stale data from the previous SYSTEM contents (the zero tail was not written over it, as already seen in EVIDENCE 33). `e2fsck -fn` on the pull-back is clean (22932 files, 370063/434944 blocks, same as the image).
  So the write is good for everything the filesystem uses.
- Not done yet (Jimmy wants to watch the first boot): reboot into the new System, grow the filesystem to the full partition with `ext4-grow` (EVIDENCE 25), reboot test, enter the Wi-Fi password in Phosh again and re-pair the A55. BOOT is m13b (sensor-hub SPI test image; the packaged rootfs has the same 11 modules). The phone was left in TWRP.
- Fallbacks if the new System does not come up: write `~/a5-backup/SYSTEM-p19-pmos-m5c.img` back the same way (console-only M5c rootfs), or rebuild with `./build-pmos.sh`.

## 77. Packaged rootfs booted, grown and reboot-tested (2026-11-07)

The rootfs of EVIDENCE 76 booted on m13b (BOOT unchanged) with Jimmy watching: Nura edge, kernel 7.2.0-a5y17lte, `/` 1.6G at 91 % at first (the image is 1.78 GB). The SSH host keys are new, so the PC's stale `known_hosts` entries for the phone's two addresses were removed.
- `ext4-grow / 1241088` (static binary pushed to /tmp, RAM only): "now 411846 blocks, growing to 1241088", `resizing filesystem from 434944 to 1241088 blocks`, `df`: 4.6G, 1.3G used, 3.0G free (31 %).
- Checks before the reboot: services `a5y17lte-usbnet`, `a5y17lte-btfw`, `a5y17lte-keyled`, `bluetooth`, `greetd`, `sshd` started; hci0 set up by the btfw service (`QCA setup on UART is completed` at 22 s); `/dev/rfkill` lists phy0 and hci0, both unblocked; the packaged
  `cfg80211.ko`/`mac80211.ko` (not byte-identical to the boot-image build, EVIDENCE 69) load and Wi-Fi works; Phosh running, backlight `exynos7880-decon` present.
- **`sudo reboot` test passed:** the phone came back, `/` still 4.6G, Wi-Fi reconnected by itself, `bluetooth` and `a5y17lte-btfw` started on their own; the first Bluetooth setup at 0.77 s still fails for lack of firmware (the rootfs is not mounted yet) and the service redoes it at ~21.7 s (completed).
- The A55 pairing is gone, as expected (new SYSTEM): re-pairing is still to do. The packaged rootfs is now the base on SYSTEM; the earlier hand-installed extras (python tools, brightness helper files, the cfg80211 swap) are not on it unless they are in the packages.

## 78. Audio: survey (2026-11-07, investigation only, no code, nothing flashed)

From the vendor DT reference (`upstream/k7880`, `exynos7880-a5y17lte_common.dtsi`) and our tree:
- **Codec:** `cod3026x@04` (`codec,cod3026x`, 10-bit I2C address 0x04) on the same HS-I2C bus as the PMIC S2MPU05 (0x66; on this board that is the bit-banged bus gpm0-0/1 we already use), supply `ldo25_reg`, interrupt gpa0-4 (`cod3026_irq`),
  headset detection through the SoC ADC channel 2 (`io-channels = <&exynos_adc 2>`, button zones: send/end 226 (range 0-217), google 582, volume up 115, volume down 114), mic bias settings, water-detect thresholds.
- **SoC side:** `i2s0` at 0x148a0000 (primary/secondary, codec), `i2s1` at 0x148b0000 ("AMP for AP"), `s1402x@14880000` (the audio subsystem block with the pin states for the codec/PMIC bus `i2s_pmic_bus` gpz1-0..4, the amp bus `i2s_amp_bus` gpz1-5, BT and FM buses),
  "dummy" DAIs for the CP (modem), BT and FM paths, and `eax` (Samsung's offload engine); the sound card is Samsung's `samsung,universal7880-cod3026x` machine driver. The DT shows no separate speaker-amp chip, so the speaker driver is probably inside the codec (to confirm).
- **Mainline:** `sound/soc/samsung/i2s.c` already handles `samsung,exynos7-i2s` and `samsung,exynos7-i2s1`; there is no `cod3026x` codec driver, no 7870/7880 sound machine/audio-subsystem driver, and our kernel has `CONFIG_SND` off (the phone has no `/proc/asound`). The audio clocks come from CMU_AUD (not described yet, the same
  fixed-clock approach as for the UART and SPI would do for a first test, rates from the vendor pwrcal in `upstream/k7880`).
- **What it needs:** (1) an I2C probe of the codec at 10-bit address 0x04 on the gpm0 bus to see whether it answers and what its ID register says (read-only), (2) its register map and the speaker/headphone/mic paths (only the vendor codec driver documents them),
  (3) the i2s/audio-subsystem bring-up (clocks, `s1402x`, DMA), (4) a DT sound card (simple-audio-card or a small machine driver) and ALSA mixer paths for PipeWire.
- **Blocked on a permission:** reading the vendor audio sources (Timeshift copy: `sound/soc/codecs/cod3026x*`, `sound/soc/samsung/` machine driver and the `s1402x`/audio-subsystem pieces) is not covered by what Jimmy allowed so far.
- **Meanwhile, no codec needed:** the phone's Bluetooth (EVIDENCE 64-77) can carry audio to a Bluetooth speaker or headset (A2DP) through PipeWire/WirePlumber, which are installed (`pipewire`, `wireplumber`, `pulseaudio-bluez`); needs a speaker to test with.

## 79. Bluetooth audio (A2DP) with the JBL T3: connected, but PipeWire creates no sink yet (2026-11-07)

Jimmy's JBL T3 (headset/speaker) is paired and connected at the BlueZ level (UUIDs: A2DP sink, AVRCP, HFP/HSP); BlueZ shows a `MediaTransport1` for it (A2DP is negotiated).
- **Missing packages on the packaged rootfs** (EVIDENCE 76/77): `pipewire-spa-bluez` (no `/usr/lib/spa-0.2/bluez5`) and `pipewire-pulse`. Installed on the phone with `apk add pipewire-spa-bluez pipewire-pulse` (a SYSTEM write, with Jimmy's OK).
  That left the family mixed (1.6.11 daemon/plugins against `pipewire-libs` 1.6.9: `pipewire --version`: "compiled with 1.6.11, linked with 1.6.9"), so `apk upgrade pipewire-libs pipewire-spa-libcamera pipewire-webrtc` (also with Jimmy's OK) brought everything to 1.6.11
  (libcamera 99990.8.0 pulled in). Both packages have to go into the device package's `depends` (not done yet).
- **WirePlumber without logind:** `support.logind` is missing from this WirePlumber build (`Failed to locate module libwireplumber-module-logind`), so `monitor.bluez.seat-monitoring` was skipped. A fragment `wireplumber.profiles = { main = { support.logind = disabled
  monitor.bluez.seat-monitoring = disabled } }` removes the skip (tested from RAM only: a copy of `/usr/share/wireplumber` in `/tmp/wpconf` plus the fragment, WirePlumber started by hand with `WIREPLUMBER_CONFIG_DIR`; PipeWire, WirePlumber and `pipewire-pulse` were started by hand from ssh with the session's D-Bus address).
- **Where it stops:** the BlueZ SPA plugin loads all codecs, registers the A2DP endpoints and creates its two device objects (the A55 and the JBL), but each logs `set_initial_profile: initial profile off` and `remove nodes`; no node appears, `wpctl status` has no audio device,
  `pactl list short cards/sinks` is empty. Other messages: `socket(SEQPACKET, SCO): Protocol not supported` (the kernel has no SCO, only needed for HFP call audio) and `RegisterProfile() failed: org.bluez.Error.NotPermitted` (HFP/HSP native backend), RTKit unavailable (harmless).
- **Open:** why no profile gets selected (a WirePlumber policy/profile hook not running, or the device never exported to the PipeWire registry); `pw-cli`/`pw-dump` are not installed (`pipewire-tools`), which would show the objects. Next tries: install `pipewire-tools`, set the profile by hand with `wpctl set-profile`, look at the `device`/`policy` components in WirePlumber's debug log.

## 80. Audio: what the vendor sources say (2026-11-07, read-only, nothing copied, no code)

Jimmy allowed reading the vendor audio sources (the 2026-09-05 Timeshift snapshot has been rotated away; the same tree was read from the 2026-11-05 snapshot of the Timeshift copy, never from the live `~/hadk`). Files: `sound/soc/codecs/cod3026x.[ch]`,
`exynos-audmixer.c` / `-regs.h`, `sound/soc/samsung/universal7880-cod3026.c`, `lpass-exynos7880.c`, `lpass.[ch]`. My notes:
- **Signal path (playback):** memory -> DMA -> I2S0 (0x148a0000, the SoC's I2S block) -> **audio mixer "s1402x" (`exynos-audmixer`, 0x14880000, a plain MMIO block, 32-bit registers on a stride of 4: 0x00 soft reset, 0x01-0x09 input controls for three
  inputs (AP I2S, CP/modem, BT/FM), 0x0a-0x0f HQ/slot/digital enable, 0x10-0x11 digital mixers, 0x16-0x17 output muxes, 0x18-0x1a amp controls, 0x68-0x72 ALC)** -> the codec's I2S port. The mixer sits between the SoC I2S and the codec, so even plain playback has to configure it
  (the machine driver calls its DAI "set_bclk_ratio" and master/slave format). The "i2s1 / AMP for AP" link goes through the same mixer.
- **Machine driver (`universal7880-cod3026`):** the codec is the I2S **slave** (`CBS_CFS`, standard I2S format, normal bit/frame clock); the SoC I2S block generates the codec's master clock (`SAMSUNG_I2S_CDCLK` as an output, rfs = 512 x fs at 48 kHz, 128 x fs at 192 kHz),
  bit clock ratio 32 x fs (64 x at 192 kHz), clock root selection `RCLKSRC_1`. Several links: AP primary/secondary, voice call (CP), BT, FM, AMP.
- **Audio subsystem ("LPASS", `lpass-exynos7880`):** sits in the DISPAUD clock block (the same one that has the DECON clock `dispaud_decon`): `fout_aud_pll` set to **98.304 MHz**, `dout_sclk_mi2s` set to ~110 MHz, plus gates; software reset bits (in the DISPAUD config register) for the I2S, mixer, AMP and DMA blocks that are toggled
  after the block is powered; the PMU pad-retention for the audio pads is released (`GPIO_MODE_AUD`/`PAD_RETENTION_AUD`) and `AUD_PATH_CFG` selects BT or FM on the shared mux. Runtime PM turns the whole block on and off; an offload path uses the internal audio SRAM and a CA5
  core ("Seiren"), which is optional and not needed for plain DMA playback.
- **Codec (`cod3026x`, regmap I2C 8-bit registers, 8-bit values, 10-bit slave address 0x04):** registers: 0x01-0x0d IRQ pending/mask/status, 0x10-0x1c power-down and reference (PD_REF, PD_AD1/2, PD_DA1/2/3, auto power, SV_HP, ZCD),
  0x20-0x26 ADC volume/mix, 0x30-0x38 analogue output: headphone volumes L/R (0x30/0x31), earpiece/speaker volume (0x32), controls EP/HP/SPK (0x33-0x35), DA mixers (0x36/0x37), 0x40-0x46 digital power, I2S format (0x41), ADC and DMIC, 0x50-0x59 DAC, DAC volumes (0x51/0x52), DNC.
  There is **no chip-ID register**; the driver trims analogue offsets from an OTP block of the chip (it reads the OTP area at start and rewrites the offsets to `0xd4`/`0xd5` etc. before enabling an output). Speaker sequence (`spkdrv_ev`): write `0xdd` = 0x82, mute the speaker mixer, set the
  speaker PGA gain to zero, enable auto power for the speaker (`0x17`), enable the speaker mixer, wait 135 ms, then restore the gain; headphone and earpiece have similar sequences with 6-180 ms waits. Supply is `vdd` = the PMIC's LDO25; interrupt on gpa0-4 for jack detection; headset detection by ADC (SoC ADC channel 2).
  The DT shows no separate speaker-amplifier chip, so the **speaker is driven by the codec** itself (class-D "SPK").
- **What a mainline bring-up needs, in order:** (1) enable LDO25 (S2MPU05 node on our PMIC bus) and find the codec on the I2C bus at 10-bit address 0x04 (read-only check); (2) DT nodes + `CONFIG_SND_SOC`: the SoC I2S (mainline's `samsung,exynos7-i2s` driver, with its own differences from Samsung's
  `i2s.c`), PDMA (the DMA controller is not in our DT yet), fixed clocks for the audio PLL/MI2S clock (98.304 MHz / 110 MHz, DISPAUD gates are on or have to be opened) and the reset/pad-retention handling; (3) a new small driver for the audio mixer (regmap-mmio, a DAI with the slot/rate controls); (4) a new `cod3026x` codec driver
  (speaker/headphone/earpiece playback first, jack and mic later); (5) a DT sound card or a small machine driver and ALSA UCM/mixer paths for PipeWire. Each step can be tested on its own; (1) can start now.

## 81. Audio: the COD3026x codec answers on the PMIC bus (2026-11-07, read-only)

`mainline/tools/i2c10-read.c` (static aarch64, `/dev/i2c-N` with `I2C_RDWR`, write register + repeated start + read, never writes; binary in the phone's /tmp only) on m13b with the packaged rootfs, nothing flashed:
- **The vendor DT's "ten-bit-address" does not mean real 10-bit addressing.** With a real 10-bit address 0x04 (`I2C_M_TEN`) every read returns ENXIO (no ACK) although the adapter supports 10-bit; with a **plain 7-bit address 0x04** (`I2C_SLAVE_FORCE`, a reserved address the kernel only accepts when forced)
  on `i2c-1` (the bit-banged PMIC bus gpm0-0/1, the same bus as the S2MPU05 at 0x66, which carries the same flag in the vendor DT and works with 7-bit 0x66) the codec answers. LDO25 (`vdd_ldo25`, always-on/boot-on in the vendor DT) is evidently on already (S-Boot), so no regulator work is needed for this probe.
- Register snapshot after boot (hex register=value, nothing playing): 06=80 07=c0 08=c0 09=ff 0a=ff | 0b..0d (status)=00 | 10..17 (power-down/auto power)=00 18=50 19=00 1a=30 1b=4b 1c=04 | 20=54 21=54 22=54 23=00 24=00 25=0a 26=05 | 30=04 31=04 32=03 33=38 34=70 35=41 36=00 37=00 38=05 |
  40=59 41=a8 42=0c 43=18 44=18 45=00 46=14 | 50=00 51=18 52=18 53=00 54=01 55=18 56=66 57=8b 58=04 59=08 | 76=22. The values are plausible (power-down registers all 0, volume registers at their defaults, I2S format register 0x41 = 0xa8) and stable between reads, so this is the real chip. The IRQ pending registers 0x01-0x05 were not read.
- So the DT for the codec node needs `reg = <0x04>` with plain 7-bit addressing (the kernel refuses to bind a client at a reserved address through DT/i2c core: `i2c_check_addr`/`i2c_check_7bit_addr_validity_strict` blocks 0x00-0x07 for `i2c_new_client_device`, so the driver may have to be bound through a platform/regmap path or the PMIC bus
  registered with `I2C_CLIENT_...`; to be checked when the driver is written).

## 82. Audio subsystem: clocks and state S-Boot leaves (2026-11-07, read-only on the phone, nothing flashed)

Registers from the vendor pwrcal tables (`upstream/k7880`, `S5E7880-cmusfr.h`; CMU_DISPAUD at 0x148d0000, PMU at 0x106b0000, audio SFR/"lpass" region = SYSREG_DISPAUD at 0x148f0000), read with the mmap-based reader (single reads; some SYSREG offsets such as 0x148f0200 and 0x148f1004 are not implemented and kill the reader with SIGBUS):
- **AUD_PLL is already running at 98.304 MHz:** `AUD_PLL_CON0` (+0x1c0) = 0xa01e0103 (enable, locked; M = 30, P = 1, S = 3), `CON1` (+0x1c4) = 0x3f55 (K = 16213): (30 + 16213/65536) x 26 MHz / (1 x 2^3) = 98.304 MHz, exactly the vendor's 48 kHz x 2048. `MUX_AUD_PLL` (+0x204) = 0x00201000.
- **MI2S source clock:** `MUX_CLK_DISPAUD_MI2S` (+0x234) = 0x00200000 (select bit 12 = 0 = AUD_PLL, the alternative is an external AUDIOCDCLK0 pad), `DIV_CLK_DISPAUD_MI2S` (+0x40c) = 0 (divide by 1), so the I2S root clock is 98.304 MHz (the vendor driver re-programs it to ~110 MHz when it opens a stream; 98.304 MHz is the right base for 48 kHz audio).
  Gates are open: `ENABLE_CLK_DISPAUD_MI2S` = 3, `..MIXER` = 1, mixer clocks AP/BT/CP/FM (+0x834..0x840) = 1, BUS 0xd, APB 0xf (the display needs this block, so S-Boot leaves it on). `DIV_CLK_DISPAUD_APB` = 1, `DIV_..MIXER` = 1.
- **Pad retention:** PMU `GPIO_MODE_AUD_SYS_PWR_REG` (0x1340) = 0, `PAD_RETENTION_AUD_OPTION` (0x3028) = 0: the vendor driver sets bit 28 of 0x3028 and bit 0 of 0x1340 when it powers the block, so the I2S/audio pads are probably still held (a write is needed before I2S pins work; to test with the existing PMU poke tool).
- **Reset register:** `DISPAUD_CFG` = SFR region +0x1000 = 0 (bits: 0 mixer, 1 MI2S, 2 AMP, 3 ADMA software reset; the vendor driver sets/clears them to reset the blocks after power-on).
- **DMA for audio is not the main PDMA:** the I2S0 DMA channels belong to `adma@14890000`, an **ARM PL330** (`arm,pl330`, 4 channels, 16 requests, IRQ 73, microcode kept in the internal audio SRAM at 0x03002000, reset bit 3 of DISPAUD_CFG), which mainline's `dma-pl330` driver supports in principle.
  The vendor `i2s0` node (`samsung,i2s-v5`, 0x148a0000, SPI 69, dmas adma 0 tx / adma 2 rx, clocks "iis", "i2s_opclk0/1" = the MI2S gate and the divided I2S clock, internal "secondary" DAI `i2s-sec` on adma 1, RX SRAM 0x03000000 + 0x2000) differs from mainline's `samsung,exynos7-i2s`; a port of the register layout is expected.
- **Next steps (all small):** (1) release the PMU audio pad retention (a PMU write; reversible by reboot) and see whether the I2S pins respond; (2) DT nodes for `adma` (PL330), `i2s0`, the mixer and fixed clocks (98.304 MHz) in a test image, check that the PL330 and I2S probe; (3) write the mixer and codec drivers.

## 83. Audio pad retention write (2026-11-07, one PMU write, reversible by reboot)

`mainline/tools/pmu-aud-pad.c` (purpose-limited: can only read, or set bit 28 of PMU `PAD_RETENTION_AUD_OPTION` (0x106b3028) and bit 0 of `GPIO_MODE_AUD_SYS_PWR_REG` (0x106b1340), the two writes of the vendor `lpass_release_pad_reg`; the existing `pmu-poke` only accepts the INFORM scratch block, so it could not do this), run on the phone from /tmp with Jimmy's OK:
- before: 0x1340 = 0, 0x3028 = 0. After `release` (and again 2 s later): **0x1340 = 1 (stuck), 0x3028 = 0 (bit 28 did not stick)**. The system kept running normally (uptime 54 min, no errors from the write). dmesg only shows the `i2c-1: died at extended address code` lines from the earlier 10-bit probe attempts of EVIDENCE 81.
- Reading: bit 28 of 0x3028 behaves like a status/read-only or a write-1-when-powered bit (the vendor driver writes it only after the audio power domain `pd_dispaud` and the clocks are up), or the audio pad group is not held in the first place. The effect on the I2S pins cannot be seen from here (the audio pads gpz1-0..5 are not in our pinctrl DT, so no way to read their mux or level) — to be settled with a real I2S probe and a signal to look at, or with the pin DT.
- The 0x1340 bit stays set until the next reboot; nothing else was changed.

## 84. Audio DMA (ADMA PL330): reading it while its reset bit is set hangs the phone (2026-11-07)

Before building m14a I checked that the new audio blocks can be read without a bus fault (`/dev/mem`, single reads): the PL330 ID registers (0x14890fe0-0xffc) read 0, I2S0 (0x148a0000/04) read 0, the mixer (0x14880000/04) read 0x3 / 0xc. The PL330 reading zeros is normal for this implementation: the vendor DT forces
`arm,primecell-periphid = <0x341330>`, so the ID registers are not wired. Then I ran the vendor-style reset toggle on the ADMA by hand (set bit 3 of `DISPAUD_CFG`, 0x148f1000, read the PL330 ID registers, clear it): **the phone froze** (no ping, no SSH) as soon as the bit was set and the ADMA was read.
- Meaning: DISPAUD_CFG bit 3 = 1 is the ADMA in reset, and an access to an AXI/APB slave that is in reset does not fault, it hangs the bus. Bit 3 = 0 (the state S-Boot leaves) is out of reset, which is why the earlier reads worked.
- My mistake: reading the block inside the reset window. Rule from now on: never touch the bit (or read an audio block) while a reset is asserted; the vendor driver toggles the reset without reading in between.
- Recovery: force power off (Volume Down + Power), power on; nothing was written to eMMC by this (the ext4 journal covers the unclean shutdown). The `pmu-aud-pad` write of EVIDENCE 83 is also gone after the reboot.
- For the m14a DT this means: the PL330 node with the vendor's `arm,primecell-periphid` override is safe to probe in the S-Boot state (reset released, bit 3 = 0).
- **Aftermath (Jimmy):** Vol Down + Power went into download mode, Vol Down there rebooted but the phone hung at the Samsung logo; forced into TWRP and Reboot -> System brought it back. The next boot: ext4 `recovery complete`, 35 orphan inodes deleted, battery 66 % (3.98 V), Wi-Fi, USB and Bluetooth (hci0 setup completed) all fine. Same logo-hang pattern as EVIDENCE 58/60 after an unclean stop.

## 85. m14a built: audio DMA, I2S0 and audio pins in the DT (2026-11-07), NOT flashed, untested

`DECON=1 MILESTONE=m14a ./build-mainline.sh`, image `out/a5y17lte-mainline-m14a-boot.img`, 16429072 B, sha256 `7476f2e0…678766`. Changes on top of m13b:
- `exynos7880.dtsi`: alias `pinctrl2`, `pinctrl_dispaud` (pinctrl@148c0000, SPI 68; our pin driver patch 0002 already has the gpz0-gpz2 banks as controller 2), fixed clocks `clk_dispaud_apb` and `clk_dispaud_mi2s` (98.304 MHz, measured, EVIDENCE 82), `adma` (the audio PL330 at 0x14890000, SPI 73, 4 channels / 16 requests, `arm,primecell-periphid = <0x341330>`
  because its ID registers read 0), `i2s0` (0x148a0000, SPI 69, dmas adma 0 tx / adma 2 rx, `samsung,exynos7-i2s` as the nearest mainline match, disabled by default). `exynos7880-pinctrl.dtsi`: the gpz0/gpz1/gpz2 banks. `exynos7880-a5y17lte.dts`: pin groups `i2s_codec_bus` (gpz1-0..4, function 2, pull-down = vendor pud 1)
  and `i2s_amp_bus` (gpz1-5, function 2, pull-up = vendor pud 3, drive 2), and `&i2s0` enabled with them. (Samsung pud numbering: 0 none, 1 pull-down, 3 pull-up; my first build had pull-up/none swapped and was rebuilt before it was ever flashed.)
- Config (built in): `SOUND`, `SND`, `SND_SOC`, `SND_SOC_SAMSUNG`, `SND_SAMSUNG_I2S` (the symbol is not `SND_SOC_SAMSUNG_I2S`; the first build silently lacked it), `DMADEVICES`, `ARM_AMBA`, `PL330_DMA`. The module set the rootfs needs should not change (everything is built in), but the packaged modules are not byte-identical to this build anyway (EVIDENCE 69).
- Expected on the phone: `dma-pl330 14890000.dma-controller` and `samsung-i2s 148a0000.i2s` probe messages, a `/proc/asound` (empty: no card) and no hang. Risks: the I2S driver writes to a register layout it may not match, the pad retention (EVIDENCE 83) may block the I2S pins (not visible from here), the PL330 microcode is allocated from DRAM instead of the audio SRAM. Fallback: m13b.

## 86. m14a on the phone: the audio PL330 and I2S probe, but the DMA reports no peripherals (2026-11-07)

m14a (sha256 `7476f2e0…678766`) flashed from TWRP and read back, booted normally on the packaged rootfs (BOOT m13b -> m14a; no hang, no logo stall; Wi-Fi and USB up):
- `dma-pl330 14890000.dma-controller: Loaded driver for PL330 DMAC-341330` (the periph-id override works) with `DBUFF-1x1bytes Num_Chans-1 Num_Peri-0 Num_Events-1`; `/sys/class/dma/dma0chan0` is the only channel. The vendor DT says 4 channels and 16 requests, which the vendor kernel forces from `#dma-channels`/`#dma-requests`
  (the vendor pl330 driver is patched); the config registers (CR0/CRD at +0xe00/+0xe14) read 0 here like the ID registers, so mainline derives 1 channel and 0 peripherals. **Mainline's `pl330` refuses slave requests when the peripheral id is >= `num_peri`** (`drivers/dma/pl330.c`, `pl330_prep_*` check on `desc->peri`), so I2S playback through it needs a small patch:
  take the number of channels, requests and events from the DT when the config registers are zero (a quirk, e.g. a new compatible or the existing `#dma-channels`/`#dma-requests` properties). The microcode buffer comes from DRAM; the vendor puts it in the audio SRAM at 0x03002000, which should not be needed.
- `samsung-i2s 148a0000.i2s-sec: DMA channels sourced from device 148a0000.i2s`: the mainline I2S driver binds both the primary and the secondary DAI (platform devices `148a0000.i2s` and `148a0000.i2s-sec`); `/proc/asound/cards` says "no soundcards" (no card defined yet). The `148c0000.pinctrl` (DISPAUD pin controller) registered with the gpz banks.
- Not yet checked: that the mainline I2S driver's register layout matches this block (it only probed; no stream was started), whether the pads are still held by retention (EVIDENCE 83), and the DMA behaviour with 4 channels. The I2S pins show as claimed by the I2S node in pinctrl debugfs (to verify).

## 87. m14b built: PL330 patched to take its channel/request counts from the DT (2026-11-07), NOT flashed, untested

- Kernel patches (branch a5y17lte, exported with `git format-patch --start-number 17`; checkpatch: only the expected "Missing Signed-off-by" error; `Assisted-by: Claude:claude-sonnet-5-5`, no AI Signed-off-by):
  `0017-dt-bindings-dma-arm-pl330-allow-dma-channels-and-dma.patch` (adds `dma-channels` 1-8 and `dma-requests` 1-32 to the arm,pl330 binding) and `0018-dmaengine-pl330-take-the-channel-and-request-counts-.patch` (in `read_dmac_config()`, after the
  registers are read: if the DT gives `dma-channels` / `dma-requests` they override `num_chan` / `num_peri`, and `num_events` is raised to at least `num_chan`, since every channel uses the event of its own number).
- `exynos7880.dtsi`: the `adma` node now uses the standard `dma-channels = <4>` / `dma-requests = <16>` instead of the vendor's `#dma-channels` / `#dma-requests`. Kernel package `linux-postmarketos-exynos7880` 7-r6 lists the two patches (`build-pmos.sh` refuses to run with a patch missing from the APKBUILD).
- Image `out/a5y17lte-mainline-m14b-boot.img`, 16429072 B, sha256 `18cbf64f…1f1977` (`DECON=1 MILESTONE=m14b ./build-mainline.sh`).
- Expected on the phone: `dma-pl330 …: DBUFF-1x1bytes Num_Chans-4 Num_Peri-16 Num_Events-4` and four DMA channels in `/sys/class/dma`. Not expected to move audio yet (no card, no mixer/codec driver; the microcode still comes from DRAM, the vendor uses the audio SRAM at 0x03002000; the controller may also be secure-only, CR0 bit 2 reads 0).

## 88. m14b on the phone: the audio DMA now has 4 channels and 16 request lines (2026-11-07)

m14b (sha256 `18cbf64f…1f1977`) flashed from TWRP, read back, booted normally: `dma-pl330 14890000.dma-controller: Loaded driver for PL330 DMAC-341330`, `DBUFF-1x1bytes Num_Chans-4 Num_Peri-16 Num_Events-4` (was 1 / 0 / 1), `/sys/class/dma` lists `dma0chan0` ... `dma0chan15`
(one per request line, as the driver exposes peripherals as channels), `samsung-i2s 148a0000.i2s-sec: DMA channels sourced from device 148a0000.i2s`, `/proc/asound/cards`: no soundcards. No hang, no errors. Patches 0017/0018 work for the counts; an actual transfer has not been run yet (needs a sound card or a test client),
so the secure-mode and microcode-in-DRAM questions of EVIDENCE 87 are still open.

## 89. First playback test prepared: kernel sound card on I2S0 plus userspace mixer/codec setup (2026-11-07), m14c built, NOT flashed

Plan to prove the whole audio chain before writing drivers: the kernel streams audio (PL330 DMA -> I2S0) into a sound card with a stand-in codec, while the real mixer (S1402X, 0x14880000) and the COD3026x (7-bit 0x04 on `i2c-1`) are configured from userspace with the values of the vendor drivers.
- **m14c** (`DECON=1 MILESTONE=m14c ./build-mainline.sh`, `out/a5y17lte-mainline-m14c-boot.img`, 16431120 B, sha256 `c9fb925b…b5de3`): on top of m14b, `simple-audio-card` "A5-Test" (I2S format, I2S0 as bit/frame clock master, mclk-fs 512) with the S/PDIF transmitter `linux,spdif-dit` as the dummy codec DAI (the simple card cannot take a bare dummy codec through the DT),
  config `SND_SIMPLE_CARD`, `SND_SOC_SPDIF`.
- **Mixer values** (computed with a throwaway program that only includes the vendor register header read-only; nothing copied): reset defaults of the S1402X match what the phone reads (reg 0x00 = 3, 0x04 = 0x0c). The vendor init writes IN1_CTL2 (0x02) = 0x00, IN2_CTL2 (0x05) = 0x00, IN3_CTL1 (0x07) = 0x04, IN3_CTL2 (0x08) = 0x00, IN3_CTL3 (0x09) = 0x24,
  SLOT_L (0x0b) = 0x01, SLOT_R (0x0c) = 0x02, TSLOT (0x0e) = 0x01, INAMP_CTL (0x18) = 0x2a, OUTAP1_CTL (0x19) = 0x00, OUTCP1_CTL (0x1a) = 0x00, HQ_CTL (0x0a) = 0x04 (MCKO enable), DIG_EN (0x0f) = 0x08 (mixer enable), then a toggle of the data-reset bit (reg 0x00 bit 1). Registers are 32-bit on a stride of 4.
  The mixer system clock is 24.576 MHz (+100 Hz) for 48 kHz in the vendor driver (49.152 MHz for 192 kHz): CMU_DISPAUD `DIV_CLK_DISPAUD_MIXER` (0x148d0410) reads 1 (49.152 MHz); the test sets it to 3.
- **Codec speaker sequence** (from `dac_ev`, the SPK switch, `spkdrv_ev`): `0x40` set bit 5 (DAC digital power), pulse bit 4 (data reset); `0x76` set bit 2 (speaker chop); write `0xdd` = 0x82; clear speaker mixer bits 3:2 of `0x37`; save and zero the speaker gain (`0x32` bits 3:0); set `0x17` bits 4 and 2 (auto power, speaker); set `0x37` bits 3:2; wait 135 ms; restore the gain.
  The OTP offset trim the vendor driver applies first is skipped (a pop is possible). After boot `0x41` already reads 0xa8 (I2S format, 24-bit words), `0x32` = 0x03, `0x35` = 0x41. Power-down is the reverse (gain to 6, clear 0x17 bits, clear 0x37 bits, `0x76` bit 2, `0x40` bits 4 and 5).
- **Tools:** `mainline/tools/codec-seq.c` (static helper: `r/w/u/g/p/ms` steps over `/dev/i2c-N` with a forced 7-bit address), `mainline/tools/audio-test.sh` (`mixer`, `up`, `tone`, `down`; runs as root on the phone from /tmp). The rootfs has no `alsa-utils` any more (the old hand-installed one was lost with the SYSTEM rewrite; only `paplay`), so `speaker-test`/`aplay` need `apk add alsa-utils`.
- **Test plan** (needs Jimmy's OK for the flash and the apk add): flash m14c, boot, check `cat /proc/asound/cards` and `aplay -l` show "A5-Test", run `audio-test.sh mixer`, `up`, `tone` (low volume first), `down`; watch dmesg for I2S/DMA errors. Possible failures: the DMA never completes (microcode in DRAM, secure mode), the I2S clocks differ from what the codec expects (the codec is the I2S slave), a pad retention still holding the pins (EVIDENCE 83), the mixer routing, the speaker amp needs more of the vendor init.

## 90. First playback attempt: the card exists, the codec and mixer take their setup, the audio DMA never starts (2026-11-07)

m14c (sha256 `c9fb925b…b5de3`) flashed from TWRP, read back, booted; `apk add alsa-utils` (with Jimmy's OK, `aplay`, `speaker-test`).
- `/proc/asound/cards`: `0 [A5Test] simple-card - A5-Test`; `aplay -l`: `card 0 ... device 0: samsung-i2s-dit-hifi dit-hifi-0`; PipeWire/WirePlumber create a "Built-in Audio Stereo" sink for it by themselves.
- `audio-test.sh mixer`: the mixer took all the values (readback equals what was written, soft-reset register 0x00 = 3) and the CMU mixer divider write (0x148d0410 = 3) went through, no hang. `audio-test.sh up`: the codec sequence ran without an I2C error (0x40: 0x59 -> 0x79, 0x76: 0x22 -> 0x26, 0x17 -> 0x14, 0x37 -> 0x0c, gain 3 restored).
  So the codec at 7-bit 0x04 accepts writes and the register values read back (nothing audible without a stream).
- **Playback fails:** `speaker-test`/`aplay -D hw:0,0 -f S16_LE -r 48000 -c 2` open the device with the right parameters (buffer 48000 frames, 4 periods) and then die on the first write with `I/O error`; there is no kernel message (no dynamic debug in this kernel). That error is the ALSA wait timeout
  ("DMA or IRQ trouble", about 1.1 s for a 1 s buffer): the stream is triggered (I2SCON changes from 0x8040ce50 to 0x8040c605 while the stream is open) but the hardware pointer never moves.
- With the stream open, **every PL330 register reads 0** (DSR, DPC, INTEN, INT_EVENT_RIS, fault status/type, CSR0/CPC0, SAR0/DAR0/CCR0): no channel ever started, no fault, no event. The I2S FIFO registers (I2SFIC, I2STXD) also stay 0.
- Vendor findings that may matter (read-only, `drivers/amba/bus.c`, `drivers/dma/pl330.c`, `arch/arm64/mm/dma-mapping.c` of the vendor tree): (a) `adma_hw_reset()` in the AMBA bus code pulses the `samsung,reset-reg/-bit` bit (here bit 3 of `DISPAUD_CFG`, `reset-high`: set it, then clear it, with no access in between) before the PL330 is probed:
  "Audio DMA block needs to be reset after system boot up, before we can start using this IP"; (b) the vendor PL330 uses special DMA ops (`arm_exynos_dma_mcode_ops`, microcode buffer at `#dma-mcode-addr` = 0x03002000 in the audio SRAM) and registers a PM notifier (`exynos_pm_register_notifier`).
- Tried from userspace after the failure: the same reset pulse (`DISPAUD_CFG` bit 3 high then low, ~100 ns apart, no reads) on the live system: no hang, register back to 0; **playback still fails the same way** (the driver had initialised the DMAC before the pulse, so this is not conclusive).
- Next: a test image with `CONFIG_DYNAMIC_DEBUG` (enable `pl330.c`, `soc-pcm.c`, `pcm_dmaengine.c`, `i2s.c` logging at runtime) to see where the DMA start stops (submit refused, DBGINST write ignored, microcode fetch from DRAM). Candidate fixes: pulse the reset in the kernel before probing (a `resets`/reset-simple node for DISPAUD_CFG), put the microcode buffer in the audio SRAM at 0x03002000, secure/non-secure manager mode.

## 91. Why the audio DMA was dead: the blocks need a held reset after boot (2026-11-07), fix in m14e (built, NOT flashed)

m14d (sha256 `c253a889…29547c`, `CONFIG_DYNAMIC_DEBUG=y`) flashed and read back. **Side effect:** with dynamic debug on, `struct module` changes size, so every module of the packaged rootfs is refused (`module cfg80211: .gnu.linkonce.this_module section size must match the kernel's built struct module size`): no Wi-Fi (Bluetooth is built in and works), USB networking fine. A debug image like m14d must not stay on the phone; the production images keep `DYNAMIC_DEBUG` off (the module set is built without it).
- Kernel log with dynamic debug: `samsung-i2s 148a0000.i2s: RCLK_SRC=98304000Hz PSR=8, RCLK=256fs, BCLK=32fs` (the root clock divided by 8, i.e. 256 x fs at the codec, not the 512 x fs of `mclk-fs`; to look at later) and then `asoc-simple-card sound: playback write timeout (DMA or IRQ trouble?)`: the stream is triggered, nothing moves.
- **Finding:** the audio PL330 is dead until the audio blocks have been reset with the reset held. On the stuck stream every PL330 register read 0 and `INTEN` (0x20, read/write) ignored a write of 5. The audio SRAM at 0x03000000/0x03002000 is alive (reads data), so the subsystem is powered and clocked. Running the vendor's sequence by hand from userspace
  (bits of `DISPAUD_CFG` at 0x148f1000 in the vendor order mixer 0, amp 2, I2S 1, DMA 3: set, hold ~200 us, clear, no other access in between, 10 ms settle) brought the PL330 to life: `CR0` = 0x00063035 (4 channels, peripheral requests, **boot manager non-secure**), `CRD` = 0x00731312, `INTEN` accepts writes.
  My earlier quick pulse (back-to-back writes, no hold) did nothing. Because the PL330 driver had already probed while everything read 0, it assumed a secure manager and playback still failed after the manual reset.
- **Fix, patches 0019 and 0020:** `dt-bindings: reset: add the Exynos7880 audio subsystem resets` and `reset: add a driver for the Exynos7880 audio subsystem resets` (`reset-exynos7880-lpass.c`, a reset controller for the four bits at 0x148f1000, mixer 0, I2S 1, amp 2, DMA 3, whose `deassert` pulses the bit for 200 us and waits 10 ms; checkpatch: only the expected "Missing Signed-off-by" and MAINTAINERS warning).
  The PL330 driver already deasserts a `dma` reset before it reads the configuration, so the DT node gets `resets = <&lpass_rst 3>; reset-names = "dma";` and a `lpass_rst` reset-controller node at 0x148f1000; config `RESET_CONTROLLER`, `RESET_EXYNOS7880_LPASS`. Kernel package 7-r7 lists the two patches.
- **m14e:** `out/a5y17lte-mainline-m14e-boot.img`, sha256 `b8bb0581…2206ee` (still with `DYNAMIC_DEBUG`, USB only). Expected: the PL330 reports real configuration at probe, the non-secure manager is detected, and a stream gets further (the microcode still comes from DRAM; the mixer and codec setup from userspace as before).

## 92. m14e: the audio DMA runs, no sound yet, and a second freeze (2026-11-07)

m14e (sha256 `b8bb0581…2206ee`, DYNAMIC_DEBUG, USB only) flashed from TWRP and read back; booted: `dma-pl330 14890000.dma-controller: Loaded driver for PL330 DMAC-341330`, `DBUFF-8x4bytes Num_Chans-4 Num_Peri-16 Num_Events-4` (the real configuration now, the probe waits 8 ms for the reset pulse), card `A5-Test` present.
- **Playback no longer times out:** `aplay -D hw:0,0 ... -d 1 /dev/zero` and `speaker-test` ran without a write error. PipeWire then holds the ALSA device open (a later `speaker-test` got `Resource busy`), so the real test is through PipeWire: a 6 s 440 Hz stereo file (S16, 48 kHz) played with `paplay` to the only sink "Built-in Audio Stereo" (vol 1.00) ran to the end, **nothing audible** (Jimmy listened at the speaker, three times, the last one after the pad release below).
- State during a PipeWire stream (registers read from userspace): PL330 `DSR` 0x200, `CSR0` 0x00200007 (channel waiting for the peripheral), `CPC0` 0x403ac01a, `CCR0` 0x00810205, `DAR0` = 0x148a0010 (the I2S TX FIFO register), `SAR0` advancing through the DMA buffer in DRAM (0x5c0a9960 -> 0x5c0b4328 between the two reads,
  ~0.5 s apart), `INTEN` 1, no fault; I2S `CON` 0x8040c305/0x8040cb05, `MOD` 0x02404021, `FIC` 0x3f00 (TX FIFO full), `PSR` 0x8400; `RCLK_SRC=98304000Hz PSR=8, RCLK=256fs, BCLK=32fs` in the kernel log. The audio pins gpz1-0..4 and gpz1-5 are owned by `148a0000.i2s` in pinctrl debugfs (function 2).
  Mixer registers after the userspace setup are as written (0x0f = 0x08 mixer enable, 0x0a = 0x04 MCKO enable, 0x18 = 0x2a). Codec registers during the stream: `0x40` = 0x79, `0x17` = 0x14, `0x37` = 0x0c, `0x32` = 0x03, `0x35` = 0x41, `0x76` = 0x26, `0xdd` = 0x82, `0x0d` = 0x01 (was 0), DAC volumes `0x51`/`0x52` = 0x18, `0x41` = 0xa8.
- **Pad retention released again** in this boot with `pmu-aud-pad release` (0x1340 bit 0 reads 1 afterwards; 0x3028 bit 28 still reads back 0, probably a self-clearing trigger): a further tone through PipeWire, still nothing heard.
- **Second freeze:** right after that, a command that played the tone (`paplay` in the background of the ssh session) and, 1.5 s into the stream, read the PL330 registers `SAR0`/`CSR0` ten times at 0.3 s intervals (the same registers had been read safely twice in the earlier dump) left the phone unreachable (no ping, no SSH, no USB link). Cause unknown: candidates are a register read of the PL330 while it is active at the wrong moment, the end of the stream, or the I2S/mixer state after the pad release. Recovery: force power-off and the TWRP route (EVIDENCE 84, memory note).
- Open (no sound): the actual signal path after the I2S block (mixer routing, whether the I2S emits clocks and data on the pins, the codec master clock supplied through the mixer's MCKO output, the speaker amplifier of the codec) and the 256 x fs root-clock ratio. To check without touching the PL330 during playback.

## 93. m14f built: m14e without DYNAMIC_DEBUG (2026-11-07), NOT flashed

`DECON=1 MILESTONE=m14f ./build-mainline.sh`, `out/a5y17lte-mainline-m14f-boot.img`, 16431120 B, sha256 `92743204…bf28e5`: the audio reset controller (patches 0019/0020), the PL330 with the DT counts (0017/0018), I2S0, the A5-Test card and the pad pins as in m14e, with `CONFIG_DYNAMIC_DEBUG` off again so the packaged modules load (Wi-Fi). The `DYNAMIC_DEBUG` line is removed from `mainline/config/a5y17lte.config`.

## 94. FIRST SOUND: the earpiece plays a tone through mainline Linux (2026-11-07)

m14f (sha256 `92743204…bf28e5`, patches 0017-0020, no DYNAMIC_DEBUG, Wi-Fi back) flashed from TWRP; with the userspace setup of `mainline/tools/audio-test.sh` and a 6 s 440 Hz stereo file played with `paplay` through PipeWire to the card "A5-Test", **Jimmy heard the tone from the earpiece**. The whole chain works:
PipeWire -> ALSA card (simple card on I2S0, stand-in codec) -> PL330 audio DMA (reset by the new reset controller, EVIDENCE 91) -> I2S0 -> audio mixer S1402X (configured from userspace) -> I2S to the COD3026x codec (configured from userspace) -> earpiece driver.
- **What finally made it audible**, after the DMA ran but everything stayed silent: (1) the mixer routing. The vendor driver does not route anything itself: the digital path is switched by ALSA controls that the Android audio HAL sets ("AP0 En", the SRC enables, the DMIX channel enables). For AP playback (I2S0 = mixer input 1): `DIG_EN` (0x0f) = 0x09 (MIX_EN bit 3 + AP0/SRC1 bit 0) and `DMIX1` (0x10) = 0x08 (channel 1 enable, level 0); the output select `DOUTMX1` (0x16) already defaults to DMIX_OUT.
  (2) the output stage: the **earpiece** path of the codec (the same pattern as the speaker one): `0x76` bit 3 (EN_EP_CHOP), `0x17` bits 4 and 0 (auto power + earpiece), `0x37` bit 7 (EN_EP_MIX_DCTL), wait 136 ms. The loudspeaker sequence of EVIDENCE 89 (`0x76` bit 2, `0x17` bit 2, `0x37` bits 3:2 with the gain dance) ran in the same boot; whether the loudspeaker itself is audible has not been separated from the earpiece yet.
- **Measurements that located the problem** (pad data register of `gpz1`, 0x148c0024, left on the I2S function, sampled at 4.7 MHz by `mainline/tools/padfreq.c`): gpz1-0 toggles at ~48 kHz (frame clock, 48 kHz), gpz1-1 at 35 x that (bit clock, 32 x fs), gpz1-4 a fast steady clock (master clock, aliased), gpz1-2 stays low (data from the codec), gpz1-5 (the amp pad) stays high; **gpz1-3 (data to the codec) has 0 transitions at idle and ~176k transitions (24.6 % high) during the tone**, i.e. the mixer outputs the audio data once the routing bits are set. (Setting a pad to GPIO input to look at it was useless: it disconnects the function.)
- Other facts from this round: PL330 registers are only meaningful after the reset sequence (EVIDENCE 91); polling the PL330 during playback froze the phone once (EVIDENCE 92); the vendor codec init values (0x77 charge pump 0x02, 0x87 = 0x0f, 0x42 = 0x0c, 0x5a = 0x1a, 0xdd = 0x82, 0x84 = 0x0d, ...) are already in place after boot; speaker volume `0x32` bits 3:0 = 3 is -3 dB (scale -6 dB + 1 dB per step, max 9); PipeWire keeps the ALSA device open, so `aplay`/`speaker-test` get `Resource busy` and playback tests go through PipeWire (`paplay`).
- Still manual: reset of the mixer/DMA is done by the kernel only for the DMA; the mixer setup, the codec power sequences, the pad retention release (`pmu-aud-pad`, bit 28 of PMU 0x3028 reads back 0, probably a self-clearing trigger) and the routing are userspace scripts; real drivers (mixer, codec, a sound card with routes and the I2S clock ratio 512 x fs) are next.

## 95. Earpiece works, loudspeaker is silent: there is an external amplifier bus (2026-11-07)

Jimmy's A/B result: one clean 6 s tone with only the loudspeaker path switched on in the codec (earpiece off: `0x76` = 0x26, `0x17` = 0x14, `0x37` = 0x0c, `0x40` = 0x79 read back before the tone): **nothing audible**; the earlier tone with both paths on was audible from the earpiece only. So the DAC, mixer, DMA and I2S chain are proven and the codec's own speaker driver does not make the bottom loudspeaker play.
- Vendor DT (`upstream/k7880`, `exynos7880-a5y17lte_common.dtsi` and the Lineage/Treble variants): an I2C controller `i2c@13860000` (`i2c_8` in `exynos7880.dtsi`, `samsung,s3c2440-i2c`, SPI 427, clock "rate_i2c"/"gate_i2c" = CMU clock 115, vendor bus number 20, 400 kHz) with pins **gpc9-0 and gpc9-1 on function 2** (pin group `spk_amp_i2c`, no pull), and
  an unspecific "AMP" I2S pad gpz1-5 (`i2s_amp_bus`). No child device is described on that bus, and the vendor kernel has no amplifier driver (no tfa98xx/max98505/cs35 options in `hybris-a5y17lte_defconfig`; only `I2C_CHARDEV=y`), so the speaker amplifier is probably configured from userspace by Samsung's audio HAL over `/dev/i2c-20`.
  The audio data for the amplifier would then come from the mixer's AMP output (`OUTAP1`/`OUTCP1`, registers 0x19/0x1a, `DIG_EN` bits AP1/CP1) on gpz1-5, which stayed high (silent) during all tones so far.
- Next: identify the amplifier with a userspace I2C scan of gpc9-0/1 (bit-banged through the GPIO character device, no kernel change), then find its address/ID, and find out what it needs (power, enable, an I2S data stream on gpz1-5).

## 96. The loudspeaker amplifier bus found: SDA gpc9-0, SCL gpc9-1, devices at 0x34 and 0x0e (2026-11-07, read-only)

`mainline/tools/i2c-gpio-scan.c` (static, GPIO character device v2, bit-banged at ~10 kHz, open-drain lines switched to input for reads; the vendor's `spk_amp_i2c` pad group is not claimed by Linux, so the pads were free for it; it only sends address bytes and register reads), run on the phone:
- **Bus:** SDA = gpc9-0, SCL = gpc9-1 (the swapped assignment finds nothing); both idle high, so the board has pull-ups. **Two 7-bit addresses acknowledge: 0x0e and 0x34.**
- **Register reads (two bytes per register, 16-bit registers, big endian):** at 0x34: reg 0x01 = 0x03ff, 0x02 = 0x0100, 0x03 = 0x1b96, 0x04 = 0x8088, 0x05 = 0x9391, 0x06 = 0x0001, 0x09 = 0x004d (reg 0x00 once gave no ACK); at 0x0e: reg 0x00 = 0x091d, **0x01 = 0x03ff, 0x02 = 0x0100, 0x03 = 0x1b96, 0x04 = 0x8088** (identical to 0x34), 0x10 = 0x5e28, 0x11 = 0xc0f1, 0x12 = 0x0028.
  The two addresses return the same register contents, so they are probably one chip answering at an alias (or two identical amplifiers); the register width and layout look like a smart speaker amplifier of the NXP TFA98xx family (I2C 0x34-0x37) or similar, but the revision value does not match the chips I know, so it is not identified yet.
- Observations: the USB networking link of the phone dropped on its own later (the phone stayed up, 50 min, and answered on Wi-Fi); the new rootfs has new SSH host keys, so the PC's entry for the Wi-Fi address was refreshed.
- Next: identify the amplifier and what it needs (an init sequence, an enable/boost supply, an I2S data stream from the mixer's AMP output on gpz1-5). The vendor kernel tree has no driver for it (EVIDENCE 95), so the only place that knows is Samsung's audio HAL or firmware files in the SYSTEM backup (e.g. a TFA98xx `.patch`/`.speaker` file set); reading proprietary HAL binaries needs Jimmy's OK.
