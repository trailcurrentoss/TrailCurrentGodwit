SUMMARY = "AirPlay audio receiver"
DESCRIPTION = "Shairport Sync makes the head unit an AirPlay receiver, so an \
iPhone on the same Wi-Fi network plays Apple Music (or anything else) \
through it. The HMI runs it with its own settings and follows it through \
its MPRIS interface on the system bus. Classic AirPlay: it takes a \
password and passes play, pause and skip back to the phone."
HOMEPAGE = "https://github.com/mikebrady/shairport-sync"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://COPYING;md5=ee2d008df96f0812830f2262b10ca8e0 \
                    file://LICENSES;md5=3caa3f9f59629dcd65c50738e60b7602"

SRC_URI = "git://github.com/mikebrady/shairport-sync.git;protocol=https;branch=master"
SRCREV = "0a34fbda83724cc06266aadcf7c88acc8fef4e7d"

DEPENDS = "alsa-lib avahi openssl popt libconfig glib-2.0 glib-2.0-native ffmpeg"

inherit autotools pkgconfig

# ALSA's default device is PipeWire (pipewire-alsa). Avahi advertises the
# receiver. FFmpeg (already in the image for Qt Multimedia) decodes the
# audio; the older built-in decoders are deprecated upstream. The D-Bus and
# MPRIS interfaces need metadata.
EXTRA_OECONF = " \
    --with-alsa \
    --with-avahi \
    --with-ssl=openssl \
    --with-ffmpeg \
    --with-metadata \
    --with-dbus-interface \
    --with-mpris-interface \
    --without-systemd-startup \
    --without-create-user-group \
    --without-configfiles \
"

RDEPENDS:${PN} = "avahi-daemon pipewire-alsa"
