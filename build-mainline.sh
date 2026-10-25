#!/usr/bin/env bash
# Galaxy A5 (2017) mainline Linux (current milestone: M5, postmarketOS).
# Builds: Linux 7.2 Image + A5 device tree, nolibc init + initramfs,
# U-Boot (a5y17lte_defconfig, position independent), a FIT payload, and the
# boot image S-Boot loads:  kernel = u-boot.bin, ramdisk = FIT.
# Everything is built with LLVM (clang, ld.lld, llvm-*).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LINUX="$ROOT/upstream/linux"
UBOOT="$ROOT/upstream/u-boot"
ML="$ROOT/mainline"
B="$ROOT/build"
OUT="$ROOT/out"
mkdir -p "$B/fit" "$OUT"
JOBS="$(nproc)"
IMG="a5y17lte-mainline-${MILESTONE:-m5e}-boot.img"

# Reproducible: the same commit gives the same image bytes on any machine.
# Time = last commit that changed a build input, so committing the image and
# notes doesn't change it (override with SOURCE_DATE_EPOCH); fixed user/host.
export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-$(git -C "$ROOT" log -1 --format=%ct -- . \
	':(exclude)out' ':(exclude)evidence' ':(exclude)*.md')}"
export TZ=UTC LC_ALL=C
export KBUILD_BUILD_TIMESTAMP="$(date -d "@$SOURCE_DATE_EPOCH")" KBUILD_BUILD_VERSION=1
export KBUILD_BUILD_USER=a5y17lte KBUILD_BUILD_HOST=a5y17lte
# release = 7.2.0 + CONFIG_LOCALVERSION, the same as the pmOS kernel package
# (an empty LOCALVERSION stops setlocalversion adding "+" for the git tree)
export LOCALVERSION=

for t in clang ld.lld llvm-objcopy flex bison python3 gzip; do
	command -v "$t" >/dev/null || { echo "missing tool: $t"; exit 1; }
done

# LLVM stand-ins for the aarch64-linux-gnu- prefix U-Boot expects
LC="$ROOT/upstream/tools/llvm-cross"
mkdir -p "$LC"
for t in ar nm objcopy objdump strip readelf ranlib; do ln -sf "$(command -v llvm-$t)" "$LC/aarch64-linux-gnu-$t"; done
ln -sf "$(command -v ld.lld)" "$LC/aarch64-linux-gnu-ld"
ln -sf "$(command -v ld.lld)" "$LC/aarch64-linux-gnu-ld.bfd"
export PATH="$LC:$PATH"

