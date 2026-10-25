# EVIDENCE 29 test image v7: no Linux, PLAIN reset. v6 (INFORM3 = recovery)
# reset from U-Boot and reached TWRP. v7 is the same but INFORM3 = 0 (what a
# plain `reboot` leaves), so S-Boot takes its normal-boot path to our U-Boot.
#   hangs at the logo = plain reset + our U-Boot image hangs, Linux not needed.
#   band comes back, BLUE (marker in DRAM) or red = S-Boot restarts our U-Boot
#   after a plain reset; the Linux hang then comes from Linux's state.
# Loops forever if it works: Volume Down + Power 10 s to leave.
# Build: BOOTCMD=boot-reset-test.cmd MILESTONE=ubreset7 ./build-mainline.sh
if itest.l *0x6762e080 -eq 0x600dcafe; then
mw.l 0x67000000 0xff0000ff 0x34bc0
else
mw.l 0x67000000 0xffff0000 0x34bc0
fi
mw.l 0x6762e080 0x600dcafe
dcache off
mw.l 0x14830070 0x10
mw.l 0x106b080c 0
mw.l 0x6713c680 0xff00ff00 0xa8c0
for i in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59; do mw.l 0x58000000 0 0x100000; done
mw.l 0x671a5e00 0xffffffff 0xa8c0
for i in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59; do mw.l 0x58000000 0 0x100000; done
mw.l 0x106b0400 1
mw.l 0x6720f580 0xffff00ff 0xa8c0
for i in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59; do mw.l 0x58000000 0 0x100000; done
mw.l 0x6724ea00 0xffff00ff 0xa8c0
for i in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59; do mw.l 0x58000000 0 0x100000; done
mw.l 0x6728de80 0xffff00ff 0xa8c0
for i in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59; do mw.l 0x58000000 0 0x100000; done
mw.l 0x672cd300 0xffff00ff 0xa8c0
for i in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59; do mw.l 0x58000000 0 0x100000; done
