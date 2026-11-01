#!/bin/sh
# Log the charger, MUIC cable-type registers and battery of the Galaxy A5 (2017) once a second.
# Run it on the phone, detached, so that it survives a cable change that drops the SSH link:
#   nohup sudo ./charge-probe.sh 180 > /tmp/charge-probe.log 2>&1 &
# then read /tmp/charge-probe.log afterwards (it lives in RAM). EVIDENCE 57/58.
#
# Registers (S2MU004 at 0x3d on the MUIC bus; Samsung s2mu004-private.h / s2mu004-muic.h):
#   ADC 0x61, DEVICE_TYPE1 0x62 (0x04 USB, 0x20 CDP, 0x40/0x44/0x60 dedicated charger), TYPE2 0x63,
#   TYPE3 0x64, CHG_TYPE 0x68, DEVICE_APPLE 0x69 (0x80 0.5A, 0x40 1A, 0x20 2A, 0x10 2.4A, 0x08 SDP DCD out),
#   SC_STATUS0 0x0a (bits 7:5 input status, 3 or 5 = valid), STATUS1 0x0b, STATUS3 0x0d,
#   SC_CTRL2 0x12 (input current limit = value * 25 + 50 mA), SC_CTRL9 0x19 (fast charge = (value + 1) * 25 mA).
# Read only: nothing is written.

SECONDS=${1:-120}
BUS=$(for d in /sys/bus/i2c/devices/i2c-*; do case "$(cat $d/name)" in *muic*) echo ${d##*-};; esac; done | head -1)
CHG=/sys/class/power_supply/s2mu004-charger
BAT=/sys/class/power_supply/s2mu004-fuel-gauge

rd() { i2cget -y -f "$BUS" 0x3d "$1" 2>/dev/null || echo "??"; }

echo "# charge-probe: muic bus i2c-$BUS, $SECONDS s"
echo "# t  online  bat_status  cap%  mV  mA(+charging)  | adc t1 t2 t3 chg apple | st0 st1 st3 | ctrl2 ctrl9 | cable-guess"
i=0
while [ "$i" -lt "$SECONDS" ]; do
	t1=$(rd 0x62)
	case "$t1" in
	0x40|0x44|0x60) guess=DEDICATED-CHARGER ;;
	0x20) guess=CDP ;;
	0x04) guess=USB-PC ;;
	0x00) guess=none/unknown ;;
	*) guess=other ;;
	esac
	echo "$i s online=$(cat $CHG/online) $(cat $BAT/status) $(cat $BAT/capacity)% $(( $(cat $BAT/voltage_now) / 1000 ))mV $(( $(cat $BAT/current_now) / 1000 ))mA" \
	     "| adc=$(rd 0x61) t1=$t1 t2=$(rd 0x63) t3=$(rd 0x64) chg=$(rd 0x68) apple=$(rd 0x69)" \
	     "| st0=$(rd 0x0a) st1=$(rd 0x0b) st3=$(rd 0x0d) | ctrl2=$(rd 0x12) ctrl9=$(rd 0x19) | $guess"
	sleep 1
	i=$((i + 1))
done