echo "== Linux: device tree + config + Image"
# Kernel driver changes live as patches (git am onto v7.2, branch a5y17lte);
# device trees are copied from mainline/dts so they stay easy to read.
if [ "$(git -C "$LINUX" rev-parse --abbrev-ref HEAD)" != a5y17lte ]; then
	git -C "$LINUX" checkout -q -B a5y17lte v7.2 2>/dev/null || git -C "$LINUX" checkout -q -B a5y17lte
	for p in "$ML"/patches/*.patch; do
		[ -e "$p" ] && git -C "$LINUX" -c user.name=a5y17lte -c user.email=a5y17lte@localhost am -q "$p"
	done
fi
cp "$ML"/dts/exynos7880*.dtsi "$ML"/dts/exynos7880-a5y17lte.dts "$LINUX/arch/arm64/boot/dts/exynos/"
grep -q exynos7880-a5y17lte "$LINUX/arch/arm64/boot/dts/exynos/Makefile" || \
	sed -i 's/^\(\texynos7870-on7xelte.dtb\t\t\\\)$/\1\n\texynos7880-a5y17lte.dtb\t\t\\/' \
		"$LINUX/arch/arm64/boot/dts/exynos/Makefile"
KM=(make -C "$LINUX" ARCH=arm64 LLVM=1)
KCONFIG_ALLCONFIG="$ML/config/a5y17lte.config" "${KM[@]}" allnoconfig >/dev/null
"${KM[@]}" -j"$JOBS" Image exynos/exynos7880-a5y17lte.dtb headers usr_gen_init_cpio >/dev/null
cp "$LINUX/arch/arm64/boot/Image" "$LINUX/arch/arm64/boot/dts/exynos/exynos7880-a5y17lte.dtb" "$B/fit/"

echo "== userspace (busybox, dropbear, a5-diag) + initramfs"
[ -x "$ROOT/upstream/userspace/out/busybox" ] || "$ROOT/build-userspace.sh"
"$ROOT/upstream/userspace/bin/aarch64-musl-cc" -Os -Wall -Werror \
	-o "$B/a5-diag" "$ML/init/a5-diag.c"
llvm-strip "$B/a5-diag"
"$ROOT/upstream/userspace/out/busybox" sh -n "$ML/rootfs/init" 2>/dev/null || true
sed -E "s#^(file [^ ]+ )([^/ ][^ ]*)#\\1$ROOT/\\2#" "$ML/initramfs.list" > "$B/initramfs.list"
"$LINUX/usr/gen_init_cpio" -t "$SOURCE_DATE_EPOCH" "$B/initramfs.list" | gzip -9n > "$B/fit/initramfs.cpio.gz"

echo "== U-Boot (GNU toolchain: U-Boot's arm64 linker script needs GNU ld;
#    ld.lld places .bss at 0 - see EVIDENCE.md section 13)"
XP="$(ls -d "$ROOT"/upstream/tools/xpack/xpack-aarch64-none-elf-gcc-*/bin 2>/dev/null | head -1)"
[ -x "$XP/aarch64-none-elf-gcc" ] || { echo "missing xPack toolchain, run ./setup.sh"; exit 1; }
rm -rf "$B/u-boot"
UM=(make -C "$UBOOT" O="$B/u-boot" HOSTCC=clang CROSS_COMPILE="$XP/aarch64-none-elf-")
"${UM[@]}" a5y17lte_defconfig >/dev/null
"$UBOOT/scripts/config" --file "$B/u-boot/.config" \
	--enable POSITION_INDEPENDENT --disable TOOLS_MKEFICAPSULE
# test images (BOOTCMD set) need "dcache off" to flush framebuffer writes
[ -z "${BOOTCMD:-}" ] || "$UBOOT/scripts/config" --file "$B/u-boot/.config" --enable CMD_CACHE
"${UM[@]}" olddefconfig >/dev/null
"${UM[@]}" -j"$JOBS" u-boot.bin tools >/dev/null

echo "== FIT payload"
cp "$ML/a5y17lte.its" "$B/fit/"
cp "$ML/${BOOTCMD:-boot.cmd}" "$B/fit/boot.cmd"
# STAGE=N: boot with a5.stage=N added to the DT bootargs (init stops after step N)
if [ -n "${STAGE:-}" ]; then
	BA="$(sed -n 's/^[[:space:]]*bootargs = "\(.*\)";/\1/p' "$ML/dts/exynos7880-a5y17lte.dts" | head -1)"
	sed -i "s|^bootm |setenv bootargs \"$BA a5.stage=$STAGE\"\nbootm |" "$B/fit/boot.cmd"
fi
# mkimage runs "dtc" from PATH (compile-time MKIMAGE_DTC); use the one U-Boot built
ln -sf "$B/u-boot/scripts/dtc/dtc" "$LC/dtc"
( cd "$B/fit" && "$B/u-boot/tools/mkimage" -f a5y17lte.its "$B/a5y17lte.fit" >/dev/null )
"$B/u-boot/tools/mkimage" -l "$B/a5y17lte.fit"

echo "== boot image (docs/board/samsung/axy17lte.rst values)"
python3 "$ROOT/tools/mkbootimg_v0.py" pack \
	--kernel "$B/u-boot/u-boot.bin" --ramdisk "$B/a5y17lte.fit" \
	--base 0x40000000 --kernel_offset 0x00000000 --ramdisk_offset 0x01000000 \
	--tags_offset 0x00000100 --second_offset 0x00f00000 --pagesize 2048 \
	-o "$OUT/$IMG"
python3 "$ROOT/tools/mkbootimg_v0.py" info "$OUT/$IMG"
( cd "$OUT" && sha256sum "$IMG" | tee "$IMG.sha256" )
