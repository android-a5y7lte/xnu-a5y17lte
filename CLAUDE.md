# Galaxy A5 2017 (a5y17lte, Exynos 7880): mainline Linux / postmarketOS

Owner: Jimmy (GitHub android-a5y7lte). Read README.md (status) and
EVIDENCE.md (every hardware finding, in order) before changing anything.

## State (2026-10-19)
- Boot chain: Samsung S-Boot → mainline U-Boot v2026.10 → Linux v7.2 + our
  patches (mainline/patches) and DT (mainline/dts), FIT inside boot.img ramdisk.
- Passed on the phone: M1 console on screen, M2 keys, M3 eMMC read-only,
  M4 USB networking (NCM+ACM; phone 172.16.42.1, PC gets 172.16.42.2 by DHCP;
  `ssh root@172.16.42.1`, blank password; `scp -O` works; telnet on 23).
- M5a passed: distro kernel config, all partitions read-only (force_ro), SYSTEM = mmcblk0p19 (4848 MiB).
- Latest image: out/a5y17lte-mainline-m5a-boot.img (built by build-mainline.sh,
  reproducible: same commit -> same bytes).
- The MUIC (S2MU004, i2c-gpio on gpc1-0/1, addr 0x3d) must be switched to USB
  by `a5-diag muic` or the PC sees nothing (EVIDENCE 18c/18d).

