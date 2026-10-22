SUMMARY = "Offline map styles, glyphs and sprites for the Godwit HMI"
DESCRIPTION = "The TrailCurrent Headwaters light and dark map styles, rewritten \
to read the installed map bundle's PMTiles and this package's glyphs and \
sprites from disk. Tiles are not included; they come from a map bundle \
installed under /data/maps."
HOMEPAGE = "https://github.com/trailcurrentoss/TrailCurrentHeadwaters"
# Styles: BSD-3-Clause (OpenMapTiles derivative) with MIT changes; Noto Sans
# glyphs: OFL-1.1; sprite icons: CC0-1.0. See Headwaters THIRD_PARTY_LICENSES.md.
LICENSE = "MIT & BSD-3-Clause & OFL-1.1 & CC0-1.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=9f115399095749332b1ba14c16e81de1 \
                    file://containers/frontend/public/maps-static/fonts/LICENSE;md5=ea85845ff56f9c496e18c243c620310c \
                    file://THIRD_PARTY_LICENSES.md;beginline=31;endline=62;md5=6f8a7d1e1dc3ba4a79a4c2b299299883"

SRC_URI = "git://github.com/trailcurrentoss/TrailCurrentHeadwaters.git;protocol=https;branch=main \
           file://godwit-style.py"
SRCREV = "ff4fd287743fd0ed8c9772dabd79b64e084b3c59"
PV = "1.0+git"

inherit allarch python3native

GODWIT_MAP_SHARE = "${datadir}/godwit/maps"
GODWIT_MAP_TILES ?= "/data/maps/current/tiles.pmtiles"
# Only the font stacks the styles use
GODWIT_MAP_FONTS = "Noto Sans Regular;Noto Sans Bold;Noto Sans Italic"

do_configure[noexec] = "1"

do_compile() {
    web=${S}/containers/frontend/public/maps-static
    for s in 3d 3d-dark; do
        mkdir -p ${B}/styles/$s
        flag=""
        [ "$s" = "3d-dark" ] && flag="--dark"
        ${PYTHON} ${UNPACKDIR}/godwit-style.py $web/styles/$s/style.json ${B}/styles/$s/style.json \
            ${GODWIT_MAP_SHARE} ${GODWIT_MAP_TILES} $flag
    done
}

do_install() {
    web=${S}/containers/frontend/public/maps-static
    dest=${D}${GODWIT_MAP_SHARE}
    install -d $dest/styles $dest/fonts $dest/sprites
    cp -r ${B}/styles/3d ${B}/styles/3d-dark $dest/styles/
    install -m 0644 $web/sprites/sprite.json $web/sprites/sprite.png \
        $web/sprites/sprite@2x.json $web/sprites/sprite@2x.png $dest/sprites/
    fonts="${GODWIT_MAP_FONTS}"
    echo "$fonts" | tr ';' '\n' | while read -r f; do
        cp -r "$web/fonts/$f" "$dest/fonts/"
    done
    install -m 0644 $web/fonts/LICENSE $dest/fonts/LICENSE
    install -d -m 0755 ${D}/data/maps
}

FILES:${PN} = "${GODWIT_MAP_SHARE} /data/maps"
