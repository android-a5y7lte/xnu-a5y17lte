# EVIDENCE 29 test image: no Linux. Show a banner, wait, reset. If S-Boot's
# normal boot after this reset comes back to this banner, Linux leaves the bad
# state; if it hangs at the Samsung logo, U-Boot or the reset itself does.
# Build: BOOTCMD=boot-reset-test.cmd MILESTONE=ubreset ./build-mainline.sh
echo "a5y17lte: U-BOOT RESET TEST (no Linux), resetting in 8 s"
sleep 8
reset