## Next milestone: M5 postmarketOS install
- Target: SYSTEM partition (Jimmy's choice, 2026-10-19). Sailfish OS is
  installed (rootfs in USERDATA); Jimmy has the Sailfish boot.img and the
  original install files, and said it is fine if Sailfish breaks: he can
  reinstall. Still: don't touch USERDATA, and ask before each eMMC write.
- Start console-only (SSH + apk), UI (Phosh/Plasma Mobile) after touchscreen.

### Where we are (end of 2026-10-19)
- M5a PASSED on the phone (EVIDENCE 19a): SYSTEM = mmcblk0p19, 5083496448 B;
  EFS = mmcblk0p3, 20971520 B; USERDATA = mmcblk0p25.
- M5b rootfs (first build, 686817280 B, sha256 06e88091addeafbb…, written in
  M5c; superseded by the M5e rootfs below): ./build-pmos.sh -> ext4
  "pmOS_root", e2fsck clean, OpenRC (pinned: pmOS console UI now defaults to
  systemd), sshd + unudhcpd.usb0 + nftables, getty on ttyGS0, user "user" with
  ~/.ssh/id_ed25519.pub, password 147147 (placeholder; change with passwd).
  os-release says "Nura edge".
- M5c steps 1-2 DONE (TWRP, read-only): by-name
  /dev/block/platform/13540000.dwmmc0/by-name/{EFS,SYSTEM} -> p3/p19, sizes
  match. Backups in ~/a5-backup (NOT in git; EFS has the IMEI):
  EFS-p3.img sha256 5efb6122…, SYSTEM-p19.img sha256 17530cf1… (full list in
  ~/a5-backup/SHA256SUMS; phone and PC hashes match; e2fsck -fn clean).
- M5c step 3 DONE (EVIDENCE 21): root.img written to by-name/SYSTEM; read-back
  sha256 = 06e88091…, e2fsck 1.47 clean on the PC (TWRP's e2fsck 1.43 can't:
  orphan_file). EFS unchanged at 5f6cfe57… (it had changed before the write:
  modem nv_data, presumably a Sailfish boot; second backup
  EFS-p3-20261019-1440.img). TWRP mounts SYSTEM (/system_root) and EFS at
  startup, so later SYSTEM hashes change; check with e2fsck instead.
- M5e rootfs ON SYSTEM now (EVIDENCE 23, 23a): build/pmos/samsung-a5y17lte-root.img,
  688914432 B, sha256 bac1e0c6…, device-samsung-a5y17lte 3-r2, kernel pkg 7.2-r1.
  Written in M5e step 2b, read back and fsck'd clean. Restore point for the M5c
  rootfs: ~/a5-backup/SYSTEM-p19-pmos-m5c.img (first 686817280 B, sha256
  06e88091…, same as the M5c root.img).
- Rollback: `adb exec-in "dd of=<by-name>/SYSTEM bs=4M" < ~/a5-backup/SYSTEM-p19.img`
  (5 GB, won't fit in TWRP /tmp), then check for sha256 17530cf1….
- android-w0 merged into main (d3b6283).
- M5d PASSED (EVIDENCE 22a): Nura boots from SYSTEM read-only under a tmpfs overlay.
- M5e PASSED on the phone (EVIDENCE 24, 24b): out/a5y17lte-mainline-m5e-boot.img,
  sha256 e38f49cd… (commit 66ba460 fixed lock-except). Nura runs read-write on
  SYSTEM (mmcblk0p19) only; the other 24 partitions stay read-only, but the raw
  /dev/mmcblk0 node is writable then (no flag can avoid that). A 50 MB file
  (sha256 7558406d…f3b8) survived a hard reboot; SYSTEM pulled to the PC:
  e2fsck -fy clean. Known bug: `sudo reboot` in Nura hangs on the Samsung logo
  (recovery forces a reboot). TWRP automounts EFS, so its hash changes (now
  a65f32b8…) without Nura touching it.
- M5e steps 1-4 ALL DONE (EVIDENCE 23-25b): SYSTEM grown online from 168192 to
  1241088 blocks (4848 MiB), df -h / = 4.6G, 12% used; after a reboot the PC's
  e2fsck -fn on the pulled SYSTEM is completely clean (6802/532608 files,
  176027/1241088 blocks). resize2fs can't run on the live root (EBUSY:
  CONFIG_BLK_DEV_WRITE_MOUNTED is off), so use mainline/tools/ext4-grow.c
  (ioctl on the mount point; static binary build/ext4-grow-aarch64, not in git).
- Fixed bug: `sudo reboot` in Nura hung on the Samsung logo (repeatable);
  workaround: Volume Down + Power to force off, then power on. ROOT CAUSE FOUND
  (EVIDENCE 29, 2026-10-27): any DRAM write at 0x48000000-~0x48016000 makes the next
  plain reset hang S-Boot. Fix: DT reserved-memory sboot-reset@48000000 (128 KiB)
  and the kernel loaded at 0x48200000. m5f (out/a5y17lte-mainline-m5f-boot.img) PASSED:
  `sudo reboot` in Nura restarts cleanly. FIXED; the workaround is no longer needed.
- M5e step 5 PASSED (EVIDENCE 26): internet via PC NAT. PC: ip_forward, iptables
  MASQUERADE 172.16.42.0/24 -> wlp2s0 plus DOCKER-USER ACCEPTs for
  enx02a517000002 (Docker's FORWARD is DROP). Phone, RAM only, redo each boot:
  `ip route add default via 172.16.42.2 dev usb0`, a resolv.conf with
  nameserver 1.1.1.1 bind-mounted over /etc/resolv.conf (systemd-resolved stub
  conflict), clock via `date -u -s` (no RTC). `apk update` OK, 37182 packages.
  M5e (all 5 steps) is COMPLETE.
- Route/DNS/clock PERSISTENT (EVIDENCE 30, 2026-10-27): package 3-r3 has the
  a5y17lte-usbnet service (default route via the PC on usb0), real nameservers
  in resolv.conf; swclock + chronyd set the clock once the network works. Applied
  live on the phone and verified over `sudo reboot`; the PC's iptables NAT is
  still redone by hand after a PC reboot. Log in as `user` (sudo password
  147147), root SSH is off; no scp (use `ssh ... 'cat > f'`).
- NEXT (not started): touchscreen -> Phosh (roadmap below).

## Roadmap after M5 (Jimmy, 2026-10-19)
Two OSes on ONE mainline kernel + DT: Nura/postmarketOS (primary, on SYSTEM)
and, later, open-source Android (AOSP) — no Samsung vendor blobs, which only
work with Samsung's 3.18 kernel.
- Shared kernel milestones, in order: touchscreen → Phosh on Nura; real 7880
  CMU clock driver; display (DECON/DSI, mandatory for Android's
  drm_hwcomposer); GPU (Mali-T830 via Panfrost, after G3D power domain, clock
  and vdd_g3d — MrLu1z's gpu-test branch has the node values but does not
  build and lacks power-domain/clock/regulator); battery, audio, Wi-Fi.
- Then binder/Android kernel options → Waydroid on Nura (first Android apps).
- Then a GloDroid-style AOSP device for a5y17lte (Mesa/Panfrost,
  drm_hwcomposer, minigbm) on the same kernel, on a partition/SD card that
  does not replace Nura on SYSTEM.
- Known limits for both OSes: no modem (Shannon, no mainline driver → no
  calls/SMS/data) and no camera, for now.

## Rules
- Test images keep the eMMC read-only (/sys/block/mmcblk0/force_ro = 1;
  BLKROSET alone never covered the partitions, EVIDENCE 19). Any eMMC write
  needs Jimmy's explicit OK first.
- Never touch ~/hadk or ~/sfos (Sailfish build trees).
- Work in small milestones; each one is flashed and tested on the phone
  (TWRP → Install Image → Boot), with a photo/log recorded in EVIDENCE.md.
- Kernel patches: `Assisted-by: Claude:<model>` trailer, no AI Signed-off-by
  (Documentation/process/coding-assistants.rst); run checkpatch.
- Commits: author Jimmy <fakeandroit@gmail.com>.
- iOS/XNU direction is closed; don't resume it. uniLoader forbids AI agents
  (its AGENTS.md); don't modify it.

## Build
./setup.sh (clones upstream/: linux, u-boot, k7880 reference, xPack GCC,
unicorn, pmaports, pmbootstrap), then ./build-userspace.sh and
./build-mainline.sh (boot image, reproducible), then ./build-pmos.sh (pmOS
rootfs; needs sudo). U-Boot must be linked with GNU ld (xPack), not ld.lld.
