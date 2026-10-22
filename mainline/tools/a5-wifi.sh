#!/bin/sh
# Galaxy A5 (2017) mainline, K8b: Wi-Fi (Qualcomm QCA9377 over SDIO, ath10k) on
# Nura (postmarketOS). Copy to the phone and run as root:
#   scp -O mainline/tools/a5-wifi.sh root@172.16.42.1:/root/   (or user@...)
#
#   a5-wifi.sh check                 what is there: SDIO card, driver, firmware, wlan0
#   a5-wifi.sh setup [BOARD.bin]     install firmware + tools (needs internet via
#                                    the USB NAT, EVIDENCE 26), Samsung board data,
#                                    then re-probe the driver
#   a5-wifi.sh probe                 re-probe ath10k on the SDIO card
#   a5-wifi.sh scan                  list networks
#   a5-wifi.sh connect SSID [PASS]   join with NetworkManager (or wpa_supplicant)
#
# Nothing here writes outside /lib/firmware/updates and the apk database.
#
# Firmware: ath10k needs Qualcomm's SDIO firmware from linux-firmware
# (ath10k/QCA9377/hw1.0/firmware-sdio-5.bin, package linux-firmware-ath10k).
# Its board-2.bin has no SDIO entry (bus=sdio,vendor=0271,device=0701), so
# ath10k falls back to board.bin. The firmware ignores the chip's OTP, so the
# board data is the radio calibration: use Samsung's own (bdwlan30.bin from the
# a5y17lte/a7y17lte vendor files, 8124 bytes), placed in /lib/firmware/updates,
# which the kernel searches before /lib/firmware (no clash with the package).
# Samsung's qcacld picks bdwlan30_OLD.bin on board revision <= 3 (prototypes),
# bdwlan30.bin otherwise (CONFIG_WLAN_QCA9377_DUAL_BDF=3); retail phones are
# expected to use bdwlan30.bin.
set -eu

FWDIR=/lib/firmware/updates/ath10k/QCA9377/hw1.0
MUPPETS=https://github.com/TheMuppets/proprietary_vendor_samsung/raw/332fcf062d447c4cee54ccd240553f310d793f1c/universal7880-common/proprietary/etc/firmware
BOARD_SHA=ee4118d7551b05956a4c6d09c2d094c99cea2c160f49b02e184111f4840ad655	# bdwlan30.bin
SDIO_VENDOR=0x0271	# Qualcomm Atheros

say() { echo " wifi   : $*"; }

sdio_dev() {	# prints the QCA SDIO function device name, e.g. mmc1:0001:1
	for d in /sys/bus/sdio/devices/*; do
		[ -e "$d/vendor" ] || continue
		[ "$(cat "$d/vendor")" = "$SDIO_VENDOR" ] && { basename "$d"; return 0; }
	done
	return 1
}

check() {
	if d=$(sdio_dev); then
		say "SDIO function $d, vendor $(cat /sys/bus/sdio/devices/$d/vendor) device $(cat /sys/bus/sdio/devices/$d/device)"
		if [ -e /sys/bus/sdio/devices/$d/driver ]; then
			say "driver: $(basename "$(readlink /sys/bus/sdio/devices/$d/driver)")"
		else
			say "driver: NOT BOUND (firmware missing at boot is normal: run setup or probe)"
		fi
	else
		say "no Qualcomm SDIO function: the card was not found (check 'a5-diag mmc1' in rescue, dmesg | grep mmc1)"
	fi
	for f in firmware-sdio-5.bin board.bin board-2.bin; do
		for p in /lib/firmware/updates/ath10k/QCA9377/hw1.0 /lib/firmware/ath10k/QCA9377/hw1.0; do
			for x in "" .zst .xz; do
				[ -e "$p/$f$x" ] && say "firmware: $p/$f$x"
			done
		done
	done
	if [ -e /sys/class/net/wlan0 ]; then
		say "wlan0: $(cat /sys/class/net/wlan0/operstate), MAC $(cat /sys/class/net/wlan0/address)"
	else
		say "wlan0: missing"
	fi
	dmesg | grep -i 'ath10k\|mmc1' | tail -n 15 || true
}

probe() {
	d=$(sdio_dev) || { say "no SDIO card, nothing to probe"; return 1; }
	if [ -e /sys/bus/sdio/devices/$d/driver ]; then
		echo "$d" > /sys/bus/sdio/devices/$d/driver/unbind
		sleep 1
	fi
	echo "$d" > /sys/bus/sdio/drivers_probe
	for i in 1 2 3 4 5 6 7 8 9 10; do
		[ -e /sys/class/net/wlan0 ] && break
		sleep 1
	done
	check
}

setup() {
	apk add linux-firmware-ath10k wireless-regdb iw wpa_supplicant curl
	command -v nmcli >/dev/null && apk add networkmanager-wifi || true
	mkdir -p "$FWDIR"
	tmp=$(mktemp)
	if [ $# -ge 1 ]; then
		cp "$1" "$tmp"
	else
		curl -fsSL -o "$tmp" "$MUPPETS/bdwlan30.bin"
	fi
	sum=$(sha256sum "$tmp" | cut -d' ' -f1)
	if [ "$sum" != "$BOARD_SHA" ] && [ $# -eq 0 ]; then
		rm -f "$tmp"
		say "board data checksum mismatch ($sum), not installed"
		return 1
	fi
	install -m 0644 "$tmp" "$FWDIR/board.bin"
	rm -f "$tmp"
	say "board data installed: $FWDIR/board.bin ($sum)"
	probe
}

scan() {
	ip link set wlan0 up
	iw dev wlan0 scan | grep -E '^BSS|SSID:|signal:|freq:' | sed 's/^\t/  /'
}

connect() {
	ssid=$1; pass=${2:-}
	if command -v nmcli >/dev/null && rc-service networkmanager status >/dev/null 2>&1; then
		nmcli dev set wlan0 managed yes 2>/dev/null || true
		nmcli dev wifi rescan 2>/dev/null || true
		sleep 3
		if [ -n "$pass" ]; then
			nmcli dev wifi connect "$ssid" password "$pass" ifname wlan0
		else
			nmcli dev wifi connect "$ssid" ifname wlan0
		fi
	else
		conf=/run/a5-wifi-wpa.conf
		if [ -n "$pass" ]; then
			wpa_passphrase "$ssid" "$pass" > "$conf"
		else
			printf 'network={\n\tssid="%s"\n\tkey_mgmt=NONE\n}\n' "$ssid" > "$conf"
		fi
		chmod 600 "$conf"
		wpa_supplicant -B -i wlan0 -D nl80211 -c "$conf"
		udhcpc -i wlan0 -n -q
	fi
	ip addr show wlan0
	ip route
}

case "${1:-check}" in
check)	check ;;
probe)	probe ;;
setup)	shift; setup "$@" ;;
scan)	scan ;;
connect) [ $# -ge 2 ] || { echo "usage: $0 connect SSID [PASSWORD]"; exit 2; }; shift; connect "$@" ;;
*)	sed -n '2,15p' "$0"; exit 2 ;;
esac
