FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += "file://60-godwit-volume.conf file://62-godwit-mic.conf"

do_install:append() {
    install -m 0644 ${UNPACKDIR}/60-godwit-volume.conf ${UNPACKDIR}/62-godwit-mic.conf \
        ${D}${sysconfdir}/wireplumber/wireplumber.conf.d/
}

# 62-godwit-mic.conf loads the echo-cancel module with WebRTC's canceller
RDEPENDS:${PN} += "pipewire-modules-echo-cancel pipewire-spa-plugins-aec-webrtc"
