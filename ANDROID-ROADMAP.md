# Open Android on a5y17lte: plan (draft 2026-10-19)

Two Android paths on the SAME mainline kernel + DT as Nura (CLAUDE.md,
"Roadmap after M5"): Waydroid inside Nura first, then a GloDroid-style AOSP
device. No Samsung vendor blobs. Device firmware loaded into a chip (Wi-Fi/BT,
maybe touch) is a separate question and is listed per item.

Starts after M5d (Nura boots from SYSTEM). Until then this file is planning
only: it does not touch mainline/config, the phone or the eMMC, which the
Nura session owns.

Milestone naming: K = shared kernel, W = Waydroid, A = AOSP. Every milestone
ends with a phone test recorded in EVIDENCE.md, like M1-M5.

## What we start from (EVIDENCE.md)
- Linux 7.2, all built in (CONFIG_MODULES=n). Already in the M5 config: PSI,
  cgroups (pids, freezer, device, cpuacct, bpf, blk), namespaces, SHMEM,
  nftables with NAT/masquerade. Missing: binder, any DRM driver, SELinux.
- Display = simple-framebuffer at 0x67000000, 1080x1920 ARGB8888, command-mode
  panel refreshed by DECON HW/TE trigger (HW_SW_TRIG_CONTROL = 0x10, sec. 15).
- Clocks are fixed-clock stand-ins with measured rates (sec. 17b, 18a).
  I2C only through i2c-gpio (MUIC on gpc1-0/1); no CMU_PERI driver yet.
- CPU: Cortex-A53, ID_AA64MMFR0 TGran16 = 0 -> no 16 KB pages (sec. 12).
  Any Android build must stay on 4 KB pages.

## Order, and why Waydroid can start early
Waydroid needs only binder + a Wayland compositor + a DRM device. With
simpledrm on the boot framebuffer and software rendering it can run long
before touch, DECON or Panfrost exist. So:

W0 -> W1 (headless) -> K1 touch -> W2 (on screen, Phosh) -> K2-K5 -> W3 (GPU)
-> A0-A5. K6-K9 (battery, audio, Wi-Fi, SD) slot in whenever convenient; A2+
needs K9 only if AOSP lives on the SD card.

## Phase W: Waydroid on Nura

### W0: kernel options + simpledrm (config only)
- Add: ANDROID_BINDER_IPC, ANDROID_BINDERFS, ANDROID_BINDER_DEVICES="".
  Source of truth for the rest: the `waydroid` (and `containers`) categories
  in upstream/pmaports/kconfigcheck.toml (setup.sh clones it); expect
  BRIDGE, VETH and netfilter extras for waydroid-net. `pmbootstrap kconfig
  check` must pass for those categories.
- Replace FB_SIMPLE with DRM + DRM_SIMPLEDRM + DRM_FBDEV_EMULATION (fbcon
  stays). wlroots/phoc and Waydroid need /dev/dri/card0; simpledrm gives it on
  the same 0x67000000 buffer. It never touches DECON, so the TE refresh set up
  before Linux keeps working. Check that first: it is the main W0 risk.
- Phone test: console still on screen, SSH, keys; `/dev/dri/card0` exists;
  `mount -t binder binder /dev/binderfs` -> binder-control; `/proc/pressure/*`
  readable; `modetest -M simpledrm` lists one 1080x1920 connector.
  Record kernel size against the BOOT partition size.

### W1: Waydroid container, headless
- `apk add waydroid`, `waydroid init` (arm64 LineageOS image, about 1 GB in
  /var/lib/waydroid). SYSTEM root is 686 MB inside a 4848 MiB partition, so
  growing the filesystem (resize2fs) comes first: an eMMC write, ask Jimmy.
- Software rendering: ro.hardware.gralloc=default, ro.hardware.egl=swiftshader
  in waydroid_base.prop.
- Compositor: `weston --backend=headless` (or phoc) as user, then
  `waydroid session start`.
- Phone test: `waydroid shell getprop sys.boot_completed` = 1;
  `waydroid shell screencap -p` copied to the PC as the evidence image.
  This proves binder/binderfs, cgroups and the image on our kernel with no
  display or input work.

### W2: Waydroid on screen (needs K1 + Phosh)
- `waydroid show-full-ui` on Phosh, still software rendered.
- Phone test: launcher visible, touch works, install and run one F-Droid app,
  network through waydroid0 NAT.

### W3: Waydroid with the GPU (needs K4, K5)
- Mesa gralloc/EGL in the container on Panfrost; check that the Waydroid image's
  Mesa still builds Midgard. Phone test: `dumpsys SurfaceFlinger` shows
  Mesa/Panfrost GLES 3.1, scrolling smoother than W2.

## Phase K: shared kernel (Nura and Android)
Each item replaces one stand-in and must keep M1-M5 behaviour.

### K1: touchscreen
- Controller, address, IRQ pin, reset/supply from Samsung
  `exynos7880-a5y17lte_common.dtsi`. Bus: i2c-gpio first (same trick as the
  MUIC), hardware I2C after K2. Firmware: check whether the IC needs a blob.
