FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += "file://godwit.conf \
            file://0001-gobex-Do-not-crash-when-the-transport-closes.patch"

# Phones list the head unit by this name and class. 0x200420 is Audio/Video,
# car audio, with the audio service bit, so it shows among car systems.
#
# Classic Bluetooth only: phones use it for calls and audio, and nothing on
# the head unit needs Low Energy. In dual mode the kernel follows a pairing
# with an SMP request to derive LE keys; an iPhone leaves that unanswered,
# the link times out and the phone reports pairing as failed.
do_install:append() {
    sed -i -e 's/^#Name = .*/Name = Godwit/' \
           -e 's/^#Class = .*/Class = 0x200420/' \
           -e 's/^#ControllerMode = .*/ControllerMode = bredr/' \
           ${D}${sysconfdir}/bluetooth/main.conf
    install -d ${D}${systemd_system_unitdir}/bluetooth.service.d
    install -m 0644 ${UNPACKDIR}/godwit.conf ${D}${systemd_system_unitdir}/bluetooth.service.d/
}

FILES:${PN} += "${systemd_system_unitdir}/bluetooth.service.d"
