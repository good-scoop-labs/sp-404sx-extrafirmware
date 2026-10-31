# Build environment for SP-404SX/A EXTRAfirmware (the SH cross compiler is Linux-only).
#   docker build -t sp404sx-extrafirmware .
#   docker run --rm -v "$PWD":/work sp404sx-extrafirmware python3 src/tools/build.py official/sp404sx_up.bin -o out/sp404sx_up.bin
#   docker run --rm -v "$PWD":/work sp404sx-extrafirmware sh src/tests/run_tests.sh
# Release v2.00 was built with Ubuntu 24.04's gcc-sh4-linux-gnu 13.3.0.
FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        python3 python3-pip gcc libc6-dev gcc-sh4-linux-gnu binutils-sh4-linux-gnu nodejs \
    && pip3 install --break-system-packages capstone \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
