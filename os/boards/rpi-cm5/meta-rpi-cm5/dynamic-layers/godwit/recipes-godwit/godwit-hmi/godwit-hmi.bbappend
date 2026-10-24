FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append:rpi = " \
    file://godwit-kms-config \
    file://rpi-kms.conf \
    file://70-godwit-gnss.rules \
"

do_install:append:rpi() {
    # Point eglfs at the DRM card with the display on it
    install -Dm 0755 ${UNPACKDIR}/godwit-kms-config ${D}${libexecdir}/godwit-kms-config
    install -Dm 0644 ${UNPACKDIR}/rpi-kms.conf ${D}${systemd_system_unitdir}/godwit-hmi.service.d/rpi-kms.conf
    # The GNSS receiver on the header I2C, as /dev/gnss-i2c
    install -Dm 0644 ${UNPACKDIR}/70-godwit-gnss.rules ${D}${sysconfdir}/udev/rules.d/70-godwit-gnss.rules
}

FILES:${PN}:append:rpi = " ${libexecdir}/godwit-kms-config ${systemd_system_unitdir}/godwit-hmi.service.d"
