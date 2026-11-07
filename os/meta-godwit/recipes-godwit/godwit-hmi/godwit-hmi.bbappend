# The voice assistant: the HMI listens for "Hey Peregrine" with godwit-wake.
# Each board's layer adds its language model, where the board has one.
RRECOMMENDS:${PN} += "godwit-wake"
# The Radio app's FM tuner, on an RTL-SDR USB dongle
RRECOMMENDS:${PN} += "godwit-radio"
# The Aircraft app's tables of aircraft types and routes
RRECOMMENDS:${PN} += "godwit-aircraft-data"
