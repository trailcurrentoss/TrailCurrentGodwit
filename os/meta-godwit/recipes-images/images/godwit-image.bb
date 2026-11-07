SUMMARY = "Godwit head unit image"
DESCRIPTION = "AGL minimal image with the Godwit HMI drawing straight to the \
display through eglfs/KMS. No Wayland compositor, so the HMI is the first \
thing on screen."
LICENSE = "MIT"

require recipes-platform/images/agl-image-minimal.bb

# Every package's license text and the package/license manifest, under
# /usr/share/common-licenses; Settings > System > About & Legal points there
COPY_LIC_MANIFEST = "1"
COPY_LIC_DIRS = "1"

# What sets up the drive. godwit-growroot gives a board one big root partition.
# A board with godwit-storage boots it (init= in its wks) and gets separate
# partitions for the system, personal data and maps, and a real factory reset.
GODWIT_STORAGE ?= "godwit-growroot"

# With godwit-storage the root partition is read only, and needs no check
ROOTFS_POSTPROCESS_COMMAND:append = "${@' godwit_readonly_root;' if d.getVar('GODWIT_STORAGE') == 'godwit-storage' else ''}"
godwit_readonly_root() {
    sed -i -E 's#^(/dev/root[[:space:]]+/[[:space:]]+[^[:space:]]+[[:space:]]+)[^[:space:]]+[[:space:]]+[0-9]+[[:space:]]+[0-9]+#\1ro,noatime 0 0#' ${IMAGE_ROOTFS}${sysconfdir}/fstab
    grep -Eq '^/dev/root[[:space:]]+/[[:space:]].*ro,noatime' ${IMAGE_ROOTFS}${sysconfdir}/fstab
}

# EGL and GBM arrive through Qt's library dependencies; the GPU driver
# itself (freedreno, v3d) is a Mesa plugin, so it is named here.
IMAGE_INSTALL:append = " \
    godwit-hmi \
    ${GODWIT_STORAGE} \
    photon \
    i2c-tools \
    mesa-megadriver \
"

# Audio: AGL's system-wide PipeWire, as its demo images have it. Its
# Bluetooth WirePlumber instance carries phone calls (HFP, through oFono) and
# phone music (A2DP) to the speakers and microphone. Not on the boot path:
# PipeWire starts after the HMI.
IMAGE_INSTALL:append = " \
    packagegroup-pipewire \
    wireplumber-config-agl \
    wireplumber-policy-config-agl \
    ${@bb.utils.contains('DISTRO_FEATURES', 'agl-devel', 'packagegroup-pipewire-tools alsa-utils', '', d)} \
"
