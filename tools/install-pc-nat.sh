#!/bin/sh
# Installs the PC side of the phone's USB internet (needs sudo): the rules
# script, a systemd service that applies it at boot (after Docker), and the
# ip_forward sysctl. Undo: systemctl disable --now a5y17lte-nat and delete the
# three files below.
set -eu
D="$(cd "$(dirname "$0")" && pwd)"
install -Dm755 "$D/pc-usbnet-nat.sh" /usr/local/sbin/a5y17lte-pc-nat
install -Dm644 "$D/a5y17lte-nat.service" /etc/systemd/system/a5y17lte-nat.service
printf 'net.ipv4.ip_forward = 1\n' > /etc/sysctl.d/99-a5y17lte-forward.conf
systemctl daemon-reload
systemctl enable --now a5y17lte-nat.service
