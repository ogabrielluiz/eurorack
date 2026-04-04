FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    gcc-arm-none-eabi \
    libnewlib-arm-none-eabi \
    g++ \
    make \
    python3 \
    python3-numpy \
    python3-scipy \
    && rm -rf /var/lib/apt/lists/* \
    && ln -s /usr/bin/python3 /usr/bin/python

WORKDIR /eurorack
COPY . .

# Build firmware (touch resources to skip Python 2 resource regeneration)
# Relax -Werror for CMSIS asm compatibility with modern GCC
RUN touch clouds/resources.cc clouds/resources.h && \
    sed -i 's/-Werror //' stmlib/makefile.inc && \
    TOOLCHAIN_PATH=/usr/ make -f clouds/makefile && \
    arm-none-eabi-objcopy -O binary build/clouds/clouds.elf build/clouds/clouds.bin

# Build desktop test
RUN mkdir -p clouds/test/build/clouds_test && \
    for f in stmlib/dsp/atan.cc clouds/test/clouds_test.cc clouds/dsp/correlator.cc \
             clouds/dsp/granular_processor.cc clouds/dsp/mu_law.cc stmlib/utils/random.cc \
             clouds/resources.cc clouds/dsp/pvoc/frame_transformation.cc \
             clouds/dsp/pvoc/phase_vocoder.cc clouds/dsp/pvoc/stft.cc \
             stmlib/dsp/units.cc; do \
        g++ -c -DTEST -g -Wall -Wno-unused-local-typedefs -I. "$f" \
            -o "clouds/test/build/clouds_test/$(basename ${f%.cc}).o"; \
    done && \
    g++ -o clouds/test/clouds_test clouds/test/build/clouds_test/*.o -lm

# Generate WAV firmware file
RUN PYTHONPATH=/eurorack python3 stm_audio_bootloader/qpsk/encoder.py \
    -t stm32f4 -s 48000 -b 12000 -c 6000 -p 256 \
    build/clouds/clouds.bin || echo "WAV generation may need Python 3 fixes"
