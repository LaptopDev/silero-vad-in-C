// ====================================================================
//  Real-time Silero VAD using ONNX Runtime + miniaudio microphone input
//  GLOBAL TIMESTAMPS + manual reset + idle auto-reset
//  - Global timestamps never reset
//  - Manual reset:
//      * Press ENTER in terminal, or
//      * touch /tmp/rt_vad_reset (configurable via --reset-file)
//    Prints "(manual reset invoked)"
//  - Idle auto-reset (--idle-reset=N seconds):
//    If no speech for N seconds, resets VAD state and prints "(silence reset)"
//  - Select desktop as source (--source=dt)
// ====================================================================

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <iostream>
#include <vector>
#include <mutex>
#include <memory>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <fstream>
#include <string>

#if defined(_WIN32)
  #include <io.h>
  #define access _access
  #define F_OK 0
#else
  #include <unistd.h>
#endif

// ====================================================================
//  ONNX Runtime + embedded VADIterator (no external headers needed)
// ====================================================================

#include "onnxruntime_cxx_api.h"

class timestamp_t {
public:
    int start;
    int end;
    timestamp_t(int s=-1, int e=-1) : start(s), end(e) {}
};

class VadIterator {
private:
    Ort::Env env;
    Ort::SessionOptions session_options;
    std::shared_ptr<Ort::Session> session = nullptr;
    Ort::AllocatorWithDefaultOptions allocator;
    Ort::MemoryInfo memory_info =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeCPU);

    const int context_samples = 64;
    std::vector<float> _context;

    int window_size_samples;
    int effective_window_size;
    int sr_per_ms;

    std::vector<Ort::Value> ort_inputs;
    std::vector<const char*> input_node_names = {"input","state","sr"};
    std::vector<float> input;

    unsigned int size_state = 2*1*128;
    std::vector<float> _state;

    std::vector<int64_t> sr;
    int64_t input_node_dims[2] = {};
    const int64_t state_node_dims[3] = {2,1,128};
    const int64_t sr_node_dims[1] = {1};

    std::vector<Ort::Value> ort_outputs;
    std::vector<const char*> output_node_names = {"output","stateN"};

    int sample_rate;
    float threshold;
    int min_silence_samples;
    int min_silence_samples_at_max_speech;
    int min_speech_samples;
    float max_speech_samples;
    int speech_pad_samples;

    bool triggered = false;
    unsigned int temp_end = 0;
    unsigned int current_sample = 0;
    int prev_end = 0;
    int next_start = 0;
    std::vector<timestamp_t> speeches;
    timestamp_t current_speech;

    void init_engine_threads(int inter, int intra)
    {
        session_options.SetIntraOpNumThreads(intra);
        session_options.SetInterOpNumThreads(inter);
        session_options.SetGraphOptimizationLevel(
            GraphOptimizationLevel::ORT_ENABLE_ALL
        );
    }

    void init_onnx_model(const std::string &path)
    {
        init_engine_threads(1,1);
        session =
            std::make_shared<Ort::Session>(env, path.c_str(), session_options);
    }

    void reset_states()
    {
        std::memset(_state.data(), 0, _state.size()*sizeof(float));
        triggered=false;
        temp_end=0;
        current_sample=0;
        prev_end=0;
        next_start=0;
        speeches.clear();
        current_speech = timestamp_t();
        std::fill(_context.begin(), _context.end(), 0.f);
    }

