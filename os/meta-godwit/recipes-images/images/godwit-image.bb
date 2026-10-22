SUMMARY = "Godwit head unit image"
DESCRIPTION = "AGL minimal image with the Godwit HMI drawing straight to the \
display through eglfs/KMS. No Wayland compositor, so the HMI is the first \
thing on screen."
LICENSE = "MIT"

require recipes-platform/images/agl-image-minimal.bb

# EGL and GBM arrive through Qt's library dependencies; the freedreno
# driver itself is a plugin, so it is named here.
IMAGE_INSTALL:append = " \
    godwit-hmi \
    mesa-megadriver \
"
