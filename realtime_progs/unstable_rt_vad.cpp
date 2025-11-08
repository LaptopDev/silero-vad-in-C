// ====================================================================
//  Real-time Silero VAD using ONNX Runtime + miniaudio microphone input
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

// ---------------------------
//   WAV Reader for VAD class
// ---------------------------
namespace wav {

class WavReader {
private:
    std::vector<float> dataf;
    int sample_rate;
    int samples;

public:
    WavReader(const char* path)
    {
        FILE* fp = fopen(path, "rb");
        if (!fp) { sample_rate = 0; samples = 0; return; }

        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 44, SEEK_SET);

        long pcm = sz - 44;
        samples = pcm / 2;
        dataf.resize(samples);

        for (int i = 0; i < samples; i++) {
            int16_t s;
            fread(&s, 1, 2, fp);
            dataf[i] = s / 32768.0f;
        }

        sample_rate = 16000;
        fclose(fp);
    }

    int num_samples() const { return samples; }
    const float* data() const { return dataf.data(); }
};

} // namespace wav


// ====================================================================
//  FULL VAD ITERATOR (your class, unmodified except:
//      - added is_triggered()
//      - added get_current_start()
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
    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeCPU);

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
    int audio_length_samples;

    bool triggered = false;
    unsigned int temp_end = 0;
    unsigned int current_sample = 0;
    int prev_end;
    int next_start = 0;
    std::vector<timestamp_t> speeches;
    timestamp_t current_speech;

    void init_engine_threads(int inter, int intra) {
        session_options.SetIntraOpNumThreads(intra);
        session_options.SetInterOpNumThreads(inter);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    }

    void init_onnx_model(const std::string& path) {
        init_engine_threads(1,1);
        session = std::make_shared<Ort::Session>(env, path.c_str(), session_options);
    }

    void reset_states() {
        std::memset(_state.data(), 0, _state.size()*sizeof(float));
        triggered=false; temp_end=0; current_sample=0;
        prev_end=0; next_start=0;
        speeches.clear();
        current_speech = timestamp_t();
        std::fill(_context.begin(), _context.end(), 0.f);
    }

public:
    VadIterator(const std::string& ModelPath,
                int Sample_rate = 16000, int windows_frame = 32,
                float Threshold = 0.5, int min_silence_ms = 100,
                int pad_ms = 30, int min_speech_ms = 250,
                float max_speech_s = INFINITY)
        : sample_rate(Sample_rate), threshold(Threshold), speech_pad_samples(pad_ms)
    {
        sr_per_ms = sample_rate / 1000;
        window_size_samples = windows_frame * sr_per_ms;
        effective_window_size = window_size_samples + context_samples;
        input_node_dims[0] = 1;
        input_node_dims[1] = effective_window_size;

        _state.resize(size_state);
        sr.resize(1); sr[0] = sample_rate;
        _context.assign(context_samples, 0.f);

        min_speech_samples = sr_per_ms * min_speech_ms;
        max_speech_samples = (sample_rate * max_speech_s - window_size_samples - 2 * speech_pad_samples);
        min_silence_samples = sr_per_ms * min_silence_ms;
        min_silence_samples_at_max_speech = sr_per_ms * 98;

        init_onnx_model(ModelPath);
    }

    void predict(const std::vector<float>& data_chunk) {
        std::vector<float> new_data(effective_window_size, 0.f);
        std::copy(_context.begin(), _context.end(), new_data.begin());
        std::copy(data_chunk.begin(), data_chunk.end(), new_data.begin()+context_samples);
        input = new_data;

        Ort::Value input_ort =
            Ort::Value::CreateTensor<float>(memory_info, input.data(),
                                            input.size(), input_node_dims, 2);

        Ort::Value state_ort =
            Ort::Value::CreateTensor<float>(memory_info, _state.data(),
                                            _state.size(), state_node_dims, 3);

        Ort::Value sr_ort =
            Ort::Value::CreateTensor<int64_t>(memory_info, sr.data(),
                                              sr.size(), sr_node_dims, 1);

        ort_inputs.clear();
        ort_inputs.emplace_back(std::move(input_ort));
        ort_inputs.emplace_back(std::move(state_ort));
        ort_inputs.emplace_back(std::move(sr_ort));

        ort_outputs = session->Run(Ort::RunOptions{nullptr},
                    input_node_names.data(), ort_inputs.data(), ort_inputs.size(),
                    output_node_names.data(), output_node_names.size());

        float speech_prob = ort_outputs[0].GetTensorMutableData<float>()[0];
        float* stateN = ort_outputs[1].GetTensorMutableData<float>();
        std::memcpy(_state.data(), stateN, size_state*sizeof(float));

        current_sample += window_size_samples;

        // START
        if (speech_prob >= threshold) {
            if (!triggered) {
                triggered = true;
                current_speech.start = current_sample - window_size_samples;
            }
            std::copy(new_data.end()-context_samples, new_data.end(), _context.begin());
            return;
        }

        // END
        if (triggered && speech_prob < (threshold - 0.15)) {
            temp_end = (temp_end == 0) ? current_sample : temp_end;

            if ((current_sample - temp_end) >= min_silence_samples) {
                current_speech.end = temp_end;
                if (current_speech.end - current_speech.start > min_speech_samples) {
                    speeches.push_back(current_speech);
                }
                triggered = false;
                temp_end = 0;
                current_speech = timestamp_t();
            }
        }

        std::copy(new_data.end()-context_samples, new_data.end(), _context.begin());
    }

    const std::vector<timestamp_t> get_speech_timestamps() const {
        return speeches;
    }

    void reset() { reset_states(); }

    // --------------------------
    // additions for real-time VAD
    // --------------------------
    bool is_triggered() const { return triggered; }
    int  get_current_start() const { return current_speech.start; }
};


