#!/bin/sh
# PC side of the phone's USB internet (EVIDENCE 26, 30): forward and masquerade
# 172.16.42.0/24 (the phone, 172.16.42.1; the PC is 172.16.42.2 on the phone's
# usb0 interface, named enx02a517000002). Idempotent. Installed by
# tools/install-pc-nat.sh as a systemd service that runs after Docker, whose
# FORWARD policy is DROP and whose DOCKER-USER chain is where the ACCEPTs go.
set -eu
IF=enx02a517000002
NET=172.16.42.0/24
sysctl -qw net.ipv4.ip_forward=1
rule() { # rule TABLE CHAIN POS ARGS...: insert/append only if missing
	t=$1; c=$2; pos=$3; shift 3
	iptables -t "$t" -C "$c" "$@" 2>/dev/null || iptables -t "$t" "$pos" "$c" "$@"
}
rule nat POSTROUTING -A -s $NET ! -d $NET -j MASQUERADE
if iptables -n -L DOCKER-USER >/dev/null 2>&1; then
	rule filter DOCKER-USER -I -i $IF -j ACCEPT
	rule filter DOCKER-USER -I -o $IF -j ACCEPT
else
	rule filter FORWARD -I -i $IF -j ACCEPT
	rule filter FORWARD -I -o $IF -j ACCEPT
fi
