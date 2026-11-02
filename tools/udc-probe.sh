#!/bin/sh
# Log the USB gadget (UDC) state next to the charger state once a second, to see whether a plain wall
# charger can be told from a PC port by "no host enumerates" (EVIDENCE 60). Read only, writes nothing.
# Run it on the phone, detached, so that it survives a cable change that drops the SSH link:
#   nohup sudo ./udc-probe.sh 300 > /tmp/udc-probe.log 2>&1 &
# Hot-plug in this order while it runs and note the times: PC cable, unplug, wall charger, unplug, PC again.
# Read /tmp/udc-probe.log afterwards (RAM, cleared by a reboot).

SECONDS=${1:-180}
BUS=$(for d in /sys/bus/i2c/devices/i2c-*; do case "$(cat $d/name)" in *muic*) echo ${d##*-};; esac; done | head -1)
CHG=/sys/class/power_supply/s2mu004-charger
BAT=/sys/class/power_supply/s2mu004-fuel-gauge
UDC=$(ls /sys/class/udc | head -1)

rd() { i2cget -y -f "$BUS" 0x3d "$1" 2>/dev/null || echo "??"; }

echo "# udc-probe: udc=$UDC muic bus i2c-$BUS, $SECONDS s"
echo "# t  udc_state  usb0_carrier  online  bat_status  mA(+charging)  st0  apple"
i=0
while [ "$i" -lt "$SECONDS" ]; do
	echo "$i s udc=$(cat /sys/class/udc/$UDC/state) carrier=$(cat /sys/class/net/usb0/carrier 2>/dev/null || echo -)" \
	     "online=$(cat $CHG/online) $(cat $BAT/status) $(( $(cat $BAT/current_now) / 1000 ))mA st0=$(rd 0x0a) apple=$(rd 0x69)"
	sleep 1
	i=$((i + 1))
done
