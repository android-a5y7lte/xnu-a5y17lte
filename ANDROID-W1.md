# W1: Waydroid headless on Nura (brief for the PC Claude Code session)

Goal: Android (Waydroid, LineageOS arm64 image, software rendering) boots in
its container on Nura and reports `sys.boot_completed = 1`, with no screen,
touch or GPU involved. Evidence: command output + one screencap, recorded as
the next EVIDENCE.md section.

## Starting point (CLAUDE.md on main, 2026-10-20)
- Nura (postmarketOS edge, OpenRC, console UI) runs read-write from SYSTEM
  (mmcblk0p19, 4.6 GB, about 4 GB free), booted with the M5e image.
- Internet only through the PC: route, DNS and clock must be redone after each
  boot (CLAUDE.md, M5e step 5). A wrong clock breaks TLS downloads.
- android-w0 (binder, binderfs, LXC/bridge/NAT options, simpledrm) is merged
  into main. Whether the M5e boot image already contains it is the first check.
- Known bug: `reboot` hangs on the Samsung logo. Do not reboot in this task;
  if it is unavoidable, Volume Down + Power forces off.
- 2.5 GB RAM, 8 Cortex-A53, 4 KB pages, no GPU driver, no modules (all built in).

## Rules for this task
- No flashing, no TWRP, no writes outside SYSTEM (everything below happens
  inside Nura's own filesystem), no USERDATA, no reboot.
- Stop and report on: any hang or panic, a write to anything but SYSTEM,
  free space under 1.5 GB, an unexpected kernel oops. Do not retry reboots.
- Record each step's output; if a step fails, record that too.

## Steps
0. Read-only checks on the phone, report before going on:
   - `zcat /proc/config.gz | grep -E 'ANDROID_BINDER|BINDERFS|VETH|BRIDGE|SQUASHFS|BLK_DEV_LOOP|TUN=|PSI='`
     (the W0b config: BINDER_DEVICES="binder,hwbinder,vndbinder", all =y).
     If absent, the running boot image predates the merge: stop and tell Jimmy
     (a boot-image flash needs his OK).
   - `grep binder /proc/filesystems`, `ls /dev/dri`, `df -h /`, `free -m`.
   - network: `ip route`, `date`, `ping -c1 1.1.1.1`, `wget -qO- https://ota.waydro.id/system | head -c 200`.
1. `apk add waydroid waydroid-openrc weston` (plus `cage` if weston fails).
   Note which packages pull in `kmod`/modprobe config; our kernel has no
   modules, so a `modprobe binder_linux` in Waydroid's scripts can fail
   harmlessly or fatally. Record it.
2. `waydroid init` (default system and vendor OTA, arm64). If it refuses
   because binder is missing, mount binderfs at /dev/binderfs and symlink
   /dev/binder, /dev/hwbinder, /dev/vndbinder to its nodes, then retry.
3. Software rendering: in /var/lib/waydroid/waydroid.cfg, `[properties]`,
   add `ro.hardware.gralloc=default` and `ro.hardware.egl=swiftshader`;
   run `waydroid upgrade -o`.
4. Start services: `rc-service waydroid-container start` (OpenRC), check
   `lxc-ls -f` / `waydroid status`, `dmesg | tail` for binder or LXC errors.
5. Headless compositor as an ordinary user (not root), for example
   `XDG_RUNTIME_DIR=/run/user/1000 weston --backend=headless --width=1080 --height=1920 --socket=wayland-1 &`.
   Untested whether Waydroid accepts it. Fallback:
   `WLR_BACKENDS=headless WLR_RENDERER=pixman cage -- sleep infinity`.
6. `WAYLAND_DISPLAY=wayland-1 waydroid session start &` as that user.
   Wait up to 10 minutes (2.5 GB RAM, 8 slow cores, software rendering).
7. Success: `waydroid shell getprop sys.boot_completed` prints 1.
   Then `waydroid shell screencap -p > /tmp/w1.png`, copy it to the PC with
   `scp -O`, and note `free -m`, `uptime`, boot time.
8. EVIDENCE.md: next section number, "W1", with what worked, what needed
   workarounds (exact commands), and the failure points. Commit on a branch
   `android-w1`, author Jimmy <fakeandroit@gmail.com>.

## Likely trouble, in order
1. No `/dev/binder`: Waydroid 1.4.x should mount binderfs itself; step 2 has the fallback.
2. LXC/cgroup mount layout under OpenRC (unified vs hybrid), cgroup v2 controllers missing.
3. waydroid-net (bridge `waydroid0`, dnsmasq, iptables-legacy vs nft): the
   container may start but have no network; boot_completed does not need it.
4. Not enough RAM: Android plus Nura on 2.5 GB. zram is on.
5. The headless compositor is rejected: try the other one.
