# K8: Wi-Fi (research notes, read-only, 2026-10-20; nothing built or tested)

Sources: Samsung universal7880 kernel (hybris-17.1: `exynos7880.dtsi`,
`exynos7880-pinctrl.dtsi`, `exynos7880-a5y17lte_eur_open_00.dts`, the
`hybris-a5y17lte_defconfig`) and Linux v7.2.

## What the hardware is
- Chip: **Qualcomm QCA9377** (not Broadcom). Samsung's config has
  `WLAN_QCA9377_DUAL_BDF` and the qcacld-2.0 / `cnss_sdio` driver; the DT node
  is `qcom,cnss_sdio`. Bluetooth is the same combo chip (Samsung's node says
  `qca,qca6174`).
- Bus: **SDIO on dwmmc1**, `dwmmc1@13550000`, SPI 246 (mshc1), 4-bit, up to
  SDR104 in Samsung's settings, clocks `biu`/`ciu`/`ciu_gate`.
- Pins (FSYS pin controller, `pinctrl@13750000`, function 2): clock gpr2-0,
  command gpr2-1, data gpr3-0 .. gpr3-3.
- Control: **WLAN_EN gpd3-6** (reset/enable), **WLAN_DCDC_EN gpd3-3** (a GPIO
  regulator `cnss_dcdc_en`, startup 4 ms, active high), **WLAN_HOST_WAKE
  gpa2-2** (function 1, pull-up; wake-up EINT, which already works for the
  volume keys on gpa2).
- Bluetooth: BT_EN gpd4-2, BT_WAKE gpd5-1, BT_HOST_WAKE gpa1-6, on a UART
  (which one is not in the board node; not looked up yet). Separate step.

## What mainline already has
- **ath10k SDIO** with a QCA9377 entry (`"qca9377 hw1.1 sdio"`,
  `drivers/net/wireless/ath/ath10k/core.c`, SDIO id QCA9377 in `sdio.c`).
  Firmware directory `ath10k/QCA9377/hw1.0/`; for SDIO the loader asks for
  `firmware-sdio-5.bin` or `-6.bin` (name format `firmware-<bus>-<api>.bin`),
  plus board data (`board-2.bin`, falling back to `board.bin`).
- DT binding: node under the SDIO host with `compatible = "qcom,ath10k"`
  (optional; SDIO ids also match without it).
- Bluetooth: `hci_qca` lists `qcom,qca6174-bt` and `qcom,qca9377-bt`.
- dw_mmc with SDIO: used by other Exynos boards; `mmc-pwrseq-simple` handles
  the WLAN_EN reset line and `regulator-fixed` the DCDC enable.

## What we still need (the work)
1. **FSYS pin controller** (`pinctrl@13750000`) is not in our DT: add it with
   the `sd1-*` groups above. Unlike eMMC (S-Boot left those pins set), nothing
   configured the SDIO pins before Linux.
2. **MMC1 clocks.** Same method as M3b/M4: a5-diag prints the CMU_CCORE
   registers for MMC1: mux `+0x28C`, divider `+0x48C`, enable `+0x88C`, and the
   FSYS gate `CLK_ENABLE_CLK_FSYS_MMC1` at CMU_FSYS `+0x818` (CCORE base
   0x10680000, FSYS 0x13730000). S-Boot never used SDIO, so these gates may be
   off; a5-diag can switch them on as it did for USB, until K2 (the CMU
   driver) exists. MMC0's CIU was 399.75 MHz (BUS_PLL/2 / 2); expect the same
   source.
3. **DT**: `&mmc1` (reg 0x13550000, SPI 246, same compatible and Exynos7880
   fallback pattern as mmc0, `samsung,dw-mshc-ciu-div = <3>`, SDIO timings
   from Samsung: sdr `<3 0 3 0>`, sdr50 `<3 0 4 2>`, sdr104 `<3 0 3 0>`),
   `non-removable`, `cap-sdio-irq`, `bus-width = <4>`, `mmc-pwrseq` with
   `reset-gpios = <&gpd3 6 GPIO_ACTIVE_LOW>`, and `vmmc-supply` from a fixed
   regulator on gpd3-3 (`startup-delay-us = <4000>`). Check the dw_mmc
   SECURE_READ/SMU behaviour (EVIDENCE 17c): patch 0004 covers the 7880
   compatible, so mmc1 inherits it.
4. **Config** (built in): CFG80211, MAC80211, ATH10K, ATH10K_SDIO, MMC_DW
   already, plus `FW_LOADER` paths; regulatory db if wanted. The kernel has no
   modules, so firmware must be reachable when ath10k probes: put it in the
   initramfs or make the driver load late (`ath10k_sdio` built in will probe
   at boot and fail if the root filesystem is not mounted yet; a built-in
   driver retries only via deferred probe, so test with the firmware in the
   initramfs first).
5. **Firmware.** `firmware-sdio-*.bin` for QCA9377 hw1.0 is Qualcomm's file and
   normally comes from `linux-firmware` (postmarketOS: `linux-firmware-ath10k`);
   I could not check its contents from the build machine (kernel.org and
   postmarketOS GitLab blocked). The **board data** is device specific
   calibration; the stock LineageOS SYSTEM backup in `~/a5-backup` probably
   holds Samsung's Wi-Fi files (loop-mount it read-only on the PC and search
   for `wlan`/`bdwlan`/`qca_cld`). Whether to use those is your call: they are
   Qualcomm calibration data for this board, not Samsung HALs, but they are
   still proprietary files.

## Test plan (all read-only until the firmware is placed)
a. `a5-diag mmc1`: print/switch the MMC1 clock gates; check the SDIO card
   answers CMD5 (dmesg `mmc1: new ... SDIO card`, vendor 0x271, device id).
b. With firmware: `dmesg | grep ath10k`, `ip link` shows wlan0, `iw dev wlan0 scan`.
c. Join a network with `iwd` or `wpa_supplicant`; measure with iperf3.
Wake-on-WLAN, power saving and Bluetooth come after the scan works.

## Risks
- Wrong or missing board data: the firmware loads but scans are poor or fail
  (error messages mention board data / bdf).
- QCA9377 over SDIO on a mainline dw_mmc host at SDR104 needs tuning; start at
  25 MHz high-speed (cap the host) and raise later.
- The Android side (A5) only needs `wpa_supplicant` and `nl80211` on top; the
  same kernel serves both OSes.
