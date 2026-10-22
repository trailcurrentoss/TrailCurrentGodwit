SUMMARY = "Eclipse Temurin Java 21 runtime, headless"
DESCRIPTION = "Prebuilt Java 21 runtime for Photon place search. The layers \
here only build OpenJDK 21 for the host, so the target gets Adoptium's \
release build. The X11 and ALSA parts are left out: nothing on the head unit \
draws or plays sound through Java."
HOMEPAGE = "https://adoptium.net"
LICENSE = "GPL-2.0-with-classpath-exception"
LIC_FILES_CHKSUM = "file://legal/java.base/LICENSE;md5=3e0b59f8fac05c3c03d4a26bbda13f8f"

JRE_BUILD = "1"
SRC_URI = "https://github.com/adoptium/temurin21-binaries/releases/download/jdk-${PV}+${JRE_BUILD}/OpenJDK21U-jre_aarch64_linux_hotspot_${PV}_${JRE_BUILD}.tar.gz"
SRC_URI[sha256sum] = "14be1f35ebdbd1f6e8d57eb911a3ffb74d6d9aa255abc5daf2b1302002cf2cf2"
S = "${UNPACKDIR}/jdk-${PV}+${JRE_BUILD}-jre"

COMPATIBLE_HOST = "aarch64.*-linux"

JRE_HOME = "${libdir}/jvm/temurin-21-jre"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    install -d ${D}${JRE_HOME}
    cp -R --no-preserve=ownership ${S}/bin ${S}/conf ${S}/lib ${S}/legal ${S}/release ${D}${JRE_HOME}/
    # Headless: these need X11 or ALSA
    rm ${D}${JRE_HOME}/lib/libawt_xawt.so ${D}${JRE_HOME}/lib/libjawt.so \
       ${D}${JRE_HOME}/lib/libsplashscreen.so ${D}${JRE_HOME}/lib/libjsound.so
    install -d ${D}${bindir}
    ln -s ${JRE_HOME}/bin/java ${D}${bindir}/java
}

FILES:${PN} = "${JRE_HOME} ${bindir}/java"

# A release build: already stripped, and its libraries are private to the JVM
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
INHIBIT_SYSROOT_STRIP = "1"
EXCLUDE_FROM_SHLIBS = "1"
INSANE_SKIP:${PN} = "already-stripped ldflags dev-so libdir"

# Linked against glibc only (libc, libm, libdl, libpthread, librt)
RDEPENDS:${PN} = "glibc"
