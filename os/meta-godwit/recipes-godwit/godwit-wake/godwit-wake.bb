SUMMARY = "Godwit's wake word detector"
DESCRIPTION = "Listens for \"Hey Peregrine\" in microphone audio with \
openWakeWord's models on ONNX Runtime, for the HMI's voice assistant. Runs \
entirely on the head unit."
HOMEPAGE = "https://github.com/dscripka/openWakeWord"
# The detector and the Hey Peregrine model: MIT. openWakeWord's feature
# models (melspectrogram, embedding): Apache-2.0.
LICENSE = "MIT & Apache-2.0"
LIC_FILES_CHKSUM = "file://${UNPACKDIR}/LICENSE;md5=0c3de729fcec549a24b64cc2c1a62d48 \
                    file://${COMMON_LICENSE_DIR}/Apache-2.0;md5=89aea4e17d99a7cacdbeed46a0096b10"

# The detector lives in this repository, beside os/
FILESEXTRAPATHS:prepend := "${GODWIT_TOPDIR}:"
OWW_URL = "https://github.com/dscripka/openWakeWord/releases/download/v0.5.1"
SRC_URI = "file://wake \
           file://LICENSE \
           ${OWW_URL}/melspectrogram.onnx;name=mel;unpack=0 \
           ${OWW_URL}/embedding_model.onnx;name=embed;unpack=0"
SRC_URI[mel.sha256sum] = "ba2b0e0f8b7b875369a2c89cb13360ff53bac436f2895cced9f479fa65eb176f"
SRC_URI[embed.sha256sum] = "70d164290c1d095d1d4ee149bc5e00543250a7316b59f31d056cff7bd3075c1f"
S = "${UNPACKDIR}/wake"

DEPENDS = "onnxruntime-bin"

inherit cmake

do_install:append() {
    install -m 0644 ${UNPACKDIR}/melspectrogram.onnx ${UNPACKDIR}/embedding_model.onnx \
        ${D}${datadir}/godwit/wake/
}

FILES:${PN} += "${datadir}/godwit/wake"
