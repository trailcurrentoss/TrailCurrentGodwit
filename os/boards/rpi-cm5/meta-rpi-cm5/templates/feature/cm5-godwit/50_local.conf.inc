# Godwit head unit on the CM5. The Pi-specific parts of the HMI are in
# meta-rpi-cm5/dynamic-layers/godwit.

# The GNSS receiver is on the 40-pin header's I2C (pins 3 and 5). This turns
# on that bus in config.txt and installs i2c-dev, but does not load it.
ENABLE_I2C:rpi = "1"
# Without i2c-dev there are no /dev/i2c-* nodes, so no /dev/gnss-i2c
KERNEL_MODULE_AUTOLOAD:append:rpi = " i2c-dev"
