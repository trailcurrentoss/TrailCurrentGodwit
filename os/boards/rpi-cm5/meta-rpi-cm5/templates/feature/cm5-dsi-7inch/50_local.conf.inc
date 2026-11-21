# 7" 800x480 DSI touch panel (an Elecrow, which works like the original
# Raspberry Pi Touch Display) on the base board's CAM/DISP 0 connector.
#
# The image already has everything the panel needs: the overlay
# (vc4-kms-dsi-7inch.dtbo is in meta-raspberrypi's default list) and its
# drivers as modules (tc358762 bridge, rpi-panel-attiny-regulator backlight,
# panel-simple, edt-ft5x06 touch). What's missing is the line that loads the
# overlay. A Compute Module ignores display_auto_detect, so it has to be set
# explicitly. For the CAM/DISP 1 connector, drop ",dsi0".
#
# With no panel connected the overlay finds nothing on its I2C bus and adds no
# display, so HDMI keeps working.
RPI_EXTRA_CONFIG:append:rpi = "\n# 7in 800x480 DSI touch panel on CAM/DISP 0\ndtoverlay=vc4-kms-dsi-7inch,dsi0\n"
