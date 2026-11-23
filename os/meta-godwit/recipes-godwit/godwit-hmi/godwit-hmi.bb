SUMMARY = "Godwit head unit HMI"
DESCRIPTION = "Qt Quick HMI for the Godwit dual-encoder head unit, rendered \
full screen with eglfs on KMS."
HOMEPAGE = "https://github.com/trailcurrentoss"
# App code and Ionicons: MIT; Inter font: OFL-1.1; the fan and seat icons
# from Material Symbols: Apache-2.0
LICENSE = "MIT & OFL-1.1 & Apache-2.0"
LIC_FILES_CHKSUM = "file://${UNPACKDIR}/LICENSE;md5=0c3de729fcec549a24b64cc2c1a62d48 \
                    file://assets/icons/LICENSE-ionicons;md5=92e02b7236a0635eaf1478f8041b0602 \
                    file://assets/fonts/LICENSE-inter.txt;md5=082d8cc7db39783f598b7eda8c911f9d \
                    file://assets/licenses/Apache-2.0.txt;md5=9c906197621ff6c11095fe41415ecad6"

# The app lives in this repository, beside os/
FILESEXTRAPATHS:prepend := "${GODWIT_TOPDIR}:"
SRC_URI = "file://hmi \
           file://LICENSE \
           file://godwit-hmi.service \
           file://godwit-kms.json \
           file://godwit-hmi-dbus.conf \
           file://godwit-obexd.service \
           file://70-godwit-dials.rules \
           file://71-godwit-usb.rules \
           file://50-godwit-hmi.rules \
           file://godwit-clear-logs.path \
           file://godwit-clear-logs.service \
           file://godwit-restart.path \
           file://godwit-restart.service"
S = "${UNPACKDIR}/hmi"

DEPENDS = "qtbase qtdeclarative qtdeclarative-native qtsvg qtmultimedia qtshadertools-native valhalla boost taglib"

inherit qt6-cmake systemd useradd

USERADD_PACKAGES = "${PN}"
# The HMI user needs the display and input devices, and PipeWire's socket for
# volume; make sure the groups exist
GROUPADD_PARAM:${PN} = "-f -r video; -f -r input; -f -r pipewire"
USERADD_PARAM:${PN} = "--system --no-create-home --home-dir /nonexistent --shell /sbin/nologin \
                       --groups video,input,pipewire godwit"

SYSTEMD_SERVICE:${PN} = "godwit-hmi.service godwit-obexd.service godwit-clear-logs.path godwit-restart.path"
SYSTEMD_AUTO_ENABLE = "enable"

# Each board's layer adds what depends on its hardware, in
# dynamic-layers/godwit/: the udev rule naming the GNSS receiver's I2C bus
# /dev/gnss-i2c, and any display or audio setup the board needs.

do_install:append() {
    install -Dm 0644 ${UNPACKDIR}/godwit-hmi.service ${D}${systemd_system_unitdir}/godwit-hmi.service
    # The display mode eglfs drives, in place of the EDID's preferred one
    install -Dm 0644 ${UNPACKDIR}/godwit-kms.json ${D}${sysconfdir}/godwit/kms.json
    install -Dm 0644 ${UNPACKDIR}/godwit-obexd.service ${D}${systemd_system_unitdir}/godwit-obexd.service
    # The encoder modules' hidraw nodes, for the godwit user
    install -Dm 0644 ${UNPACKDIR}/70-godwit-dials.rules ${D}${sysconfdir}/udev/rules.d/70-godwit-dials.rules
    # USB drives mounted read-only for importing photos
    install -Dm 0644 ${UNPACKDIR}/71-godwit-usb.rules ${D}${sysconfdir}/udev/rules.d/71-godwit-usb.rules
    install -Dm 0644 ${UNPACKDIR}/godwit-hmi-dbus.conf ${D}${datadir}/dbus-1/system.d/godwit-hmi.conf
    # What the HMI's user may not do itself: it leaves a file in its runtime
    # directory and these act on it. Clearing the logs, restarting the head unit.
    for unit in godwit-clear-logs.path godwit-clear-logs.service godwit-restart.path godwit-restart.service; do
        install -Dm 0644 ${UNPACKDIR}/$unit ${D}${systemd_system_unitdir}/$unit
    done
    # Lets the HMI set the clock and time zone
    install -Dm 0644 ${UNPACKDIR}/50-godwit-hmi.rules ${D}${datadir}/polkit-1/rules.d/50-godwit-hmi.rules
    # The media library: Pictures, Videos, Media (Local) and Trails show these folders
    install -d -m 0750 -o godwit -g godwit ${D}/data/media ${D}/data/media/photos ${D}/data/media/videos ${D}/data/media/music ${D}/data/media/trails
}

FILES:${PN} += "${datadir}/dbus-1 ${datadir}/polkit-1 /data/media ${systemd_system_unitdir}/godwit-clear-logs.service ${systemd_system_unitdir}/godwit-restart.service"

RDEPENDS:${PN} = " \
    qtbase-plugins \
    qtdeclarative-qmlplugins \
    qtsvg-plugins \
    qtmultimedia-plugins \
    qtmultimedia-qmlplugins \
    maplibre-native-qt \
    connman \
    ofono \
    bluez5-obex \
    godwit-map-style \
    godwit-tracks \
    godwit-clock \
    godwit-hostname \
    godwit-can \
    trailcurrent-dbc \
    valhalla-conf \
    photon \
    piper-tts \
    whisper-cpp \
    pipewire-tools \
"
# USB drives to import photos from; where these are built into the kernel
# there is no package, and nothing is missing
RRECOMMENDS:${PN} = "kernel-module-exfat kernel-module-ntfs3"

