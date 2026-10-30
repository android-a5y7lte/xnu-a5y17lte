#!/usr/bin/env bash
# Galaxy A5 (2017) M5: postmarketOS root filesystem for the SYSTEM partition.
# pmbootstrap and pmaports at the commits pinned in setup.sh, plus our two
# packages from pmos/ (device-samsung-a5y17lte, linux-postmarketos-exynos7880).
# The kernel package gets the same patches, device tree and .config as the
# FIT that build-mainline.sh puts on the BOOT partition.
# pmbootstrap needs sudo (chroots); everything stays in build/pmbootstrap.
# Phosh (greetd autologin, software rendering, scale 2) comes from the device package.
# Output: build/pmos/samsung-a5y17lte-root.img (ext4) + .sha256
# Env: A5_PMOS_PASSWORD (default 147147, change it with passwd on the phone;
#      ~/.ssh/*.pub are installed for SSH key login either way)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LINUX="$ROOT/upstream/linux"
PMA="$ROOT/upstream/pmaports"
W="$ROOT/build/pmbootstrap"
CFG="$ROOT/build/pmbootstrap_v3.cfg"
O="$ROOT/build/pmos"
K=linux-postmarketos-exynos7880
DEV=device-samsung-a5y17lte
# linux-7.2.tar.xz; its sha256 f9fef3d1...02b3 matches kernel.org v7.x/sha256sums.asc
KERNEL_SHA512=47e63679363261a864d271277340a6f2d45f544e1a056be4159df081f4f6537d0efa865c4af26611ab33a3079ee65db88ec2f6bc0e5fff43e0c043cde0cd91e1
pmb() { python3 "$ROOT/upstream/pmbootstrap/pmbootstrap.py" -c "$CFG" -y "$@"; }

[ -d "$PMA/.git" ] && [ -d "$ROOT/upstream/pmbootstrap/.git" ] || { echo "run ./setup.sh first"; exit 1; }
[ "$(git -C "$LINUX" rev-parse --abbrev-ref HEAD)" = a5y17lte ] || { echo "run ./build-mainline.sh first"; exit 1; }
mkdir -p "$W" "$O"
# a fresh work dir needs the version stamp pmbootstrap init would write
[ -e "$W/version" ] || [ -e "$W/chroot_native" ] || \
	echo "$(sed -n 's/^work_version = //p' "$ROOT/upstream/pmbootstrap/pmb/config/__init__.py")" > "$W/version"

echo "== kernel config (same as build-mainline.sh)"
KCONFIG_ALLCONFIG="$ROOT/mainline/config/a5y17lte.config" \
	make -s -C "$LINUX" ARCH=arm64 LLVM=1 allnoconfig >/dev/null

echo "== our packages -> pmaports device/testing"
# ours replaces the archived downstream port of the same name
rm -rf "$PMA/device/archived/$DEV"
for p in "$DEV" "$K"; do
	rm -rf "$PMA/device/testing/$p"
	cp -r "$ROOT/pmos/$p" "$PMA/device/testing/$p"
