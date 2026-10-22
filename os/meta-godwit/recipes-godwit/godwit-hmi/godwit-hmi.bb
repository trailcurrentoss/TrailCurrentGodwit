SUMMARY = "Godwit head unit HMI"
DESCRIPTION = "Qt Quick HMI for the Godwit dual-encoder head unit, rendered \
full screen with eglfs on KMS."
HOMEPAGE = "https://github.com/trailcurrentoss"
# App code and Ionicons: MIT; Inter font: OFL-1.1
LICENSE = "MIT & OFL-1.1"
LIC_FILES_CHKSUM = "file://assets/icons/LICENSE-ionicons;md5=92e02b7236a0635eaf1478f8041b0602 \
                    file://assets/fonts/LICENSE-inter.txt;md5=082d8cc7db39783f598b7eda8c911f9d"

# The app lives in this repository, beside os/
FILESEXTRAPATHS:prepend := "${GODWIT_TOPDIR}:"
SRC_URI = "file://hmi \
           file://godwit-hmi.service \
           file://godwit-hmi-dbus.conf \
           file://godwit-obexd.service \
           file://70-godwit-dials.rules"
S = "${UNPACKDIR}/hmi"

DEPENDS = "qtbase qtdeclarative qtdeclarative-native qtsvg qtshadertools-native"

inherit qt6-cmake systemd useradd

USERADD_PACKAGES = "${PN}"
# The HMI user needs the display and input devices, and PipeWire's socket for
# volume; make sure the groups exist
GROUPADD_PARAM:${PN} = "-f -r video; -f -r input; -f -r pipewire"
USERADD_PARAM:${PN} = "--system --no-create-home --home-dir /nonexistent --shell /sbin/nologin \
                       --groups video,input,pipewire godwit"

SYSTEMD_SERVICE:${PN} = "godwit-hmi.service godwit-obexd.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install:append() {
    install -Dm 0644 ${UNPACKDIR}/godwit-hmi.service ${D}${systemd_system_unitdir}/godwit-hmi.service
    install -Dm 0644 ${UNPACKDIR}/godwit-obexd.service ${D}${systemd_system_unitdir}/godwit-obexd.service
    # The encoder modules' hidraw nodes, for the godwit user
    install -Dm 0644 ${UNPACKDIR}/70-godwit-dials.rules ${D}${sysconfdir}/udev/rules.d/70-godwit-dials.rules
    install -Dm 0644 ${UNPACKDIR}/godwit-hmi-dbus.conf ${D}${datadir}/dbus-1/system.d/godwit-hmi.conf
}

FILES:${PN} += "${datadir}/dbus-1"

RDEPENDS:${PN} = " \
    qtbase-plugins \
    qtdeclarative-qmlplugins \
    qtsvg-plugins \
    maplibre-native-qt \
    connman \
    ofono \
    bluez5-obex \
    godwit-map-style \
"
