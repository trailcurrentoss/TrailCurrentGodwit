FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append:rpi = " file://can-ports"

do_install:append:rpi() {
    install -Dm 0644 ${UNPACKDIR}/can-ports ${D}${datadir}/godwit/can-ports
}

FILES:${PN}:append:rpi = " ${datadir}/godwit/can-ports"
