SUMMARY = "whisper.cpp speech to text, offline, with an English model"
DESCRIPTION = "whisper.cpp's command line transcriber and the base.en Whisper \
model, for dictating text message replies in the HMI. Runs entirely on the \
head unit. Built statically, so whisper-cli carries its own ggml."
HOMEPAGE = "https://github.com/ggml-org/whisper.cpp"
# whisper.cpp and the Whisper model weights: MIT
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=1539dadbedb60aa18519febfeab70632"

MODEL = "ggml-base.en.bin"
SRC_URI = "git://github.com/ggml-org/whisper.cpp.git;protocol=https;branch=master;tag=v${PV} \
           https://huggingface.co/ggerganov/whisper.cpp/resolve/main/${MODEL};name=model;unpack=0"
SRCREV = "a8d002cfd879315632a579e73f0148d06959de36"
SRC_URI[model.sha256sum] = "a03779c86df3323075f5e796cb2ce5029f00ec8869eee3fdfb897afe36c6d002"

COMPATIBLE_HOST = "aarch64.*-linux"

inherit cmake

# The QCS6490's cores all have the dot product and half precision
# instructions, which ggml's matrix code uses
EXTRA_OECMAKE = " \
    -DBUILD_SHARED_LIBS=OFF \
    -DGGML_NATIVE=OFF \
    -DGGML_CPU_ARM_ARCH=armv8.2-a+dotprod+fp16 \
    -DWHISPER_BUILD_TESTS=OFF \
    -DWHISPER_BUILD_SERVER=OFF \
    -DWHISPER_BUILD_EXAMPLES=ON \
    -DWHISPER_SDL2=OFF \
    -DWHISPER_CURL=OFF \
"

# Only the transcriber and the model; the libraries are linked in
do_install() {
    install -Dm 0755 ${B}/bin/whisper-cli ${D}${bindir}/whisper-cli
    install -Dm 0644 ${UNPACKDIR}/${MODEL} ${D}${datadir}/whisper/${MODEL}
}

FILES:${PN} = "${bindir}/whisper-cli ${datadir}/whisper"
