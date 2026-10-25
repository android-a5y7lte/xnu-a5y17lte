# EVIDENCE 29 test image v4: no Linux. This U-Boot has no video driver and no
# working "reset" command (SYSRESET off, reset_cpu() empty on arm64), so it
# draws through S-Boot's framebuffer (0x67000000, 1080 wide, ARGB8888),
# triggers the command-mode panel (DECON 0x70 = 0x10, as a5-diag panel) and
# resets with the PMU SWRESET write Linux uses (0x106b0400 = 1).
# v3 showed a static red band + logo for 5 min: either the reset did not
# happen, or it did and S-Boot never started us again, and a stale frame looks
# the same. v4 tells them apart:
#   - after the reset write, a green stripe is drawn every second for 10 s:
#     stripes keep coming = the write did NOT reset (U-Boot still alive);
#     no stripes = the phone did reset.
#   - a marker pixel (DRAM, row 1500) makes a later boot draw BLUE instead of
#     red: a blue band = S-Boot started our U-Boot again after the reset.
# Build: BOOTCMD=boot-reset-test.cmd MILESTONE=ubreset4 ./build-mainline.sh
if itest.l *0x6762e080 -eq 0x600dcafe; then
mw.l 0x67000000 0xff0000ff 0x34bc0
else
mw.l 0x67000000 0xffff0000 0x34bc0
fi
mw.l 0x6762e080 0x600dcafe
dcache off
mw.l 0x14830070 0x10
echo "a5y17lte: U-BOOT RESET TEST v4 (no Linux), resetting in 8 s"
sleep 8
mw.l 0x106b0400 1
mw.l 0x6713c680 0xff00ff00 0xa8c0
sleep 1
mw.l 0x6717bb00 0xff00ff00 0xa8c0
sleep 1
mw.l 0x671baf80 0xff00ff00 0xa8c0
sleep 1
mw.l 0x671fa400 0xff00ff00 0xa8c0
sleep 1
mw.l 0x67239880 0xff00ff00 0xa8c0
sleep 1
mw.l 0x67278d00 0xff00ff00 0xa8c0
sleep 1
mw.l 0x672b8180 0xff00ff00 0xa8c0
sleep 1
mw.l 0x672f7600 0xff00ff00 0xa8c0
sleep 1
mw.l 0x67336a80 0xff00ff00 0xa8c0
sleep 1
mw.l 0x67375f00 0xff00ff00 0xa8c0
sleep 1
echo "a5y17lte: still running 10 s after the SWRESET write"
