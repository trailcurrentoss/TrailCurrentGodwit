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

# EGL and GBM arrive through Qt's library dependencies; the GPU driver
# itself (freedreno, v3d) is a Mesa plugin, so it is named here.
IMAGE_INSTALL:append = " \
    godwit-hmi \
    godwit-growroot \
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
