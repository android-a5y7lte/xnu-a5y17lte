# Next phone test (prepared 2026-10-23, images rebuilt 2026-10-26 after merging main)

The three images now also contain Jimmy's main up to a714c3c (EVIDENCE 28/29):
`a5-diag reboot recovery|download|bootloader`, banners without the penguin.
Use `a5-diag reboot recovery` to get back to TWRP between tests (plain
`reboot` still hangs, EVIDENCE 28). Check with `sha256sum -c out/<image>.sha256`:
K9 `d18d12f4da2625e7…`, K2 `8072fd28fd437ad6…`, K6 `3adf84cd87d6e5c6…`.

All images are in `out/` on branch **android-k6** (it contains K1, K8, K5a,
K9 and K2; K5b, Panfrost, is on its own branch android-k5b and not built). Flash with TWRP -> Install -> Install Image -> Boot. None of them writes
the eMMC in rescue; hold **Volume Down** at the 3 s prompt for rescue, then
`ssh root@172.16.42.1`. Rollback any time: flash the M5e image.

| Order | Image | What it adds | Flash only if |
|---|---|---|---|
| 1 | `a5y17lte-mainline-k9-boot.img` | touch (K1), Wi-Fi (K8a/b), GPU checks (K5a), Bluetooth (K9) | - |
| 2 | `a5y17lte-mainline-k2-boot.img` | real clock driver (CMU_CCORE, CMU_PERI) | 1 boots and USB works |
| 3 | `a5y17lte-mainline-k6-boot.img` | battery fuel gauge | 2 boots and USB works |
| - | K5b (Panfrost) | not built | the K5a GPU checks passed |

## Image 1 (K9), in rescue, paste the output of:
```
dmesg | grep -i 'zinitix\|s2mpu05\|vtouch'; a5-diag touch   # then touch the screen: "touch X,Y" lines
a5-diag mmc1; dmesg | grep -i 'mmc1\|sdio\|ath10k'          # want "new ... SDIO card"
a5-diag bt; ls /sys/class/bluetooth; dmesg | grep -i 'qca\|hci\|13810000'
a5-diag gpu status
a5-diag gpu on          # one at a time; a hang = Volume Down + Power
a5-diag gpu
a5-diag gpu clk
a5-diag gpu id          # want GPU_ID 0x0830xxxx
```
Then boot Nura (no key), USB internet as in EVIDENCE 26, copy and run:
`mainline/tools/a5-wifi.sh setup`, `scan`, `connect "<ssid>" "<pass>"`;
`mainline/tools/a5-bt.sh setup`, `scan`.

## Image 2 (K2), in rescue:
```
cat /sys/kernel/debug/clk/clk_summary | head -80   # fout_mif_bus_pll ~1599 MHz
lsblk; a5-diag mmc1; a5-diag bt                     # eMMC, SDIO and BT clocks still fine
```

## Image 3 (K6), in rescue:
```
cat /sys/class/power_supply/*/uevent                # %, voltage, current
```
Details for each step: EVIDENCE sections K1, K8b, K9, K5a, K2, K6a.
