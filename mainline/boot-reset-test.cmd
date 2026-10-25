# EVIDENCE 29 test image v6: no Linux. v3-v5 all hung in "sleep": this U-Boot
# has no working timer (CNTFRQ_EL0 = 0 at EL1, CONFIG_ARMV8_CNTFRQ_BROKEN off,
# so get_timer() never advances and sleep/udelay never finish). They never
# reached the SWRESET write. v6 waits by filling scratch DRAM (4 MB at
# 0x58000000, 60 times, cache off) instead of sleeping.
# Stripes: red band rows 0-200 = U-Boot ran; green row 300 = after INFORM3 =
# recovery (0x12345674) write; blue row 400 = about to write SWRESET; white
# rows 500+ = still alive after the SWRESET write.
# TWRP appears = reset from U-Boot works. Stripes to blue, then frozen/no
# white = the SWRESET write ended U-Boot (reset happened or not: look for logo).
# Build: BOOTCMD=boot-reset-test.cmd MILESTONE=ubreset6 ./build-mainline.sh
mw.l 0x67000000 0xffff0000 0x34bc0
dcache off
mw.l 0x14830070 0x10
mw.l 0x106b080c 0x12345674
mw.l 0x6713c680 0xff00ff00 0xa8c0
for i in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59; do mw.l 0x58000000 0 0x100000; done
mw.l 0x671a5e00 0xff0000ff 0xa8c0
for i in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59; do mw.l 0x58000000 0 0x100000; done
mw.l 0x106b0400 1
mw.l 0x6720f580 0xffffffff 0xa8c0
for i in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59; do mw.l 0x58000000 0 0x100000; done
mw.l 0x6724ea00 0xffffffff 0xa8c0
for i in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59; do mw.l 0x58000000 0 0x100000; done
mw.l 0x6728de80 0xffffffff 0xa8c0
for i in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59; do mw.l 0x58000000 0 0x100000; done
mw.l 0x672cd300 0xffffffff 0xa8c0
for i in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59; do mw.l 0x58000000 0 0x100000; done
