# EVIDENCE 29 test image v5: no Linux (see v4). Stage stripes show how far
# U-Boot got; INFORM3 = recovery (0x12345674, the value that works from Linux)
# is written before the PMU SWRESET write. Stripes, top to bottom:
#   red band rows 0-200: U-Boot ran      green row 300: INFORM3 write returned
#   blue row 400: 3 s later, about to SWRESET     white rows 500+: still alive
#   after the SWRESET write (one per second for 6 s)
# Result guide: TWRP appears = reset from U-Boot works, S-Boot alive, only the
# normal-boot branch hangs. Frozen with stripes up to blue = SWRESET write
# killed U-Boot with no reset or S-Boot dead. Stripes up to green only = the
# INFORM3 write itself failed (abort).
# Build: BOOTCMD=boot-reset-test.cmd MILESTONE=ubreset5 ./build-mainline.sh
mw.l 0x67000000 0xffff0000 0x34bc0
dcache off
mw.l 0x14830070 0x10
mw.l 0x106b080c 0x12345674
mw.l 0x6713c680 0xff00ff00 0xa8c0
sleep 3
mw.l 0x671a5e00 0xff0000ff 0xa8c0
sleep 1
mw.l 0x106b0400 1
mw.l 0x6720f580 0xffffffff 0xa8c0
sleep 1
mw.l 0x6724ea00 0xffffffff 0xa8c0
sleep 1
mw.l 0x6728de80 0xffffffff 0xa8c0
sleep 1
mw.l 0x672cd300 0xffffffff 0xa8c0
sleep 1
mw.l 0x6730c780 0xffffffff 0xa8c0
sleep 1
mw.l 0x6734bc00 0xffffffff 0xa8c0
sleep 1