// ====================================================================
//  Realtime microphone VAD using miniaudio
// ====================================================================

static std::unique_ptr<VadIterator> g_vad;
static std::mutex g_mutex;

static const int SAMPLE_RATE  = 16000;
static const int CHUNK_SIZE   = 512;
static bool in_speech = false;
static std::vector<float> ring_buffer;


// ------------------------------------------------------------
//  Audio callback (runs in audio thread)
// ------------------------------------------------------------
static void data_callback(ma_device* dev, void* output,
                          const void* input, ma_uint32 frameCount)
{
    (void)output;
    const float* in = (const float*)input;

    std::lock_guard<std::mutex> lock(g_mutex);

    ma_uint32 offset = 0;
    while (offset + CHUNK_SIZE <= frameCount) {
        std::vector<float> chunk(in + offset, in + offset + CHUNK_SIZE);
        offset += CHUNK_SIZE;

        g_vad->predict(chunk);

        // START
        if (!in_speech && g_vad->is_triggered()) {
            double t0 = g_vad->get_current_start() / double(SAMPLE_RATE);
            printf("Speech START at %.3f s\n", t0);
            ring_buffer.clear();
            in_speech = true;
        }

        if (in_speech) {
            ring_buffer.insert(ring_buffer.end(), chunk.begin(), chunk.end());
        }

        // END
        if (in_speech && !g_vad->is_triggered()) {
            auto segs = g_vad->get_speech_timestamps();
            if (!segs.empty()) {
                auto ts = segs.back();
                double t1 = ts.end / double(SAMPLE_RATE);
                printf("Speech END   at %.3f s\n", t1);
            }
            in_speech = false;
            ring_buffer.clear();
            g_vad->reset();
        }
    }
}


// ------------------------------------------------------------
//  MAIN
// ------------------------------------------------------------
int main()
{
    g_vad = std::make_unique<VadIterator>(
        "/usr/local/share/silero-vad/silero_vad.onnx"
    );

    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format        = ma_format_f32;
    cfg.capture.channels      = 1;
    cfg.sampleRate            = SAMPLE_RATE;
    cfg.periodSizeInFrames    = CHUNK_SIZE;
    cfg.noPreSilencedOutputBuffer = MA_TRUE;
    cfg.dataCallback          = data_callback;

    ma_device device;
    if (ma_device_init(NULL, &cfg, &device) != MA_SUCCESS) {
        std::cerr << "ERROR: cannot open default microphone\n";
        return 1;
    }

    if (ma_device_start(&device) != MA_SUCCESS) {
        std::cerr << "ERROR: cannot start microphone\n";
        ma_device_uninit(&device);
        return 1;
    }

    std::cout << "Listening...  Ctrl-C to exit.\n";

    while (true) {
        ma_sleep(1000);
    }

    ma_device_uninit(&device);
    return 0;
}
