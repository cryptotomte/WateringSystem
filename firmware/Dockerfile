# WateringSystem firmware build environment.
#
# This Dockerfile exists solely to pin the ESP-IDF toolchain version used for
# all firmware builds (local and CI must produce identical results). The
# upstream espressif/idf image already contains the full toolchain, Python
# environment and idf.py — nothing else is needed.
#
# Usage (from the firmware/ directory):
#   docker build -t wateringsystem-idf .
#   docker run --rm -v "$PWD":/project wateringsystem-idf idf.py build
#
# Or skip the build entirely and use the upstream image directly:
#   docker run --rm -v "$PWD":/project -w /project espressif/idf:v6.0.1 idf.py build

FROM espressif/idf:v6.0.1

WORKDIR /project
