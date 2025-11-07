TODO:
build all utils with make
confirm audio and model paths can be dynamically set regardless of path given at build time

GUIDE:

Clone the repo

Some scripts require model in ./model/

----------------------------------------------
Dependencies:

Miniaudio
  sudo curl -Lo /usr/local/include/miniaudio.h     https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h




Model Installation & Building and installing binary


**Linux (Arch & other distros)**
Download model:

  wget https://github.com/snakers4/silero-vad/raw/master/files/silero_vad.onnx -O silero_vad.onnx

Install binary + model (Makefile default):

  sudo make install

Installs to:
/usr/local/bin/vad
/usr/local/share/silero-vad/silero_vad.onnx




**macOS**
Download model:

  wget https://github.com/snakers4/silero-vad/raw/master/files/silero_vad.onnx -O silero_vad.onnx

Intel macOS (default prefix /usr/local):

  sudo make install PREFIX=/usr/local

Apple Silicon (Homebrew prefix /opt/homebrew):

  sudo make install PREFIX=/opt/homebrew

Installs to:
$PREFIX/bin/vad
$PREFIX/share/silero-vad/silero_vad.onnx




**Windows (MinGW / MSVC)**
Download model (PowerShell):

  Invoke-WebRequest -Uri "https://github.com/snakers4/silero-vad/raw/master/files/silero_vad.onnx" -OutFile "silero_vad.onnx"

Install using Makefile (MinGW shell):

  make install PREFIX="C:/Program Files/silero-vad" MODEL_DIR="C:/Program Files/silero-vad/models"

Files installed to:
C:\Program Files\silero-vad\bin\vad.exe
C:\Program Files\silero-vad\models\silero_vad.onnx

---

Build:

  make

Install (Linux/macOS default prefix /usr/local):

  sudo make install

Uninstall:

  sudo make uninstall

Rebuild cleanly:

  make clean



---
Utilities:
vad                 run on a wav; show vad, don't know how it shows timestamps (it is either by timestamps or duration); redirect output to file (> output)
find_silence        run on output of `vad` program; sorts longest silences first; to save sorted list, redirect output to file (> output); demo only
unstable_rt_vad     run on mic input; detection normalization is force reset after each start&end (at which time, time count is reset); realtime stdo
rt_aad              run on mic input; show audio activity detection based on threshhold; optionally, play a sound when audio detected
rt_vad_global_reset run on mic or desktop input; show realtime vad; manual reset context window (pressing `enter`) for vad accuracy control in noise pollted speech streams; optional automatic reset on silence (REDUCES misalignment and repeated framing)


You can alternatively compile each program using the compilation commands below:

Compile vad
g++ -O3 -march=native -std=gnu++17     vad.cpp     -I/usr/include/onnxruntime     -lonnxruntime -lpthread     -o vad

Compile unstable_rt_vad
g++ -O3 -march=native -std=gnu++17     unstable_rt_vad.cpp     -I/usr/include/onnxruntime     -lonnxruntime -lpthread     -o unstable_rt_vad

Compile find_silence
gcc -O3 -std=c11 -o find_silence find_silence.c

Compile rt_aad
g++ -O3 -march=native -std=gnu++17 rt_aad.cpp -lm -lpthread -o rt_aad

Compile rt_vad_global_reset
g++ -O3 -march=native -std=gnu++17     rt_vad_global_reset.cpp     -I/usr/include/onnxruntime     -lonnxruntime -lpthread     -o rt_vad_global_reset



audio activity detection command example
./rt_aad drop.wav .004
Suggested values for microphone pickup
    Room silence	0.001 – 0.005
    Quiet breathing	0.005 – 0.01
    Soft speech	0.01 – 0.03
    Normal speech	0.03 – 0.08
    Loud speech	0.08 – 0.2
    Claps / bangs	0.2 – 1.0