public:
    VadIterator(const std::string& ModelPath,
                int Sample_rate = 16000,
                int windows_frame = 32,
                float Threshold = 0.5,
                int min_silence_ms = 100,
                int pad_ms = 30,
                int min_speech_ms = 250,
                float max_speech_s = INFINITY)
        : sample_rate(Sample_rate),
          threshold(Threshold),
          speech_pad_samples(pad_ms)
    {
        sr_per_ms = sample_rate / 1000;

        window_size_samples = windows_frame * sr_per_ms;
        effective_window_size = window_size_samples + context_samples;

        input_node_dims[0] = 1;
        input_node_dims[1] = effective_window_size;

        _state.resize(size_state);
        sr.resize(1);
        sr[0] = sample_rate;

        _context.assign(context_samples, 0.f);

        min_speech_samples = sr_per_ms * min_speech_ms;
        max_speech_samples =
            (sample_rate * max_speech_s -
             window_size_samples -
             2 * speech_pad_samples);

        min_silence_samples = sr_per_ms * min_silence_ms;
        min_silence_samples_at_max_speech = sr_per_ms * 98;

        init_onnx_model(ModelPath);
    }

    void predict(const std::vector<float> &data_chunk)
    {
        // shifted context buffer + chunk
        std::vector<float> new_data(effective_window_size, 0.f);
        std::copy(_context.begin(), _context.end(), new_data.begin());
        std::copy(data_chunk.begin(),
                  data_chunk.end(),
                  new_data.begin() + context_samples);

        input = new_data;

        Ort::Value input_ort =
            Ort::Value::CreateTensor<float>(memory_info,
                                            input.data(),
                                            input.size(),
                                            input_node_dims, 2);

        Ort::Value state_ort =
            Ort::Value::CreateTensor<float>(memory_info,
                                            _state.data(),
                                            _state.size(),
                                            state_node_dims, 3);

        Ort::Value sr_ort =
            Ort::Value::CreateTensor<int64_t>(memory_info,
                                              sr.data(),
                                              sr.size(),
                                              sr_node_dims, 1);

        ort_inputs.clear();
        ort_inputs.emplace_back(std::move(input_ort));
        ort_inputs.emplace_back(std::move(state_ort));
        ort_inputs.emplace_back(std::move(sr_ort));

        ort_outputs =
            session->Run(Ort::RunOptions{nullptr},
                         input_node_names.data(),
                         ort_inputs.data(),
                         ort_inputs.size(),
                         output_node_names.data(),
                         output_node_names.size());

        float speech_prob =
            ort_outputs[0].GetTensorMutableData<float>()[0];

        float *stateN =
            ort_outputs[1].GetTensorMutableData<float>();
        std::memcpy(_state.data(), stateN, size_state*sizeof(float));

        current_sample += window_size_samples;

        // START
        if (speech_prob >= threshold) {
            if (!triggered) {
                triggered = true;
                current_speech.start =
                    current_sample - window_size_samples;
            }
            std::copy(new_data.end() - context_samples,
                      new_data.end(),
                      _context.begin());
            return;
        }

        // END
        if (triggered && speech_prob < (threshold - 0.15)) {
            temp_end = (temp_end == 0) ? current_sample : temp_end;

            if ((current_sample - temp_end) >= min_silence_samples) {
                current_speech.end = temp_end;

                if (current_speech.end -
                    current_speech.start > min_speech_samples)
                {
                    speeches.push_back(current_speech);
                }
                triggered = false;
                temp_end  = 0;
                current_speech = timestamp_t();
            }
        }

        std::copy(new_data.end() - context_samples,
                  new_data.end(),
                  _context.begin());
    }

    bool is_triggered() const { return triggered; }
    int  get_current_start() const { return current_speech.start; }

    const std::vector<timestamp_t> get_speech_timestamps() const {
        return speeches;
    }

    void reset() { reset_states(); }
};


// ====================================================================
//  GLOBALS / STATE
// ====================================================================

static std::unique_ptr<VadIterator> g_vad;
static std::mutex g_mutex;

static const int SAMPLE_RATE = 16000;
static const int CHUNK_SIZE  = 512;

static std::atomic<bool> g_in_speech{false};
static std::vector<float> ring_buffer;

static std::atomic<uint64_t> g_total_samples{0};      // global time; never reset
static std::atomic<uint64_t> g_last_speech_samples{0}; // last time speech was seen

// Manual reset signaling
static std::atomic<bool> g_manual_reset_requested{false};
static std::string g_reset_file = "/tmp/rt_vad_reset";

// Idle auto-reset seconds (0 = disabled)
static std::atomic<int> g_idle_reset_seconds{0};


