FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI:append:rpi = " file://61-godwit-audio.conf"

do_install:append:rpi() {
    install -m 0644 ${UNPACKDIR}/61-godwit-audio.conf ${D}${sysconfdir}/wireplumber/wireplumber.conf.d/
}
