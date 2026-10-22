# U-Boot boot script, run from the FIT that S-Boot loads as the "ramdisk"
# (include/configs/exynos78x0-common.h: bootcmd=source $prevbl_initrd_start_addr:bootscript)
echo "a5y17lte: FIT at ${prevbl_initrd_start_addr}, booting mainline Linux (M5e)"
# K8: clocks for the SDIO host (mmc1, Wi-Fi). S-Boot never uses it and Samsung's
# kernel gates it at boot ([P] S5E7880-vclk.c), and there is no Linux CMU_CCORE
# driver yet, so set it here, before Linux probes mmc1 (a gated host can hang
# the bus). [P] S5E7880-cmusfr.h / -cmu.c: CCORE mux 0x28c (bit 12 = 0:
# BUS_PLL/2, bit 21 = mux gate), div 0x48c (bits 9:0 = ratio - 1; 3 -> 1599/2/4
# = 199.875 MHz), gate 0x88c bit 0; FSYS 0x804 bit 9 (MMC1 ACLK), 0x800 bit 3
# (MMC1 OSCCLK), 0x818 bit 0 (SDCLKIN). Read-modify-write, nothing else changes.
setexpr.l a5v *0x1068028c '&' 0xffffefff
setexpr a5v ${a5v} '|' 0x00200000
mw.l 0x1068028c ${a5v}
setexpr.l a5v *0x1068048c '&' 0xfffffc00
setexpr a5v ${a5v} '|' 3
mw.l 0x1068048c ${a5v}
setexpr.l a5v *0x1068088c '|' 1
mw.l 0x1068088c ${a5v}
setexpr.l a5v *0x13730804 '|' 0x200
mw.l 0x13730804 ${a5v}
setexpr.l a5v *0x13730800 '|' 8
mw.l 0x13730800 ${a5v}
setexpr.l a5v *0x13730818 '|' 1
mw.l 0x13730818 ${a5v}
echo "a5y17lte: mmc1 clocks set (CCORE 0x28c/0x48c/0x88c, FSYS 0x800/0x804/0x818)"
bootm ${prevbl_initrd_start_addr}#a5y17lte
echo "a5y17lte: bootm returned, resetting"
reset
