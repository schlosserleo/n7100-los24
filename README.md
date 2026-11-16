# LineageOS 24 (Android 17) for the Galaxy Note II (GT-N7100), on mainline Linux

LineageOS 24 running on Linux 7.3.1 with the open-source graphics stack (Mesa lima) on the
Exynos 4412 Prime: no Samsung kernel, no Mali blobs. Samsung's binaries are only used where
there is no open driver: the modem (boot daemon + RIL) and the GPS daemon, both behind HALs
written for this port.

## Status

| | |
|---|---|
| Display, brightness, auto-brightness | works with both Note II panels, each with its own device tree. The flash zip carries the EA8061's only and stops when TWRP reports the S6EVR02 (check with `dmesg \| grep 'panel driver'` in TWRP). For the S6EVR02 (kernel patches 0132-0134) `tools/build-kernel.sh` writes a second kernel image, `prebuilt/kernel/zImage-dtb-s6evr02`; a boot image with it has to be made and flashed by hand for now |
| GPU | works: Mali-400 MP4 with Mesa lima (OpenGL ES 2), drm_hwcomposer, minigbm |
| Touchscreen, S Pen, touch keys | work |
| Audio | works (WM1811: speaker, headphones, microphone; capture runs at the playback rate of 44.1 kHz); for calls see Modem |
| Wi-Fi, Bluetooth | work (BCM4334); Wi-Fi stays connected in deep sleep and wakes the phone for traffic to it (the stock firmware has no WoWLAN: kernel patches 0121 and 0124 do it with packet filters) |
| Modem | XMM6262 boots, baseband and IMEI are reported. With a SIM (tested on UMTS, one operator): calls, SMS and mobile data work. Call audio is only half in the tree: the kernel's voice PCM and the radio HAL's sound requests to the modem are (kernel patches 0136 and 0137), the service that runs that PCM and sets the WM1811 mixer during a call is not yet. In deep sleep the HSIC port loses its power and the modem attaches again on wake-up (kernel patches 0128 and 0140); calls, SMS and alarms wake the phone. A suspend that is aborted still makes the modem link disconnect and enumerate again (1 to 2 s), and a series of them can end in a modem reset (20 s to 2 min offline). Untested: MMS, tethering, a long standby; call hold is refused by the test network |
| GPS | the BCM4752 runs and reports NMEA; a fix outdoors is not verified yet |
| Sensors | work: accelerometer, gyroscope, magnetometer, barometer, light, proximity |
| Rear camera | preview, photos up to 2304x1296 and video (software H.264 encoder: about 11 fps at 960x720 through CameraX); continuous autofocus and tap-to-focus (kernel patches 0119 and 0120) |
| Flashlight, camera flash | the flash LED as a torch (quick settings tile, 8 brightness levels); camera apps get it as the torch and as the flash for photos (a continuous light at torch current, no strobe) |
| Front camera | preview, photos and video up to 1280x720 (16:9, fixed focus), through the FIMC-IS ISP with the stock firmware (kernel patches 0107-0112, Martin Jücker's FIMC-IS fixes); colours are warm under room light (`device/samsung/n7100/camera/README.txt`) |
| Charging, battery, standby | work, with deep sleep; the fuel gauge measures the voltage only (battery level, no current or charge counter); a PC port is reported as USB (kernel patch 0139; a wall charger is untested since); a cable plugged into the switched-off phone boots Android (no charger UI), a reboot with a cable attached goes straight back to Android (kernel patch 0143) |
| Notification LED | works: notifications (with the screen off, as LineageOS does it) and the battery state, in colour; the LED blinks by itself, also in deep sleep |
| Vibration | works, with the amplitude Android asks for (kernel patch 0138: at full strength the PWM was a constant level and the motor didn't move) |
| USB | adb, file transfer (MTP/PTP over FunctionFS) and the USB settings (kernel patch 0106 gives Android its gadget state); the phone stays awake while a computer uses it (kernel patch 0118) |
| Screen mirroring | scrcpy works (video and audio over adb) |
| USB OTG | implemented (role switch through the MAX77693 MUIC, 5 V boost), untested |
| NFC | not possible: the PN544 is an HCI chip, Android's NFC stack only speaks NCI |
| Hardware video decoding | H.264 up to 1080p30 on the MFC v5, through v4l2_codec2 (Codec2) with a decoder for it; FIMC converts its tiled frames. Other codecs and all encoders are software |
| Security | SELinux enforcing, file-based encryption (Adiantum: the Cortex-A9 has no AES instructions) |
| Recovery | LineageOS recovery on the same kernel (adb sideload, reboot), two-stage to fit the 8 MiB partition; see [Installing](#lineageos-recovery) |

CPU at 1.6 GHz (Prime operating points), GPU at 533 MHz, schedutil, a power HAL that raises
CPU/GPU/memory bus floors on touch and app launches, 1 GB of zram swap (lz4).

The bootloader's logo stays on the screen until the boot animation (kernel patches 0145 and
0146). exynos-drm can keep a reserve of cleared display buffers for reuse (kernel patch 0144,
`/sys/module/exynosdrm/parameters/gem_pool`, off by default; nothing sets it yet).

The display controller (FIMD) composes the screen from hardware planes but can't scale them, so
the wallpaper is kept at the screen's height: a 1440x1280 crop of LineageOS' default, no zoom
behind the shade and the app drawer, and the image wallpaper drawn to fill the display
(`patches/frameworks_base-imagewallpaper-fill-display.patch`). A scaled wallpaper sent every frame
with it on screen to GPU composition, on top of the apps' own rendering.

## Installing

Needs the stock bootloader (sboot) and, for the first installation, TWRP in the recovery
partition (3.7.0 tested); the partition table stays Samsung's.

1. Boot TWRP (Volume Up + Home + Power).
2. Install `lineage-24.0-<date>-UNOFFICIAL-n7100.zip`. Coming from stock or LineageOS 21, the
   zip clears /data and /metadata (Android formats and encrypts them on first boot); an update
   of this build keeps the data.
3. Reboot. The first boot takes a few minutes.

No GApps are included.

### LineageOS recovery

With the ROM installed, `lineage-24.0-<date>-recovery-n7100.img` can replace TWRP: flash it to
the recovery partition from TWRP (Install → Install Image → Recovery) or with Heimdall
(`heimdall flash --RECOVERY <img>`). It installs zips with `adb sideload` (Apply update → Apply
from ADB), checks their signature and reboots; there are no backups and no file manager.

The recovery partition has 8 MiB: enough for the kernel, not for the recovery ramdisk next to it.
So the image holds the kernel and a small loader (`device/samsung/n7100/recovery/loader.c`), and
the ramdisk is in the vendor partition (`/vendor/etc/recovery/ramdisk.cpio`), where the loader
unpacks it from. It only works with this ROM installed, and every later zip of the ROM updates
it; a TWRP in the recovery partition stays.

## Building

Linux (or WSL2) with the usual LineageOS build requirements, clang/lld for the kernel, Rust with
the `armv7-unknown-linux-musleabihf` target for n7100tool.

```bash
# 1. Samsung blobs and firmware, from LineageOS 21 for the n7100 and the stock N7100XXUFNE1 system
#    (a TWRP backup or an unpacked /system); checked against device/samsung/n7100/proprietary/blobs.sha1
tools/extract-blobs.sh 21.0-20250120-HTML6405-n7100.zip <stock system>

# 2. Kernel: Linux 7.3.1 + kernel/patches, into device/samsung/n7100/prebuilt
#    (kernel/zImage-dtb for the EA8061 panel, kernel/zImage-dtb-s6evr02 for the S6EVR02)
git clone -b v7.3.1 --depth 1 https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git
tools/build-kernel.sh linux

# 3. LineageOS 24 (lineage-24.0): applies patches/, builds, packages the flash zip into out/
repo init -u https://github.com/LineageOS/android.git -b lineage-24.0 --git-lfs && repo sync
tools/build-android.sh <lineage tree> out
```

`N7100_DEVELOPMENT=true tools/build-android.sh ...` builds a debuggable userdebug that authorises
the adb key in `device/samsung/n7100/adb_keys` (not in the repository) and stays awake;
`tools/twrp-flash.sh` writes the images over adb from TWRP.

## Layout

- `kernel/` — `base.config` (postmarketOS' linux-postmarketos-exynos4), the Android and device
  fragments, and the patch series on top of v7.3.1: device tree, display panels, sensor hub,
  touch, S Pen, charger, modem (Samsung's modem_if ported), USB, and fixes.
- `device/samsung/n7100/` — the LineageOS device tree, including the HALs written for this port:
  radio (AIDL host for Samsung's libsec-ril), GNSS (around Samsung's legacy GPS HAL and gpsd),
  camera (from AOSP's external camera HAL), power, sensors, lights, vibrator. Each has its notes.
- `patches/` — changes to LineageOS repositories (`external/minigbm`, `external/v4l2_codec2`,
  `frameworks/av`, `packages/modules/Connectivity`, `vendor/aospext`); `patches/development/` only
  for development builds.
- `tools/` — extracting blobs, building, the flash zip; `tools/development/` debugging aids.
- `n7100tool/` — keeps the hardware watchdog fed.

## Credits

The mainline Exynos 4 work this builds on, in particular by Artur Weber, Henrik Grimler, Simon
Shields, Martin Jücker and the postmarketOS community; LineageOS and AOSP; GloDroid's aospext,
drm_hwcomposer and minigbm; Mesa's lima driver.

## License

The kernel patches are GPL-2.0 like the kernel. The device tree and tools are Apache-2.0 unless a
file says otherwise. Samsung's binaries are not part of this repository.
