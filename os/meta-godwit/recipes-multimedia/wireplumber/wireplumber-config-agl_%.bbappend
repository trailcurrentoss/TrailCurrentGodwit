FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += "file://60-godwit-volume.conf"

do_install:append() {
    install -m 0644 ${UNPACKDIR}/60-godwit-volume.conf ${D}${sysconfdir}/wireplumber/wireplumber.conf.d/
}
