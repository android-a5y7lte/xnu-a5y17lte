#!/bin/sh
# Galaxy A5 (2017) mainline, K9: Bluetooth (QCA9377 BT over the Samsung UART,
# hci_qca) on Nura (postmarketOS). Copy to the phone and run as root:
#   scp -O mainline/tools/a5-bt.sh root@172.16.42.1:/root/
#
#   a5-bt.sh check            hci device, firmware files, dmesg
#   a5-bt.sh setup            install BlueZ and Samsung's BT firmware (needs
#                             internet via the USB NAT, EVIDENCE 26), power on
#   a5-bt.sh on               power the controller on (loads the firmware)
#   a5-bt.sh scan             scan for 15 s
#
# Firmware: hci_qca asks for qca/nvm_tlv_tf_1.1.bin and
# qca/rampatch_tlv_tf_1.1.tlv (DT firmware-name). These are Samsung's TLV files
# for the QCA9377 ("TF" 1.1), from the a5y17lte/a7y17lte vendor files; they are
# not in linux-firmware under these names. The firmware is loaded when the
# controller is powered on, so no re-probe is needed after installing it.
set -eu

FWDIR=/lib/firmware/qca
MUPPETS=https://github.com/TheMuppets/proprietary_vendor_samsung/raw/332fcf062d447c4cee54ccd240553f310d793f1c/universal7880-common/proprietary/etc/firmware
NVM_SHA=8869a8e490f2f00a66de40232f6d587cc741753d64f08e35d3358ca21b6ab665
PATCH_SHA=143a3a869a48e71c9f92250586a0a4f6f505f75c48bc7ebbe4570f22f60ca17b

say() { echo " bt     : $*"; }

check() {
	found=0
	for h in /sys/class/bluetooth/hci*; do
		[ -e "$h" ] || continue
		found=1
		say "$(basename "$h") present"
	done
	[ $found = 1 ] || say "no hci device (serial1 or hci_qca did not probe: dmesg below)"
	for f in nvm_tlv_tf_1.1.bin rampatch_tlv_tf_1.1.tlv; do
		if [ -e "$FWDIR/$f" ]; then say "firmware: $FWDIR/$f"; else say "firmware MISSING: $FWDIR/$f"; fi
	done
	command -v btmgmt >/dev/null && btmgmt info 2>/dev/null | head -n 8 || true
	dmesg | grep -i 'bluetooth\|hci\|qca\|13810000\|ttySAC' | tail -n 20 || true
}

fetch() {	# name sha256
	tmp=$(mktemp)
	curl -fsSL -o "$tmp" "$MUPPETS/$1"
	sum=$(sha256sum "$tmp" | cut -d' ' -f1)
	if [ "$sum" != "$2" ]; then
		rm -f "$tmp"
		say "$1: checksum mismatch ($sum), not installed"
		return 1
	fi
	install -m 0644 "$tmp" "$FWDIR/$1"
	rm -f "$tmp"
	say "installed $FWDIR/$1"
}

setup() {
	apk add bluez bluez-btmgmt curl
	mkdir -p "$FWDIR"
	fetch nvm_tlv_tf_1.1.bin "$NVM_SHA"
	fetch rampatch_tlv_tf_1.1.tlv "$PATCH_SHA"
	rc-service bluetooth start 2>/dev/null || true
	on
}

on() {
	if command -v bluetoothctl >/dev/null && rc-service bluetooth status >/dev/null 2>&1; then
		bluetoothctl power on
	else
		btmgmt power on
	fi
	sleep 2
	check
}

scan() {
	bluetoothctl --timeout 15 scan on
	bluetoothctl devices
}

case "${1:-check}" in
check)	check ;;
setup)	setup ;;
on)	on ;;
scan)	scan ;;
*)	sed -n '2,12p' "$0"; exit 2 ;;
esac
