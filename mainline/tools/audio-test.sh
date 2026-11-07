#!/bin/sh
# Galaxy A5 (2017) playback test without mixer/codec drivers (EVIDENCE 89): configures the audio mixer (S1402X at 0x14880000) and the
# COD3026x codec (7-bit 0x04 on i2c-1) from userspace with the values of the vendor drivers, then plays a tone through the
# kernel sound card "A5-Test" (I2S0, PL330 DMA, dummy codec). Run as root on the phone, with the static helper codec-seq in /tmp.
#   audio-test.sh mixer      reset and set up the mixer, mixer clock for 48 kHz
#   audio-test.sh up         codec: DAC and speaker path on (low speaker gain)
#   audio-test.sh ep         codec: earpiece path on (the top speaker; this is the path that was audible first, EVIDENCE 94)
#   audio-test.sh tone       4 seconds of 440 Hz, 48 kHz, S16_LE
#   audio-test.sh down       codec: speaker path and DAC off
CS=${CS:-/tmp/codec-seq}
BUS=1
ADDR=0x04

case "$1" in
mixer)
	python3 - <<'PY'
import mmap, os, struct, time
fd = os.open('/dev/mem', os.O_RDWR | os.O_SYNC)
def page(a): return mmap.mmap(fd, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE, offset=a & ~0xfff)
mix = page(0x14880000); cmu = page(0x148d0000)
def w(reg, v): mix[reg * 4:reg * 4 + 4] = struct.pack('<I', v)
def r(reg): return struct.unpack('<I', mix[reg * 4:reg * 4 + 4])[0]
# mixer system clock for 48 kHz: 98.304 MHz / 4 = 24.576 MHz (CMU_DISPAUD DIV_CLK_DISPAUD_MIXER = 3; it was 1)
cmu[0x410:0x414] = struct.pack('<I', 3)
w(0x00, 0x00); time.sleep(0.001); w(0x00, 0x03); time.sleep(0.001)
for reg, v in ((0x02, 0x00), (0x05, 0x00), (0x07, 0x04), (0x08, 0x00), (0x09, 0x24), (0x0b, 0x01), (0x0c, 0x02), (0x0e, 0x01),
               (0x18, 0x2a), (0x19, 0x00), (0x1a, 0x00), (0x0a, 0x04), (0x0f, 0x08)):
    w(reg, v)
w(0x00, r(0x00) & ~2); w(0x00, r(0x00) | 2)       # data path reset toggle, as the vendor driver does after the setup
# routing, which the vendor driver leaves to ALSA controls set by the Android audio HAL (EVIDENCE 94): AP0/SRC1 enable (DIG_EN bit 0, with MIX_EN bit 3)
# and channel 1 of the digital mixer DMIX1 (bit 3, level 0); the output select 0x16 already defaults to DMIX_OUT
w(0x0f, 0x09); w(0x10, 0x08)
print('mixer regs:', ' '.join('%02x=%02x' % (i, r(i)) for i in (0x00, 0x02, 0x05, 0x07, 0x09, 0x0a, 0x0b, 0x0c, 0x0e, 0x0f, 0x16, 0x17, 0x18, 0x19)))
PY
	;;
up)
	# dac_ev (power up) then the speaker switch and spkdrv_ev (power up), as in the vendor codec driver; the OTP offset trim is skipped
	$CS $BUS $ADDR u:0x40:0x20:0x20 u:0x40:0x10:0x00 u:0x40:0x10:0x10 \
		u:0x76:0x04:0x04 \
		w:0xdd:0x82 u:0x37:0x0c:0x00 g:0x32:0x0f u:0x32:0x0f:0x00 u:0x17:0x14:0x14 u:0x37:0x0c:0x0c ms:135 p:0x32:0x0f
	;;
ep)
	# dac_ev (power up) must have run ("up"); earpiece driver as in epdrv_ev: chop bit, auto power, earpiece mixer, 136 ms
	$CS $BUS $ADDR u:0x76:0x08:0x08 u:0x17:0x11:0x11 u:0x37:0x80:0x80 ms:136
	;;
tone)
	speaker-test -D hw:0,0 -t sine -f 440 -c 2 -r 48000 -F S16_LE -l 1 -s 1 2>&1 | tail -15 &
	sleep 4; pkill speaker-test; wait
	;;
down)
	$CS $BUS $ADDR u:0x32:0x0f:0x06 u:0x17:0x14:0x00 ms:1 u:0x37:0x0c:0x00 u:0x76:0x04:0x00 u:0x40:0x10:0x00 u:0x40:0x20:0x00 u:0x76:0x08:0x00 u:0x37:0x80:0x00 u:0x17:0x01:0x00
	;;
*)
	echo "usage: $0 mixer|up|ep|tone|down" >&2
	exit 2
	;;
esac
