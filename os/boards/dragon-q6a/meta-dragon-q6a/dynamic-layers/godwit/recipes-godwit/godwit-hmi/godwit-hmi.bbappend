FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append:dragon-q6a = " \
    file://70-godwit-gnss.rules \
    file://70-godwit-audio.rules \
"

do_install:append:dragon-q6a() {
    # The GNSS receiver on the header I2C (I2C6), as /dev/gnss-i2c
    install -Dm 0644 ${UNPACKDIR}/70-godwit-gnss.rules ${D}${sysconfdir}/udev/rules.d/70-godwit-gnss.rules
    # The codec's headphone amplifier level
    install -Dm 0644 ${UNPACKDIR}/70-godwit-audio.rules ${D}${sysconfdir}/udev/rules.d/70-godwit-audio.rules
}

# 70-godwit-audio.rules runs amixer
RDEPENDS:${PN}:append:dragon-q6a = " alsa-utils-amixer"