done
cp "$ROOT"/mainline/patches/*.patch "$ROOT"/mainline/dts/exynos7880*.dts* "$PMA/device/testing/$K/"
cp "$LINUX/.config" "$PMA/device/testing/$K/config-postmarketos-exynos7880.aarch64"
# every patch must be listed in the APKBUILD
for p in "$ROOT"/mainline/patches/*.patch; do
	grep -q "^	$(basename "$p")$" "$ROOT/pmos/$K/APKBUILD" || { echo "not in $K/APKBUILD: $p"; exit 1; }
done

cat > "$CFG" <<EOF
[pmbootstrap]
aports = $PMA
work = $W
device = samsung-a5y17lte
ui = console
service_manager = openrc
extra_packages = doas,i2c-tools,libgpiod,evtest,gnome-console,gnome-text-editor,gnome-calculator,gnome-clocks,gnome-contacts,gnome-calendar,loupe,papers,nautilus,gnome-maps,epiphany
user = user
hostname = a5y17lte
ssh_keys = True
timezone = $(cat /etc/timezone 2>/dev/null || echo UTC)
locale = en_US.UTF-8
jobs = $(nproc)
EOF

echo "== checksums (kernel tarball pinned)"
mkdir -p "$W/cache_distfiles"
[ -f "$W/cache_distfiles/linux-7.2.tar.xz" ] || [ ! -f "$ROOT/upstream/tools/dl/linux-7.2.tar.xz" ] || \
	cp "$ROOT/upstream/tools/dl/linux-7.2.tar.xz" "$W/cache_distfiles/"
pmb checksum "$DEV" "$K"
grep -q "^$KERNEL_SHA512  linux-7.2.tar.xz$" "$PMA/device/testing/$K/APKBUILD" || \
	{ echo "linux-7.2.tar.xz checksum does not match the pinned one"; exit 1; }

echo "== build + install (console UI, SSH)"
# the patched phosh (auto-unlock at a passcode length, EVIDENCE 46): Alpine's recipe + our patch, cross built
rm -rf "$PMA/temp/phosh"; cp -r "$ROOT/pmos/phosh" "$PMA/temp/phosh"
pmb build --arch aarch64 phosh
pmb build --force "$K" "$DEV"
# --zap: a fresh rootfs every time (post-install scripts, no upgrade leftovers)
pmb install --zap --split --password "${A5_PMOS_PASSWORD:-147147}"

R="$W/chroot_native/home/pmos/rootfs/samsung-a5y17lte-root.img"
sudo cp "$R" "$O/samsung-a5y17lte-root.img"
sudo chown "$(id -u):$(id -g)" "$O/samsung-a5y17lte-root.img"

echo "== /etc/fstab: drop /boot (no pmOS_boot partition: the kernel is in the BOOT FIT)"
# pmbootstrap also leaves the file owned by the build user; make it root's
F="$(mktemp)"
debugfs -R "cat /etc/fstab" "$O/samsung-a5y17lte-root.img" 2>/dev/null | grep -v ' /boot ' > "$F"
debugfs -w -f - "$O/samsung-a5y17lte-root.img" >/dev/null 2>&1 <<EOF
rm /etc/fstab
write $F /etc/fstab
sif /etc/fstab uid 0
sif /etc/fstab gid 0
sif /etc/fstab mode 0100644
EOF
rm -f "$F"
# No RTC driver yet: device-samsung-a5y17lte-openrc enables swclock, but
# postmarketos-base-openrc's post-install runs after it and adds hwclock back
debugfs -w -R "rm /etc/runlevels/boot/hwclock" "$O/samsung-a5y17lte-root.img" >/dev/null 2>&1
# Same ordering trap for resolv.conf: the stub 127.0.0.53 has no resolver behind
# it under OpenRC (EVIDENCE 26); give it real nameservers (the device package's
# post-install does that too, but base's can run after it)
if debugfs -R "cat /etc/resolv.conf" "$O/samsung-a5y17lte-root.img" 2>/dev/null | grep -q '^nameserver 127\.0\.0\.53'; then
	F="$(mktemp)"; printf 'nameserver 1.1.1.1\nnameserver 9.9.9.9\n' > "$F"
	debugfs -w -f - "$O/samsung-a5y17lte-root.img" >/dev/null 2>&1 <<EOF
rm /etc/resolv.conf
write $F /etc/resolv.conf
sif /etc/resolv.conf uid 0
sif /etc/resolv.conf gid 0
sif /etc/resolv.conf mode 0100644
EOF
	rm -f "$F"
fi
debugfs -R "cat /etc/resolv.conf" "$O/samsung-a5y17lte-root.img" 2>/dev/null
debugfs -R "ls /etc/runlevels/default" "$O/samsung-a5y17lte-root.img" 2>/dev/null | tr -s ' ' '\n' | grep usbnet || echo "WARNING: a5y17lte-usbnet not in the default runlevel"
# Phosh (EVIDENCE 32): the device package's post-install enables elogind, polkit and
# greetd and drops the tty1 getty, but postmarketos-base's can run after it (same
# ordering trap as hwclock/resolv.conf): check the image, fix what is missing
IMG="$O/samsung-a5y17lte-root.img"
for svc in elogind polkit greetd a5y17lte-usbnet a5y17lte-keyled; do
	if ! debugfs -R "stat /etc/runlevels/default/$svc" "$IMG" 2>&1 | grep -q "Inode:"; then
		echo "runlevel default lacks $svc: adding"
		debugfs -w -R "symlink /etc/runlevels/default/$svc /etc/init.d/$svc" "$IMG" >/dev/null 2>&1
	fi
done
if debugfs -R "cat /etc/inittab" "$IMG" 2>/dev/null | grep -q '^tty1::respawn:'; then
	echo "inittab still has the tty1 getty: commenting it out"
	F="$(mktemp)"
	debugfs -R "cat /etc/inittab" "$IMG" 2>/dev/null | sed 's|^\(tty1::respawn:\)|#a5y17lte (greetd owns tty1) \1|' > "$F"
	debugfs -w -f - "$IMG" >/dev/null 2>&1 <<EOF
rm /etc/inittab
write $F /etc/inittab
sif /etc/inittab uid 0
sif /etc/inittab gid 0
sif /etc/inittab mode 0100644
EOF
	rm -f "$F"
fi
for f in /etc/greetd/config.toml /etc/phosh/phoc.ini /usr/bin/a5-phosh-session; do
	debugfs -R "stat $f" "$IMG" 2>&1 | grep -q "Inode:" || echo "WARNING: $f missing from the image"
done
debugfs -R "ls /etc/runlevels/default" "$IMG" 2>/dev/null | tr -s ' ' '\n' | grep -E 'elogind|polkit|greetd|usbnet|dbus' | tr '\n' ' '; echo
debugfs -R "cat /etc/inittab" "$IMG" 2>/dev/null | grep tty1
# The kernel lives in the BOOT partition's FIT, and pmbootstrap --split moves /boot out
# of the rootfs, so /boot is empty. postmarketos-mkinitfs (not removable: base needs
# it) runs from apk triggers and boot-deploy aborts the whole apk transaction with
# "No kernel found in /boot". An empty /boot/vmlinuz is enough for it to succeed
# (it then writes an unused /boot/initramfs). EVIDENCE 34.
if ! debugfs -R "stat /boot/vmlinuz" "$IMG" 2>&1 | grep -q "Inode:"; then
	echo "adding the placeholder /boot/vmlinuz"
	debugfs -w -f - "$IMG" >/dev/null 2>&1 <<EOF
write /dev/null /boot/vmlinuz
sif /boot/vmlinuz uid 0
sif /boot/vmlinuz gid 0
sif /boot/vmlinuz mode 0100644
EOF
fi
debugfs -R "stat /boot/vmlinuz" "$IMG" 2>&1 | grep -q "Inode:" || echo "WARNING: /boot/vmlinuz missing from the image"
# boot-deploy also looks for the device tree blob in /boot/dtbs (EVIDENCE 43): empty placeholder
if ! debugfs -R "stat /boot/dtbs/exynos/exynos7880-a5y17lte.dtb" "$IMG" 2>&1 | grep -q "Inode:"; then
	echo "adding the placeholder /boot/dtbs/exynos/exynos7880-a5y17lte.dtb"
	debugfs -w -f - "$IMG" >/dev/null 2>&1 <<EOF
mkdir /boot/dtbs
mkdir /boot/dtbs/exynos
write /dev/null /boot/dtbs/exynos/exynos7880-a5y17lte.dtb
sif /boot/dtbs/exynos/exynos7880-a5y17lte.dtb uid 0
sif /boot/dtbs/exynos/exynos7880-a5y17lte.dtb gid 0
sif /boot/dtbs/exynos/exynos7880-a5y17lte.dtb mode 0100644
EOF
fi
# sleep-inhibitor cannot run without CONFIG_SUSPEND (EVIDENCE 37): no runlevel link
if debugfs -R "stat /etc/runlevels/default/sleep-inhibitor" "$IMG" 2>&1 | grep -q "Inode:"; then
	echo "removing sleep-inhibitor from the default runlevel"
	debugfs -w -R "rm /etc/runlevels/default/sleep-inhibitor" "$IMG" >/dev/null 2>&1
fi
debugfs -R "ls /etc/runlevels/boot" "$O/samsung-a5y17lte-root.img" 2>/dev/null | tr -s ' ' '\n' | grep -E 'clock' || true
debugfs -R "cat /etc/fstab" "$O/samsung-a5y17lte-root.img" 2>/dev/null
e2fsck -fn "$O/samsung-a5y17lte-root.img" | tail -1
( cd "$O" && sha256sum samsung-a5y17lte-root.img | tee samsung-a5y17lte-root.img.sha256 )
ls -l "$O"
