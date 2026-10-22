SUMMARY = "Radxa's Linux kernel for Qualcomm boards (Dragon Q6A)"
DESCRIPTION = "Mainline 7.0 stable plus Radxa's Q6A patches: HDMI through the \
RA620 DP bridge, USB 3 / Type-C, UFS and the board's firmware fixes."

inherit kernel
require recipes-kernel/linux/linux-yocto.inc
require ${@bb.utils.contains('AGL_FEATURES', 'aglcore', 'recipes-kernel/linux/linux-yocto-agl.inc', '', d)}

LIC_FILES_CHKSUM = "file://COPYING;md5=6bc538ed5bd9a7fc9398086aedcd7e46"

KBRANCH = "linux-7.0.11"
SRC_URI = " \
    git://github.com/radxa/kernel.git;protocol=https;branch=${KBRANCH};name=machine \
    file://q6a-yocto.cfg \
    file://q6a-audio.cfg \
    file://0001-Bluetooth-btusb-Use-16-bit-SCO-alt-setting-when-voic.patch \
"
SRCREV_machine = "4ff8ac9601d6951aed068af1c9c072ac43bbf6dd"

# The Radxa repository carries many full kernel branches; fetch only this one
BB_GIT_SHALLOW = "1"
BB_GIT_SHALLOW_DEPTH = "1"

LINUX_VERSION = "7.0.11"
PV = "${LINUX_VERSION}+git"

KBUILD_DEFCONFIG = "radxa_qcom_7_0_defconfig"
KCONFIG_MODE = "--alldefconfig"

# These live in the linux-yocto kernel-cache, which this tree doesn't use;
# Radxa's defconfig already enables EFI and VFAT
KERNEL_FEATURES:remove = "cfg/efi.scc cfg/fs/vfat.scc"

# The config audit's kconfiglib can't parse 7.0's "depends on X if X" syntax
KMETA_AUDIT = ""

COMPATIBLE_MACHINE = "dragon-q6a"
