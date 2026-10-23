SUMMARY = "Piper text to speech, offline, with an English voice"
DESCRIPTION = "Piper neural text to speech for the HMI's spoken navigation \
prompts, with the LJSpeech English voice. Runs entirely on the head unit. \
Piper's prebuilt aarch64 release: the piper program and its ONNX Runtime, \
piper-phonemize and espeak-ng libraries, which find each other through \
their \$ORIGIN rpath, and espeak-ng's phoneme data."
HOMEPAGE = "https://github.com/rhasspy/piper"
# Piper, ONNX Runtime and the voice model: MIT. espeak-ng: GPL-3.0-or-later.
# The voice is trained on LJSpeech, which is public domain.
LICENSE = "MIT & GPL-3.0-or-later"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302 \
                    file://${COMMON_LICENSE_DIR}/GPL-3.0-or-later;md5=1c76c4cc354acaac30ed4d5eefea7245"

VOICE = "en_US-ljspeech-medium"
VOICE_URL = "https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/ljspeech/medium"
SRC_URI = "https://github.com/rhasspy/piper/releases/download/${PV}/piper_linux_aarch64.tar.gz;subdir=release \
           ${VOICE_URL}/${VOICE}.onnx;name=model;unpack=0 \
           ${VOICE_URL}/${VOICE}.onnx.json;name=config;unpack=0"
SRC_URI[sha256sum] = "fea0fd2d87c54dbc7078d0f878289f404bd4d6eea6e7444a77835d1537ab88eb"
SRC_URI[model.sha256sum] = "6f52a751e2349abe7a76735eb09dc1875298c77ea2342ffd2fef79ff81b87f22"
SRC_URI[config.sha256sum] = "141d612cc0a95ed7efc1ca936b845c2364967f2e9217c5dbfcf69fc4d6c65860"
S = "${UNPACKDIR}/release/piper"

COMPATIBLE_HOST = "aarch64.*-linux"

PIPER_DIR = "${libdir}/piper"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    install -d ${D}${PIPER_DIR}
    install -m 0755 ${S}/piper ${D}${PIPER_DIR}/
    for lib in libonnxruntime.so.1.14.1 libpiper_phonemize.so.1.2.0 libespeak-ng.so.1.52.0.1; do
        install -m 0755 ${S}/$lib ${D}${PIPER_DIR}/
    done
    ln -s libpiper_phonemize.so.1.2.0 ${D}${PIPER_DIR}/libpiper_phonemize.so.1
    ln -s libespeak-ng.so.1.52.0.1 ${D}${PIPER_DIR}/libespeak-ng.so.1
    cp -R --no-preserve=ownership ${S}/espeak-ng-data ${D}${PIPER_DIR}/
    install -Dm 0644 ${UNPACKDIR}/${VOICE}.onnx ${D}${datadir}/piper/voices/${VOICE}.onnx
    install -Dm 0644 ${UNPACKDIR}/${VOICE}.onnx.json ${D}${datadir}/piper/voices/${VOICE}.onnx.json
}

FILES:${PN} = "${PIPER_DIR} ${datadir}/piper"

# A release build: already stripped, and its libraries are private to Piper
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
INHIBIT_SYSROOT_STRIP = "1"
EXCLUDE_FROM_SHLIBS = "1"
INSANE_SKIP:${PN} = "already-stripped ldflags dev-so libdir file-rdeps"

RDEPENDS:${PN} = "glibc libstdc++ libgcc"
