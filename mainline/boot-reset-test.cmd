# EVIDENCE 29 test image: no Linux. This U-Boot has no video driver and no
# working "reset" (SYSRESET off, reset_cpu() is empty on arm64), so the first
# test (v1) showed nothing and did not reset. v2 draws through S-Boot's
# framebuffer (0x67000000, 1080 wide, ARGB8888) and resets with the PMU SWRESET
# write that Linux uses (0x106b0400 = 1).
#   red band (200 rows) = this U-Boot ran; 8 s later the phone resets.
#   band comes back every ~10 s: S-Boot warm-boots our U-Boot fine.
#   Samsung logo only, no band after the first reset: S-Boot (or the reset)
#   never reaches our U-Boot, Linux is not involved.
# Build: BOOTCMD=boot-reset-test.cmd MILESTONE=ubreset2 ./build-mainline.sh
mw.l 0x67000000 0xffff0000 0x34bc0
dcache off
echo "a5y17lte: U-BOOT RESET TEST v2 (no Linux), resetting in 8 s"
sleep 8
mw.l 0x106b0400 1
sleep 5
echo "a5y17lte: SWRESET write did not reset"
