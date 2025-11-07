// rt_aad.cpp — real-time amplitude-based audio activity detector

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <iostream>
#include <vector>
#include <mutex>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <chrono>
#include <ctime>
#include <iomanip>

// return "YYYY-MM-DD HH:MM:SS"
static std::string now_datetime()
{
    using namespace std::chrono;
    auto t  = system_clock::now();
    std::time_t tt = system_clock::to_time_t(t);

    std::tm tm{};
    localtime_r(&tt, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}


static const int SAMPLE_RATE = 16000;
static const int CHUNK_SIZE  = 512;

static std::mutex g_mutex;

static bool  in_activity = false;
static int   current_start_sample = 0;
static int   current_sample = 0;

static float g_threshold = 0.02f;    // user-configured

static ma_engine g_engine;
static std::string g_sound_path;

// ------------------------------------------------------------
// compute RMS of a chunk
// ------------------------------------------------------------
float compute_rms(const float* x, int n)
{
    float s = 0.0f;
    for (int i = 0; i < n; i++)
        s += x[i] * x[i];
    return std::sqrt(s / n);
}

// ------------------------------------------------------------
// miniaudio callback
// ------------------------------------------------------------
static void data_callback(ma_device* dev, void* output,
                          const void* input, ma_uint32 frameCount)
{
    (void)output;
    const float* in = (const float*)input;

    std::lock_guard<std::mutex> lock(g_mutex);

    ma_uint32 offset = 0;
    while (offset + CHUNK_SIZE <= frameCount) {

        const float* chunk = in + offset;
        offset += CHUNK_SIZE;

        float rms = compute_rms(chunk, CHUNK_SIZE);

        bool active = (rms >= g_threshold);

        // START event
        if (!in_activity && active) {
            current_start_sample = current_sample;
            double t0 = current_start_sample / double(SAMPLE_RATE);
            printf("Noise START at %.3f s (rms=%.4f) [%s]\n",
                   t0, rms, now_datetime().c_str());
        
            in_activity = true;
        
            // fire overlapping playback
            ma_engine_play_sound(&g_engine, g_sound_path.c_str(), NULL);
        }
        
        // END event
        if (in_activity && !active) {
            int end_sample = current_sample;
            double t1 = end_sample / double(SAMPLE_RATE);
            printf("Noise END   at %.3f s [%s]\n",
                   t1, now_datetime().c_str());
            in_activity = false;
        }

        current_sample += CHUNK_SIZE;
    }
}

// ------------------------------------------------------------
// MAIN
// ------------------------------------------------------------
int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "Usage: ./rt_aad <sound.wav> [threshold]\n";
        return 1;
    }
    g_sound_path = argv[1];
    
    if (argc > 2) {
        g_threshold = std::atof(argv[2]);
    }
    
    ma_result er = ma_engine_init(NULL, &g_engine);
    if (er != MA_SUCCESS) {
        std::cerr << "ERROR: cannot init playback engine.\n";
        return 1;
    }

    // setup mic
    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format     = ma_format_f32;
    cfg.capture.channels   = 1;
    cfg.sampleRate         = SAMPLE_RATE;
    cfg.periodSizeInFrames = CHUNK_SIZE;
    cfg.dataCallback       = data_callback;

    ma_device dev;
    if (ma_device_init(NULL, &cfg, &dev) != MA_SUCCESS) {
        std::cerr << "ERROR: cannot open default microphone.\n";
        return 1;
    }

    if (ma_device_start(&dev) != MA_SUCCESS) {
        std::cerr << "ERROR: cannot start microphone.\n";
        ma_device_uninit(&dev);
        return 1;
    }

    std::cout << "Listening... Ctrl-C to quit.\n";
    while (true)
        ma_sleep(1000);

    ma_device_uninit(&dev);
    return 0;
}
