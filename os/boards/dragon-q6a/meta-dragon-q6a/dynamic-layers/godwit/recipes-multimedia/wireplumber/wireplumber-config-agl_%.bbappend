FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI:append:dragon-q6a = " file://61-godwit-output.conf"

do_install:append:dragon-q6a() {
    install -m 0644 ${UNPACKDIR}/61-godwit-output.conf ${D}${sysconfdir}/wireplumber/wireplumber.conf.d/
}
