SUMMARY = "MapLibre Native for Qt"
DESCRIPTION = "Vector map renderer with a Qt Quick item (import MapLibre). \
Reads PMTiles directly from local files (pmtiles://file://...), so no tile \
server is needed."
HOMEPAGE = "https://github.com/maplibre/maplibre-native-qt"
# Only src/location is (L)GPL, and it is not built (MLN_QT_WITH_LOCATION=OFF)
LICENSE = "BSD-2-Clause"
LIC_FILES_CHKSUM = "file://LICENSES/BSD-2-Clause.txt;md5=272be00ca1ae12eceb040a3946c3c2cc \
                    file://vendor/maplibre-native/LICENSE.md;md5=9557c4180a72a6137edd96affcb140e4"

# The standalone MapLibre QML item (no QtLocation) is only on the 4.0 line.
SRC_URI = "gitsm://github.com/maplibre/maplibre-native-qt.git;protocol=https;branch=main"
SRCREV = "c3485f3a9590081c7be8edbc542341e688f6663d"
PV = "4.0.0+git"

DEPENDS = "qtbase qtdeclarative qtdeclarative-native icu zlib"

inherit qt6-cmake pkgconfig

# MLN_WITH_WERROR: GCC 15 raises a false free-nonheap-object warning in
# MapLibre's symbol code; keep warnings as warnings for a packaged build.
EXTRA_OECMAKE += " \
    -DMLN_WITH_WERROR=OFF \
    -DMLN_WITH_OPENGL=ON \
    -DMLN_QT_WITH_LOCATION=OFF \
    -DMLN_QT_WITH_WIDGETS=OFF \
"

# The QML module installs to ${prefix}/qml, a path hard-coded upstream; Qt
# looks for modules in its own QML directory, so move it there.
do_install:append() {
    if [ -d ${D}${prefix}/qml ]; then
        install -d ${D}${QT6_INSTALL_QMLDIR}
        mv ${D}${prefix}/qml/MapLibre ${D}${QT6_INSTALL_QMLDIR}/
        rmdir ${D}${prefix}/qml
    fi
}

FILES:${PN} += "${QT6_INSTALL_QMLDIR}/MapLibre"