- Test: `evtest` shows ABS_MT coordinates matching the 1080x1920 panel.

### K2: 7880 CMU driver
- Variant of clk-exynos7870.c: CCORE, MIF0, MIF1, PERI, then G3D and DISPAUD
  (same addresses as 7870 except CCORE/MIF, sec. 13). Swap fixed clocks one
  block at a time, MMC first.
- Test: `clk_summary` rates equal the measured ones (MMC0 CIU 399.75 MHz,
  FSYS_BUS 266.5 MHz, USB 50 MHz); eMMC 82 MiB/s and USB still work.

### K3: PMIC (S2MPU05) regulators
- Needed for vdd_g3d, panel, touch and Wi-Fi supplies. Check what mainline
  already has for the 7870 phones before writing anything.
- Test, read-only first: `regulator_summary` voltages equal the Samsung DT;
  mark every rail always-on until its consumer is verified.

### K4: display (DECON + DSIM + panel)
- Base on Kaustabh Chakraborty's Exynos7870 display stack and the later DSIM
  fixes series (simplefb hand-off, PLL wait, MFLUSH_VS). Panel: command mode
  with TE, from the Samsung DT; panel driver may be new.
- drm_hwcomposer needs: atomic KMS, primary plane (+ DECON windows as overlay
  planes), ARGB/XRGB/RGB565, vblank events from TE.
- Test: `modetest -M exynos` shows planes; `kmscube`; atomic test commit;
  clean simpledrm -> exynos hand-off with fbcon.

### K5: GPU, Mali-T830 via Panfrost
- G3D power domain (PMU), CMU_G3D clock, vdd_g3d (K3), interrupts and OPPs
  from the Samsung DT (fixed frequency first, DVFS later). MrLu1z's gpu-test
  branch: node values only.
- Can be validated before K4: `dmesg` shows panfrost probing a T830;
  `glmark2-es2-drm --off-screen` or `eglinfo` (surfaceless) on Panfrost.
- Mesa: Midgard (T830) = GLES 3.1 / GL 3.1, no Vulkan (PanVK is Bifrost+).

### K6 battery/charger (S2MU004, same chip as the MUIC), K7 audio,
### K8 Wi-Fi/BT, K9 microSD (dwmmc2)
- Identify each IC from the Samsung DT first; Wi-Fi needs a firmware file and
  NVRAM/calibration, which may live in EFS (read-only, never written).

## Phase A: AOSP device "a5y17lte" (GloDroid-style)

### A0: decisions (no phone work)
- Base: GloDroid v2.0 mono-repo (supports PinePhone, PinePhone Pro, RPi 4).
  Model the device on PinePhone Pro: its RK3399 has a Mali-T860, also Midgard
  on Panfrost, so the GLES-only (no Vulkan) Android path is already proven
  there. Check which AOSP version v2.0 is on and whether newer AOSP still runs
  without Vulkan.
- 4 KB page-size build only (A53, sec. 12).
- Storage: microSD (needs K9) is the default: no eMMC writes, removable, does
  not touch Nura on SYSTEM. Alternative: USERDATA, only if Sailfish is
  retired, and only with Jimmy's OK.
- Boot: same U-Boot + kernel + DTB, Android ramdisk + `androidboot.*` in the
  FIT. Test builds flashed to BOOT through TWRP as today; later one image where
  the U-Boot script picks the OS by a held key (Volume Up -> Android).
- Per-OS kernel command line from the boot script: SELinux built in, enabled
  for Android, off for Nura (`lsm=`/`selinux=0`).

### A1: kernel additions for AOSP
- On top of W0: SECURITY_SELINUX, FUNCTIONFS (adb), UHID, UINPUT,
  DMABUF_HEAPS (system + CMA), EROFS and/or F2FS, cgroup v2 + CPUSETS.
  Checklist: AOSP kernel/configs android-base.config for the chosen release
  plus GloDroid's kernel fragment. Nura must still pass its kconfig check.

### A2: AOSP boots to adb (headless)
- Test: `adb shell getprop sys.boot_completed` = 1 over USB.

### A3: UI on simpledrm, software rendering
- drm_hwcomposer on simpledrm (single plane) + SwiftShader/llvmpipe.
- Test: launcher on screen, touch (K1) works. Slow is fine.

### A4: real display + GPU (needs K4, K5)
- drm_hwcomposer on DECON, Mesa Panfrost EGL/GLES, minigbm (gbm_mesa).
- Test: `dumpsys SurfaceFlinger` shows Panfrost; overlay planes used.

### A5: hardware HALs
- Generic, blob-free: health (power_supply sysfs, K6), audio (ALSA/tinyalsa,
  K7), Wi-Fi (nl80211 + wpa_supplicant, K8), lights/vibrator, sensors if any.

## Known limits
No modem (Shannon), no camera; also expect no hardware video decode for now
(MFC not looked at), no fingerprint.

## Main risks
1. simpledrm keeping the command-mode panel refreshing (W0).
2. Command-mode + TE support in the Exynos DRM DSIM path (K4).
3. AOSP's dependence on Vulkan in newer releases (A0).
4. Size: kernel with DRM + SELinux + Android options against the BOOT partition.
