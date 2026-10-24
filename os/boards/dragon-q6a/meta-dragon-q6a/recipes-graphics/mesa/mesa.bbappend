# Adreno 643: freedreno (GL/GLES) and turnip (Vulkan). OE-core only enables
# GPU drivers by default on x86, so without this the image renders in software.
PACKAGECONFIG:append:dragon-q6a = " freedreno"
