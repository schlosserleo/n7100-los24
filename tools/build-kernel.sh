#!/bin/bash
# Build the kernel: Linux 7.3.1 + kernel/patches, configured from kernel/base.config (postmarketOS'
# linux-postmarketos-exynos4) and the fragments, and install it into the device tree's prebuilt/:
# zImage with the N7100 DTB appended (sboot knows no device tree), modules, the netfilter list.
#
# usage: tools/build-kernel.sh <linux>
#   linux  a linux-stable checkout at v7.3.1 (git clone -b v7.3.1 --depth 1
#          https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git); the patches are
#          applied with git am if they aren't yet
# needs: clang/lld (LLVM=1), the usual kernel build dependencies
set -euo pipefail
R=$(realpath "$(dirname "$0")/..")
K=$(realpath "$1")
DT=$R/device/samsung/n7100
P=$DT/prebuilt
cd "$K"

if ! git log --format=%s | grep -qx "ARM: dts: n710x: quiet boot, no charger mode"; then
    git -c user.name=build -c user.email=build@localhost am "$R"/kernel/patches/0*.patch
fi

M="make ARCH=arm LLVM=1 -j$(nproc) KBUILD_BUILD_USER=n7100 KBUILD_BUILD_HOST=build"
cp "$R/kernel/base.config" .config
ARCH=arm ./scripts/kconfig/merge_config.sh -m .config "$R/kernel/android-base.fragment" \
    "$R/kernel/n7100-android.fragment" "$R/kernel/n7100-slim.fragment" > /dev/null
$M olddefconfig > /dev/null
$M zImage dtbs modules

# sboot loads the ramdisk at 0x42000000; the decompressor moves the zImage behind the end of
# the decompressed image, so both together must stay below ~33.5 MB from 0x40008000
size=$(($(stat -c %s arch/arm/boot/Image) + $(stat -c %s arch/arm/boot/zImage)))
[ $size -lt 35000000 ] || { echo "Image + zImage = $size bytes: too big for sboot's layout"; exit 1; }

rm -rf "$P/kernel" "$P/modules" && mkdir -p "$P/kernel" "$P/modules"
cat arch/arm/boot/zImage arch/arm/boot/dts/samsung/exynos4412-n7100-ea8061.dtb > "$P/kernel/zImage-dtb"
# the same kernel for the Note II's other panel (not in the flash zip yet)
cat arch/arm/boot/zImage arch/arm/boot/dts/samsung/exynos4412-n7100-s6evr02.dtb > "$P/kernel/zImage-dtb-s6evr02"
MR=$(mktemp -d)
$M INSTALL_MOD_PATH="$MR" INSTALL_MOD_STRIP=1 modules_install > /dev/null
find "$MR/lib/modules" -name '*.ko' -exec cp {} "$P/modules/" \;
# netfilter modules behind netd's iptables rules (rootdir/bin/n7100-modules.sh loads them)
(cd "$MR"/lib/modules/*/kernel && find net -path '*netfilter*' -name '*.ko') | xargs -n1 basename |
    sed 's/\.ko$//' | grep -vE '^(nft_|nf_tables|nf_flow_table|ip_vs|ebt|arp|br_|nf_conntrack_bridge)' |
    sort > "$P/modules.netfilter"
rm -rf "$MR"
echo "kernel $(cat include/config/kernel.release): $(stat -c %s "$P/kernel/zImage-dtb") bytes," \
    "$(ls "$P/modules" | wc -l) modules"
