# TODO

-   Build all utils with `make`
-   Confirm audio and model paths can be dynamically set regardless of
    build-time paths

------------------------------------------------------------------------

# Guide

## Clone the repo

Some scripts require the model in `./model/`.

------------------------------------------------------------------------

# Dependencies

### Miniaudio

``` sh
sudo curl -Lo /usr/local/include/miniaudio.h \
  https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h
```

------------------------------------------------------------------------

## Linux (Arch & other distros)

### Download model

``` sh
wget https://github.com/snakers4/silero-vad/raw/master/files/silero_vad.onnx -O silero_vad.onnx
```

### Install (Makefile default)

``` sh
sudo make install
```

Installs to:

    /usr/local/bin/vad
    /usr/local/share/silero-vad/silero_vad.onnx

------------------------------------------------------------------------

## macOS

### Download model

``` sh
wget https://github.com/snakers4/silero-vad/raw/master/files/silero_vad.onnx -O silero_vad.onnx
```

### Install

Intel macOS (default `/usr/local`):

``` sh
sudo make install PREFIX=/usr/local
```

Apple Silicon (Homebrew `/opt/homebrew`):

``` sh
sudo make install PREFIX=/opt/homebrew
```

Installs to:

    $PREFIX/bin/vad
    $PREFIX/share/silero-vad/silero_vad.onnx

------------------------------------------------------------------------

## Windows (MinGW / MSVC)

### Download model (PowerShell)

``` powershell
Invoke-WebRequest -Uri "https://github.com/snakers4/silero-vad/raw/master/files/silero_vad.onnx" -OutFile "silero_vad.onnx"
```

### Install using Makefile (MinGW shell)

``` sh
make install PREFIX="C:/Program Files/silero-vad" MODEL_DIR="C:/Program Files/silero-vad/models"
```

Files installed to:

    C:\Program Files\silero-vad\bin\vad.exe
    C:\Program Files\silero-vad\models\silero_vad.onnx


------------------------------------------------------------------------

# Included Utilities

### `vad`

Run on a WAV file; prints VAD output. Redirect if needed:

``` sh
vad input.wav > output
```

### `find_silence`

Prints silence segments from `vad` output, sort with `--long` or `--short`:

``` sh
find_silence [arg] vad_output > sorted
```

### `unstable_rt_vad`

Realtime mic VAD. Detection normalization is force reset after each start&end; realtime output; apparent duplicate frames issue

### `rt_aad`

Show microphone audio activity detection events at or above given threshhold; show peak levels and timestamps; optionally, play a sound on each detection.

``` sh
./rt_aad drop.wav .004
```

### `rt_vad_global_reset`

Realtime VAD on mic or desktop stream. Manual and auto reset reduces misalignment and repeated framing. Logs resets.

``` sh
./rt_vad_global_reset               # mic
./rt_vad_global_reset [--source=dt] # desktop
```


------------------------------------------------------------------------

# Manual Compilation

### `vad`

``` sh
g++ -O3 -march=native -std=gnu++17 vad.cpp \
  -I/usr/include/onnxruntime -lonnxruntime -lpthread \
  -o vad
```

### `unstable_rt_vad`

``` sh
g++ -O3 -march=native -std=gnu++17 unstable_rt_vad.cpp \
  -I/usr/include/onnxruntime -lonnxruntime -lpthread \
  -o unstable_rt_vad
```

### `find_silence`

``` sh
gcc -O3 -std=c11 -o find_silence find_silence.c
```

### `rt_aad`

``` sh
g++ -O3 -march=native -std=gnu++17 rt_aad.cpp -lm -lpthread -o rt_aad
```

### `rt_vad_global_reset`

``` sh
g++ -O3 -march=native -std=gnu++17 rt_vad_global_reset.cpp \
  -I/usr/include/onnxruntime -lonnxruntime -lpthread \
  -o rt_vad_global_reset
```

------------------------------------------------------------------------

# Audio Activity Detection Example



### Suggested microphone thresholds

  Condition         Threshold
  ----------------- --------------
  Room silence      0.001--0.005
  Quiet breathing   0.005--0.01
  Soft speech       0.01--0.03
  Normal speech     0.03--0.08
  Loud speech       0.08--0.2
  Claps / bangs     0.2--1.0
