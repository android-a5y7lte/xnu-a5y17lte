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
