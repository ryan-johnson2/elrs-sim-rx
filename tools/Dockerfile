# Build environment for ELRS Sim RX firmware.
# Pico SDK + ARM GCC + CMake/Ninja + picotool, plus host GCC for unit tests.
#
# Build:  docker build -t elrs-sim-rx-build tools/
# Use:    tools/build.sh        (firmware)
#         tools/test.sh         (host unit tests)
#         tools/shell.sh        (interactive shell in the container)

FROM debian:bookworm-slim

ARG PICO_SDK_TAG=2.1.1
ARG PICOTOOL_TAG=2.1.1

ENV DEBIAN_FRONTEND=noninteractive \
    PICO_SDK_PATH=/opt/pico-sdk \
    PICO_BOARD=waveshare_rp2040_zero

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        git \
        ca-certificates \
        python3 \
        pkg-config \
        gcc-arm-none-eabi \
        libnewlib-arm-none-eabi \
        libstdc++-arm-none-eabi-newlib \
        libusb-1.0-0-dev \
    && rm -rf /var/lib/apt/lists/*

# Pico SDK with TinyUSB submodule
RUN git clone --depth 1 --branch ${PICO_SDK_TAG} https://github.com/raspberrypi/pico-sdk.git ${PICO_SDK_PATH} \
    && cd ${PICO_SDK_PATH} \
    && git submodule update --init --depth 1 lib/tinyusb

# picotool (for `picotool info`, UF2 conversion, loading over USB)
RUN git clone --depth 1 --branch ${PICOTOOL_TAG} https://github.com/raspberrypi/picotool.git /tmp/picotool \
    && cmake -S /tmp/picotool -B /tmp/picotool/build -G Ninja -DPICO_SDK_PATH=${PICO_SDK_PATH} \
    && cmake --build /tmp/picotool/build \
    && cmake --install /tmp/picotool/build \
    && rm -rf /tmp/picotool

WORKDIR /work