// ====================================================================
//  AUDIO CALLBACK
// ====================================================================
static void data_callback(ma_device* dev,
                          void* output,
                          const void* input,
                          ma_uint32 frameCount)
{
    (void)dev;
    (void)output;

    const float* in = (const float*)input;
    std::lock_guard<std::mutex> lock(g_mutex);

    ma_uint32 offset = 0;

    while (offset + CHUNK_SIZE <= frameCount)
    {
        std::vector<float> chunk(in + offset,
                                 in + offset + CHUNK_SIZE);
        offset += CHUNK_SIZE;

        g_vad->predict(chunk);
        g_total_samples += CHUNK_SIZE;

        // START
        if (!g_in_speech.load(std::memory_order_relaxed) && g_vad->is_triggered()) {
            uint64_t abs_start = g_total_samples - CHUNK_SIZE;
            double t0 = abs_start / double(SAMPLE_RATE);
            printf("Speech START at %.3f s\n", t0);
            ring_buffer.clear();
            g_in_speech.store(true, std::memory_order_relaxed);
            g_last_speech_samples.store(g_total_samples, std::memory_order_relaxed);
        }

        if (g_in_speech.load(std::memory_order_relaxed)) {
            ring_buffer.insert(ring_buffer.end(), chunk.begin(), chunk.end());
        }

        // END
        if (g_in_speech.load(std::memory_order_relaxed) && !g_vad->is_triggered()) {
            double t1 = g_total_samples / double(SAMPLE_RATE);
            printf("Speech END   at %.3f s\n", t1);

            g_in_speech.store(false, std::memory_order_relaxed);
            ring_buffer.clear();
            g_vad->reset();

            g_last_speech_samples.store(g_total_samples, std::memory_order_relaxed);
        }

        // If still in speech, update "last seen" marker continuously.
        if (g_vad->is_triggered()) {
            g_last_speech_samples.store(g_total_samples, std::memory_order_relaxed);
        }
    }
}


// ====================================================================
//  HELPERS
// ====================================================================

static bool file_exists(const std::string& path) {
#if defined(_WIN32)
    return access(path.c_str(), F_OK) == 0;
#else
    return access(path.c_str(), F_OK) == 0;
#endif
}

static void remove_file(const std::string& path) {
    std::remove(path.c_str());
}

static void do_reset_locked(const char* reason_tag)
{
    // g_mutex must be held by caller
    (void)reason_tag;
    ring_buffer.clear();
    g_vad->reset();
    g_in_speech.store(false, std::memory_order_relaxed);
    // Do NOT reset g_total_samples (global time)
    // Update last speech marker to "now" so we don't instantly fire idle-reset again.
    g_last_speech_samples.store(g_total_samples, std::memory_order_relaxed);
}

static void do_reset_with_log(const char* reason_tag)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    do_reset_locked(reason_tag);
    double t = g_total_samples.load() / double(SAMPLE_RATE);
    printf("%s at %.3f s\n", reason_tag, t);
    fflush(stdout);
}


