FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append:rpi = " file://rpi5-dsi-rotation.conf"

do_install:append:rpi() {
    # The panel is portrait (720x1280) and mounted in landscape
    install -Dm 0644 ${UNPACKDIR}/rpi5-dsi-rotation.conf ${D}${systemd_system_unitdir}/godwit-hmi.service.d/rpi5-dsi-rotation.conf
}

FILES:${PN}:append:rpi = " ${systemd_system_unitdir}/godwit-hmi.service.d"
