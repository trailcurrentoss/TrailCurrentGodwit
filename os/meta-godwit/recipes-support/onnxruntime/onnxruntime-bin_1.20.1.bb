SUMMARY = "ONNX Runtime, Microsoft's prebuilt aarch64 release"
DESCRIPTION = "ONNX Runtime's CPU inference library and its C and C++ API \
headers, from the official aarch64 release, for the wake word detector \
(godwit-wake)."
HOMEPAGE = "https://onnxruntime.ai"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=0f7e3b1308cb5c00b372a6e78835732d"

SRC_URI = "https://github.com/microsoft/onnxruntime/releases/download/v${PV}/onnxruntime-linux-aarch64-${PV}.tgz"
SRC_URI[sha256sum] = "ae4fedbdc8c18d688c01306b4b50c63de3445cdf2dbd720e01a2fa3810b8106a"
S = "${UNPACKDIR}/onnxruntime-linux-aarch64-${PV}"

COMPATIBLE_HOST = "aarch64.*-linux"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    install -d ${D}${libdir} ${D}${includedir}/onnxruntime
    install -m 0755 ${S}/lib/libonnxruntime.so.${PV} ${D}${libdir}/
    ln -s libonnxruntime.so.${PV} ${D}${libdir}/libonnxruntime.so.1
    ln -s libonnxruntime.so.1 ${D}${libdir}/libonnxruntime.so
    cp -R --no-preserve=ownership ${S}/include/. ${D}${includedir}/onnxruntime/
}

# A release build: already stripped
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
INHIBIT_SYSROOT_STRIP = "1"
INSANE_SKIP:${PN} = "already-stripped ldflags"
