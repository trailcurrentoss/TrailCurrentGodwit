SUMMARY = "TrailCurrent boot logo for a Raspberry Pi with a DSI panel"
DESCRIPTION = "The TrailCurrent splash the Raspberry Pi kernel draws on the \
panel (fullscreen_logo) from the moment the display comes up until the HMI \
takes over, and no login prompt on the panel's console to draw over it."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Made by tools/make-bootlogo.py from the TrailCurrent splash
SRC_URI = "file://godwit-logo.tga"
S = "${UNPACKDIR}"

inherit allarch

do_install() {
    install -Dm 0644 ${UNPACKDIR}/godwit-logo.tga ${D}${nonarch_base_libdir}/firmware/godwit-logo.tga
    # The panel belongs to the HMI. A getty on tty1 would clear the logo and
    # print a login prompt before the HMI starts; the serial console keeps its own.
    install -d ${D}${sysconfdir}/systemd/system
    ln -sf /dev/null ${D}${sysconfdir}/systemd/system/getty@tty1.service
}

FILES:${PN} = "${nonarch_base_libdir}/firmware/godwit-logo.tga ${sysconfdir}/systemd/system/getty@tty1.service"
