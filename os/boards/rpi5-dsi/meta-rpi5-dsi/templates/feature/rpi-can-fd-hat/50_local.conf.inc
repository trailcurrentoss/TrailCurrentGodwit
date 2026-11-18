# Waveshare 2-CH CAN FD HAT: two MCP2518FD controllers on SPI, in the HAT's
# default mode A (no resistors moved):
#   can0  SPI0 CE0, interrupt on GPIO 25
#   can1  SPI1 CE0, interrupt on GPIO 24
# The mcp251xfd driver is already in the image. Each port's role, bit rates,
# CAN FD and listen-only mode are set in the HMI's Settings > CAN and applied
# by godwit-can (meta-godwit).
ENABLE_SPI_BUS:rpi = "1"
RPI_KERNEL_DEVICETREE_OVERLAYS:append:rpi = " overlays/spi1-3cs.dtbo overlays/mcp251xfd.dtbo"
RPI_EXTRA_CONFIG:append:rpi = "\n# Waveshare 2-CH CAN FD HAT, mode A\ndtoverlay=spi1-3cs\ndtoverlay=mcp251xfd,spi0-0,interrupt=25\ndtoverlay=mcp251xfd,spi1-0,interrupt=24\n"

# Settings > CAN, candump/cansend, and an ip that can show a CAN link's state
# and error counters
IMAGE_INSTALL:append = " godwit-can can-utils iproute2"