// Thread: monitors stdin for ENTER and watches reset file; sets g_manual_reset_requested
static void reset_monitor_thread()
{
    // Non-blocking file poll every 200ms and a non-blocking check for ENTER by using std::getline with std::cin.rdbuf()->in_avail.
    // Simpler: block on std::getline in a separate tiny thread, AND poll file here.

    // Inner thread just waits for ENTER
    std::thread enter_thread([](){
        std::string line;
        while (std::getline(std::cin, line)) {
            // Any ENTER press triggers a manual reset request
            g_manual_reset_requested.store(true, std::memory_order_relaxed);
        }
        // If stdin closes, just exit the thread.
    });
    enter_thread.detach();

    // Poll reset file
    while (true) {
        if (!g_reset_file.empty() && file_exists(g_reset_file)) {
            g_manual_reset_requested.store(true, std::memory_order_relaxed);
            remove_file(g_reset_file); // clear the file after signaling
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}


// ====================================================================
//  MAIN
// ====================================================================
int main(int argc, char** argv)
{
    // Parse simple CLI flags: --idle-reset=SECONDS and --reset-file=PATH
    std::string source = "mic";   // default
    
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--source=", 0) == 0) {
            source = a.substr(strlen("--source="));
        } else if (a.rfind("--idle-reset=", 0) == 0) {
            int sec = std::max(0, std::atoi(a.c_str() + 13));
            g_idle_reset_seconds.store(sec);
        } else if (a.rfind("--reset-file=", 0) == 0) {
            g_reset_file = a.substr(13);
        } else {
            std::cerr << "Unknown arg: " << a << "\n";
        }
    }

    // Model path (system-installed)
    const char* model_path = "/usr/local/share/silero-vad/silero_vad.onnx";

    // Init VAD
    g_vad = std::make_unique<VadIterator>(model_path);
    
    // -----------------------------------------------------------
    // Select audio source: mic (default) or dt (desktop monitor)
    // -----------------------------------------------------------
    ma_context ctx;
    ma_context_init(NULL, 0, NULL, &ctx);
    
    ma_device_id selected_id = {};
    bool use_specific_id = false;
    
    if (source == "dt") {
        ma_device_info* playback_devs;
        ma_uint32 playback_count;
        ma_device_info* capture_devs;
        ma_uint32 capture_count;
    
        ma_context_get_devices(&ctx,
                               &playback_devs, &playback_count,
                               &capture_devs, &capture_count);
    
        // Find a PulseAudio/PipeWire monitor source
        bool found = false;
        
        for (ma_uint32 i = 0; i < capture_count; i++) {
            std::string name = capture_devs[i].name;
        
            // Normalize lowercase
            std::string low = name;
            std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        
            if (low.find("monitor") != std::string::npos ||
                (low.find("alsa_output") != std::string::npos &&
                 low.rfind(".monitor") != std::string::npos))
            {
                selected_id = capture_devs[i].id;
                use_specific_id = true;
                found = true;
                std::cout << "Using desktop audio source: " << name << "\n";
                break;
            }
        }
        
        if (!found) {
            std::cerr << "ERROR: --source=dt requested, but no monitor device found.\n";
            return 1;
        }
    }
    
    // -----------------------------------------------------------
    // Configure miniaudio device
    // -----------------------------------------------------------
    ma_device_config cfg =
        ma_device_config_init(ma_device_type_capture);
    
    cfg.capture.format     = ma_format_f32;
    cfg.capture.channels   = 1;
    cfg.sampleRate         = SAMPLE_RATE;
    cfg.periodSizeInFrames = CHUNK_SIZE;
    cfg.noPreSilencedOutputBuffer = MA_TRUE;
    cfg.dataCallback       = data_callback;
    
    ma_device device;
    
    // If a specific capture device (desktop monitor) was found:
    if (use_specific_id) {
        cfg.capture.pDeviceID = &selected_id;
    }
    
    ma_result r = ma_device_init(&ctx, &cfg, &device);
    if (r != MA_SUCCESS) {
        std::cerr << "ERROR: cannot open capture device\n";
        return 1;
    }
    
    if (ma_device_start(&device) != MA_SUCCESS) {
        std::cerr << "ERROR: cannot start capture device\n";
        ma_device_uninit(&device);
        return 1;
    }
    
    std::cout << "Listening with GLOBAL timestamps... Ctrl-C to exit.\n";
    std::cout << "Manual reset: press ENTER or touch " << g_reset_file << "\n";
    
    if (g_idle_reset_seconds.load() > 0) {
        std::cout << "Idle auto-reset: "
                  << g_idle_reset_seconds.load()
                  << "s of silence\n";
    }

// Start reset monitor
std::thread monitor(reset_monitor_thread);
monitor.detach();

    // Main management loop
    while (true) {
        // Manual reset?
        if (g_manual_reset_requested.exchange(false)) {
            do_reset_with_log("(manual reset invoked)");
        }

        // Idle auto-reset?
        int idleSec = g_idle_reset_seconds.load();
        if (idleSec > 0) {
            bool inSpeech = g_in_speech.load(std::memory_order_relaxed);
            if (!inSpeech) {
                uint64_t last = g_last_speech_samples.load(std::memory_order_relaxed);
                uint64_t now  = g_total_samples.load(std::memory_order_relaxed);
                double idle_elapsed_s = (now - last) / double(SAMPLE_RATE);
                if (idle_elapsed_s >= idleSec) {
                    do_reset_with_log("(silence reset)");
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // never reached
    ma_device_uninit(&device);
    return 0;
}
