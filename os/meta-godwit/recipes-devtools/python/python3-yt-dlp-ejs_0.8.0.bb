SUMMARY = "yt-dlp's solver scripts for YouTube's player"
DESCRIPTION = "JavaScript that yt-dlp runs (here with Node.js) to read \
YouTube's player. Installed from the released wheel: building it from \
source needs a JavaScript toolchain."
HOMEPAGE = "https://github.com/yt-dlp/ejs"
# The package: Unlicense. The bundled JavaScript libraries: MIT and ISC.
LICENSE = "Unlicense & MIT & ISC"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/Unlicense;md5=7246f848faa4e9c9fc0ea91122d6e680 \
                    file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302 \
                    file://${COMMON_LICENSE_DIR}/ISC;md5=f3b90e78ea0cffb20bf5cca7947a896d"

WHEEL = "yt_dlp_ejs-${PV}-py3-none-any.whl"
SRC_URI = "https://files.pythonhosted.org/packages/e3/bd/520769863744b669440a924271a6159ddd82ad5ae26b4ac4d4b69e9f8d44/${WHEEL};unpack=0"
SRC_URI[sha256sum] = "79300e5fca7f937a1eeede11f0456862c1b41107ce1d726871e0207424f4bdb4"
S = "${UNPACKDIR}"

inherit python_pep517

do_configure[noexec] = "1"
# The wheel is the build
do_compile() {
    install -Dm 0644 ${UNPACKDIR}/${WHEEL} ${PEP517_WHEEL_PATH}/${WHEEL}
}

RDEPENDS:${PN} = "python3-core"
