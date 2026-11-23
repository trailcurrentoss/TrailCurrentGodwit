SUMMARY = "The TrailCurrent CAN bus messages, as a DBC file"
DESCRIPTION = "TrailCurrent.dbc from the TrailCurrent documentation: every \
message on the TrailCurrent bus, its signals, their layout and scaling. The \
HMI reads it to decode the lights, energy, water, leveling, thermostat and \
safety modules and to encode the commands it sends them."
HOMEPAGE = "https://github.com/trailcurrentoss/TrailCurrentDocumentation"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=e3ddaed20fb6fe32fbf4ba0f8e6ee20c"

# The DBC is kept in one place, the documentation. Move SRCREV forward when it
# gains a message the HMI uses.
SRC_URI = "git://github.com/trailcurrentoss/TrailCurrentDocumentation.git;protocol=https;branch=main"
SRCREV = "6f9fe631f18c0723be55d2e925fc356aef6dfa4c"
PV = "1.0.0+git"

inherit allarch

do_configure[noexec] = "1"
do_compile[noexec] = "1"

# Beside AGL's own DBC files, where the vehicle bus's will go too
do_install() {
    install -Dm 0644 ${S}/TrailCurrent.dbc ${D}${datadir}/dbc/TrailCurrent.dbc
}

FILES:${PN} = "${datadir}/dbc/TrailCurrent.dbc"
