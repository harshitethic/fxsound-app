/*
 * FxSound macOS CoreAudio bridge
 * Uses the upstream FxSound DfxDsp engine (AGPL-3.0-or-later).
 */
#include "DfxDsp.h"
#include "FxNativeTapBridge.h"
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
constexpr uint32_t kRate = 48000;
constexpr uint32_t kChannels = 2;
constexpr int kWindowsDefaultEqBands = 10;
// FxSound's 64-bit Windows path recommends ~40 ms average delay. Its WASAPI
// buffers are roughly twice that length and playback starts after half-fill.
// Keep the Linux bridge in the same operating envelope by default.
constexpr uint32_t kWindowsAverageDelayMs = 40;
constexpr size_t kWindowsPrimeFrames =
    static_cast<size_t>(kRate) * kWindowsAverageDelayMs / 1000u; // 1920 @ 48 kHz
constexpr size_t kRingFrames = 1u << 14; // ~341 ms, above Windows' 200 ms max buffer
constexpr float kPeakCeiling = 0.977f;   // about -0.2 dBFS

struct Ring {
    std::vector<float> data = std::vector<float>(kRingFrames * kChannels, 0.0f);
    std::atomic<uint64_t> read_pos{0};
    std::atomic<uint64_t> write_pos{0};
    std::atomic<uint64_t> overruns{0};
    std::atomic<uint64_t> underruns{0};
    std::atomic<bool> primed{false};

    void push(const float* src, size_t frames) {
        uint64_t w = write_pos.load(std::memory_order_relaxed);
        uint64_t r = read_pos.load(std::memory_order_acquire);
        size_t used = static_cast<size_t>(w - r);
        size_t room = used < kRingFrames ? kRingFrames - used : 0;
        if (frames > room) {
            overruns.fetch_add(1, std::memory_order_relaxed);
            frames = room;
        }
        for (size_t i = 0; i < frames; ++i) {
            size_t idx = static_cast<size_t>((w + i) % kRingFrames) * kChannels;
            data[idx] = src[i * 2];
            data[idx + 1] = src[i * 2 + 1];
        }
        write_pos.store(w + frames, std::memory_order_release);
    }

    size_t pop(float* dst, size_t frames) {
        uint64_t r = read_pos.load(std::memory_order_relaxed);
        uint64_t w = write_pos.load(std::memory_order_acquire);
        size_t available = static_cast<size_t>(w - r);

        // Windows FxSound does not start playback from an empty render queue.
        // It first fills roughly half of its internal buffer (about 40 ms on a
        // modern 64-bit system), which stabilises the capture/render cadence.
        if (!primed.load(std::memory_order_relaxed)) {
            if (available < kWindowsPrimeFrames) {
                std::fill(dst, dst + frames * kChannels, 0.0f);
                return 0;
            }
            primed.store(true, std::memory_order_release);
        }

        size_t take = std::min(frames, available);
        for (size_t i = 0; i < take; ++i) {
            size_t idx = static_cast<size_t>((r + i) % kRingFrames) * kChannels;
            dst[i * 2] = data[idx];
            dst[i * 2 + 1] = data[idx + 1];
        }
        if (take < frames) {
            std::fill(dst + take * kChannels, dst + frames * kChannels, 0.0f);
            underruns.fetch_add(1, std::memory_order_relaxed);
            primed.store(false, std::memory_order_release);
        }
        read_pos.store(r + take, std::memory_order_release);
        return take;
    }
};

struct OutputDevice {
    AudioDeviceID id{kAudioObjectUnknown};
    std::string name;        // stable CoreAudio UID
    std::string description; // human-readable device name
};

constexpr size_t kMaxCallbackFrames = 8192;

struct App {
    AudioDeviceID capture_device{kAudioObjectUnknown};
    AudioDeviceID output_device{kAudioObjectUnknown};
    AudioDeviceID original_default_output{kAudioObjectUnknown};
    AudioDeviceID original_system_output{kAudioObjectUnknown};
    AudioDeviceIOProcID capture_proc{};
    AudioDeviceIOProcID output_proc{};
    void* native_tap_handle{nullptr};
    bool using_native_tap{false};
    bool changed_system_defaults{false};
    std::atomic<bool> running{true};
    DfxDsp dsp;
    std::mutex dsp_mutex;
    std::string current_preset{"General"};
    bool preset_modified{false};
    bool preset_dirty{false};
    std::chrono::steady_clock::time_point last_autosave{std::chrono::steady_clock::now()};
    bool power_on{true};
    std::atomic<bool> control_running{true};
    int control_fd{-1};
    std::thread control_thread;
    Ring ring;
    bool peak_guard_enabled{false};
    float limiter_gain{1.0f};
    std::atomic<uint64_t> input_frames{0};
    std::atomic<uint64_t> output_frames{0};
    std::atomic<float> raw_peak_in{0.0f};
    std::atomic<float> peak_in{0.0f};
    std::atomic<float> peak_out{0.0f};
    std::mutex outputs_mutex;
    std::vector<OutputDevice> outputs;
    std::string selected_output_name;
    std::array<float, kMaxCallbackFrames * kChannels> capture_scratch{};
    std::array<float, kMaxCallbackFrames * kChannels> output_scratch{};
};

App* g_app = nullptr;

struct FactoryPreset {
    const char* name;
    const char* file;
};

constexpr std::array<FactoryPreset, 13> kFactoryPresets = {{
    {"General", "General.fac"},
    {"Music", "Music.fac"},
    {"Voice", "Voice.fac"},
    {"Volume Boost", "Volume_Boost.fac"},
    {"Gaming", "Gaming.fac"},
    {"Classic Processing", "Classic_Processing.fac"},
    {"Light Processing", "Light_Processing.fac"},
    {"Bass Boost", "Bass_Boost.fac"},
    {"Streaming Video", "Streaming_Video.fac"},
    {"Movies", "Movies.fac"},
    {"TV", "TV.fac"},
    {"Transcription", "Transcription.fac"},
    {"Default", "Default.fac"},
}};

std::filesystem::path app_support_dir() {
    if (const char* home = std::getenv("HOME"))
        return std::filesystem::path(home) / "Library/Application Support/FxSound";
    return std::filesystem::path(".");
}

std::filesystem::path state_dir() {
    return app_support_dir() / "state";
}

std::filesystem::path preset_dir() {
    return app_support_dir() / "presets";
}

std::filesystem::path autosave_dir() {
    return app_support_dir() / "autosave";
}

std::filesystem::path autosave_path_for_name(const std::string& name) {
    return autosave_dir() / (name + ".fac");
}

const FactoryPreset* find_factory_preset(const std::string& name) {
    for (const auto& preset : kFactoryPresets)
        if (name == preset.name) return &preset;
    return nullptr;
}

std::string load_saved_preset_name() {
    std::ifstream in(state_dir() / "current-preset");
    std::string name;
    std::getline(in, name);
    return find_factory_preset(name) ? name : "General";
}

void save_preset_name(const std::string& name) {
    std::error_code ec;
    std::filesystem::create_directories(state_dir(), ec);
    std::ofstream out(state_dir() / "current-preset", std::ios::trunc);
    if (out) out << name << "\n";
}

std::string load_saved_output_name() {
    std::ifstream in(state_dir() / "output-node");
    std::string name;
    std::getline(in, name);
    const auto first = name.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = name.find_last_not_of(" \t\r\n");
    return name.substr(first, last - first + 1);
}

void save_output_name(const std::string& name) {
    std::error_code ec;
    std::filesystem::create_directories(state_dir(), ec);
    std::ofstream out(state_dir() / "output-node", std::ios::trunc);
    if (out) out << name << "\n";
}

struct PersistentAudioSettings {
    int bands{kWindowsDefaultEqBands};
    float volume_leveling{0.0f};
    float master_gain{0.0f};
    float balance{0.0f};
    float filter_q{1.0f};
    bool power{true};
};

PersistentAudioSettings load_audio_settings() {
    PersistentAudioSettings settings;
    std::ifstream in(state_dir() / "audio-settings.conf");
    std::string line;
    while (std::getline(in, line)) {
        const auto split = line.find('=');
        if (split == std::string::npos) continue;
        const std::string key = line.substr(0, split);
        const std::string value = line.substr(split + 1);
        try {
            if (key == "bands") settings.bands = std::stoi(value);
            else if (key == "volume_leveling") settings.volume_leveling = std::stof(value);
            else if (key == "master_gain") settings.master_gain = std::stof(value);
            else if (key == "balance") settings.balance = std::stof(value);
            else if (key == "filter_q") settings.filter_q = std::stof(value);
            else if (key == "power") settings.power = (value != "0");
        } catch (...) {
            // Keep the Windows defaults for malformed settings.
        }
    }

    if (!(settings.bands == 5 || settings.bands == 10 || settings.bands == 15 ||
          settings.bands == 20 || settings.bands == 31))
        settings.bands = kWindowsDefaultEqBands;
    settings.volume_leveling = std::clamp(
        std::round(settings.volume_leveling * 2.0f) / 2.0f, 0.0f, 4.0f);
    settings.master_gain = std::clamp(std::round(settings.master_gain), -20.0f, 20.0f);
    settings.balance = std::clamp(std::round(settings.balance), -20.0f, 20.0f);
    settings.filter_q = std::clamp(
        std::round(settings.filter_q * 2.0f) / 2.0f, 1.0f, 3.0f);
    return settings;
}

void save_audio_settings_unlocked(App& app) {
    std::error_code ec;
    std::filesystem::create_directories(state_dir(), ec);
    std::ofstream out(state_dir() / "audio-settings.conf", std::ios::trunc);
    if (!out) return;

    out << "bands=" << app.dsp.getNumEqBands() << '\n'
        << "volume_leveling=" << app.dsp.getVolumeLeveling() << '\n'
        << "master_gain=" << app.dsp.getMasterGain() << '\n'
        << "balance=" << app.dsp.getBalance() << '\n'
        << "filter_q=" << app.dsp.getFilterQ() << '\n'
        << "power=" << (app.power_on ? 1 : 0) << '\n';
}

std::filesystem::path preset_path_for_name(const std::string& name) {
    if (const auto* preset = find_factory_preset(name))
        return preset_dir() / preset->file;
    return {};
}

std::filesystem::path default_preset_path(App& app) {
    if (const char* requested = std::getenv("FXSOUND_PRESET_PATH")) {
        app.current_preset = "Custom";
        app.preset_modified = false;
        app.preset_dirty = false;
        return std::filesystem::path(requested);
    }

    app.current_preset = load_saved_preset_name();
    const auto autosave = autosave_path_for_name(app.current_preset);
    std::error_code ec;
    if (std::filesystem::exists(autosave, ec)) {
        app.preset_modified = true;
        app.preset_dirty = false;
        return autosave;
    }

    app.preset_modified = false;
    app.preset_dirty = false;
    return preset_path_for_name(app.current_preset);
}

bool apply_preset(App& app, const std::filesystem::path& preset_path) {
    const std::wstring preset_wide = std::filesystem::absolute(preset_path).wstring();

    if (app.dsp.loadPreset(preset_wide) != 0) {
        std::fprintf(stderr, "FxSound: failed to load preset: %s\n", preset_path.c_str());
        return false;
    }

    // Mirror FxSound's Windows controller exactly. loadPreset() decodes the
    // legacy MIDI-scaled .fac state; the controller then reapplies each decoded
    // value to the real-time processor.
    for (int effect = (int)DfxDsp::Fidelity; effect < (int)DfxDsp::NumEffects; ++effect) {
        const auto e = static_cast<DfxDsp::Effect>(effect);
        const float decoded = app.dsp.getEffectValue(e);
        app.dsp.setEffectValue(e, decoded * 10.0f);
    }

    const int bands = app.dsp.getNumEqBands();
    for (int band = 0; band < bands; ++band) {
        app.dsp.setEqBandFrequency(band, app.dsp.getEqBandFrequency(band));
        app.dsp.setEqBandBoostCut(band, app.dsp.getEqBandBoostCut(band));
    }

    // loadPreset() already restores FxSound's stored EQ on/off state.
    // Do not force EQ on here; Windows respects the flag embedded in the .fac.
    return true;
}

bool autosave_current_preset_unlocked(App& app) {
    if (!app.preset_dirty || !find_factory_preset(app.current_preset))
        return true;

    std::error_code ec;
    const auto dir = autosave_dir();
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        std::fprintf(stderr, "FxSound: failed to create autosave directory: %s\n",
                     ec.message().c_str());
        return false;
    }

    std::wstring preset_name(app.current_preset.begin(), app.current_preset.end());
    if (app.dsp.savePreset(preset_name, dir.wstring()) != 0) {
        std::fprintf(stderr, "FxSound: failed to autosave preset: %s\n",
                     app.current_preset.c_str());
        return false;
    }

    app.preset_dirty = false;
    app.last_autosave = std::chrono::steady_clock::now();
    return true;
}

void mark_preset_modified(App& app) {
    app.preset_modified = true;
    app.preset_dirty = true;
}

void configure_dsp(App& app) {
    if (app.dsp.setSignalFormat(32, 2, kRate, 32) != 0) {
        std::fprintf(stderr, "FxSound: setSignalFormat failed\n");
        return;
    }

    const auto settings = load_audio_settings();
    app.power_on = settings.power;
    app.dsp.powerOn(settings.power);

    // Windows persists EQ-band count separately from the selected .fac preset.
    app.dsp.setNumBands(settings.bands);

    const auto preset = default_preset_path(app);
    if (!apply_preset(app, preset)) {
        std::fprintf(stderr, "FxSound: preset load failed; leaving native DSP flat rather than using guessed tuning\n");
    }

    // These are application settings in the Windows controller, not .fac fields.
    app.dsp.setVolumeLeveling(settings.volume_leveling);
    app.dsp.setNormalization(0.0f);
    app.dsp.setFilterQ(settings.filter_q);
    app.dsp.setMasterGain(settings.master_gain);
    app.dsp.setBalance(settings.balance);
}


std::string trim_copy(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::filesystem::path control_socket_path() {
    if (const char* home = std::getenv("HOME"))
        return std::filesystem::path(home) / "Library/Caches/FxSound/control.sock";
    return std::filesystem::path("/tmp/fxsound-macos-control.sock");
}

bool parse_float(const std::string& text, float& value) {
    char* end = nullptr;
    errno = 0;
    const float parsed = std::strtof(text.c_str(), &end);
    if (errno != 0 || end == text.c_str() || !end || *end != '\0' || !std::isfinite(parsed))
        return false;
    value = parsed;
    return true;
}

bool parse_int(const std::string& text, int& value) {
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || !end || *end != '\0')
        return false;
    value = static_cast<int>(parsed);
    return true;
}

bool effect_from_name(std::string name, DfxDsp::Effect& effect) {
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (name == "clarity" || name == "fidelity") {
        effect = DfxDsp::Fidelity;
    } else if (name == "ambience") {
        effect = DfxDsp::Ambience;
    } else if (name == "surround" || name == "surroundsound") {
        effect = DfxDsp::Surround;
    } else if (name == "dynamic" || name == "dynamicboost") {
        effect = DfxDsp::DynamicBoost;
    } else if (name == "bass" || name == "bassboost") {
        effect = DfxDsp::Bass;
    } else {
        return false;
    }
    return true;
}

std::string json_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (unsigned char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}


std::string cf_string_to_utf8(CFStringRef value) {
    if (!value) return {};
    char buffer[1024] = {};
    if (!CFStringGetCString(value, buffer, sizeof(buffer), kCFStringEncodingUTF8))
        return {};
    return buffer;
}

std::string device_string(AudioDeviceID id, AudioObjectPropertySelector selector) {
    AudioObjectPropertyAddress addr{selector, kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    CFStringRef value = nullptr;
    UInt32 size = sizeof(value);
    if (AudioObjectGetPropertyData(id, &addr, 0, nullptr, &size, &value) != noErr || !value)
        return {};
    std::string out = cf_string_to_utf8(value);
    CFRelease(value);
    return out;
}

std::string device_name(AudioDeviceID id) {
    return device_string(id, kAudioObjectPropertyName);
}

std::string device_uid(AudioDeviceID id) {
    return device_string(id, kAudioDevicePropertyDeviceUID);
}

std::vector<AudioDeviceID> all_audio_devices() {
    AudioObjectPropertyAddress addr{kAudioHardwarePropertyDevices,
                                    kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    UInt32 bytes = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, nullptr, &bytes) != noErr)
        return {};
    std::vector<AudioDeviceID> devices(bytes / sizeof(AudioDeviceID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr,
                                   &bytes, devices.data()) != noErr)
        return {};
    return devices;
}

UInt32 device_channels(AudioDeviceID id, AudioObjectPropertyScope scope) {
    AudioObjectPropertyAddress addr{kAudioDevicePropertyStreamConfiguration, scope,
                                    kAudioObjectPropertyElementMain};
    UInt32 bytes = 0;
    if (AudioObjectGetPropertyDataSize(id, &addr, 0, nullptr, &bytes) != noErr || !bytes)
        return 0;
    std::vector<unsigned char> storage(bytes);
    auto* list = reinterpret_cast<AudioBufferList*>(storage.data());
    if (AudioObjectGetPropertyData(id, &addr, 0, nullptr, &bytes, list) != noErr)
        return 0;
    UInt32 channels = 0;
    for (UInt32 i = 0; i < list->mNumberBuffers; ++i)
        channels += list->mBuffers[i].mNumberChannels;
    return channels;
}

AudioDeviceID default_audio_device(AudioObjectPropertySelector selector) {
    AudioObjectPropertyAddress addr{selector,
                                    kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    AudioDeviceID id = kAudioObjectUnknown;
    UInt32 bytes = sizeof(id);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &bytes, &id);
    return id;
}

AudioDeviceID default_output_device() {
    return default_audio_device(kAudioHardwarePropertyDefaultOutputDevice);
}

AudioDeviceID system_output_device() {
    return default_audio_device(kAudioHardwarePropertyDefaultSystemOutputDevice);
}

bool set_audio_device(AudioObjectPropertySelector selector, AudioDeviceID id) {
    if (id == kAudioObjectUnknown) return false;
    AudioObjectPropertyAddress addr{selector,
                                    kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    UInt32 bytes = sizeof(id);
    return AudioObjectSetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr,
                                      bytes, &id) == noErr;
}

bool set_default_output_device(AudioDeviceID id) {
    return set_audio_device(kAudioHardwarePropertyDefaultOutputDevice, id);
}

bool set_system_output_device(AudioDeviceID id) {
    return set_audio_device(kAudioHardwarePropertyDefaultSystemOutputDevice, id);
}

bool set_device_rate(AudioDeviceID id, double rate) {
    AudioObjectPropertyAddress addr{kAudioDevicePropertyNominalSampleRate,
                                    kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    Boolean writable = false;
    if (AudioObjectIsPropertySettable(id, &addr, &writable) != noErr || !writable)
        return true;
    Float64 value = rate;
    return AudioObjectSetPropertyData(id, &addr, 0, nullptr, sizeof(value), &value) == noErr;
}

void set_device_buffer(AudioDeviceID id, UInt32 frames) {
    AudioObjectPropertyAddress addr{kAudioDevicePropertyBufferFrameSize,
                                    kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    Boolean writable = false;
    if (AudioObjectIsPropertySettable(id, &addr, &writable) == noErr && writable)
        AudioObjectSetPropertyData(id, &addr, 0, nullptr, sizeof(frames), &frames);
}

AudioDeviceID find_blackhole() {
    for (auto id : all_audio_devices()) {
        const auto n = device_name(id);
        if (n.find("BlackHole") != std::string::npos &&
            device_channels(id, kAudioDevicePropertyScopeInput) >= 2)
            return id;
    }
    return kAudioObjectUnknown;
}

AudioDeviceID find_device_by_uid(const std::string& uid) {
    for (auto id : all_audio_devices())
        if (device_uid(id) == uid) return id;
    return kAudioObjectUnknown;
}

void refresh_outputs(App& app) {
    std::vector<OutputDevice> values;
    for (auto id : all_audio_devices()) {
        if (device_channels(id, kAudioDevicePropertyScopeOutput) < 2) continue;
        const auto desc = device_name(id);
        if (desc.find("BlackHole") != std::string::npos) continue;
        const auto uid = device_uid(id);
        if (uid.empty()) continue;
        values.push_back({id, uid, desc.empty() ? uid : desc});
    }
    std::sort(values.begin(), values.end(),
              [](const OutputDevice& a, const OutputDevice& b) {
                  return a.description < b.description;
              });
    std::lock_guard<std::mutex> lock(app.outputs_mutex);
    app.outputs = std::move(values);
}

std::string outputs_json(App& app) {
    refresh_outputs(app);
    std::lock_guard<std::mutex> lock(app.outputs_mutex);
    std::ostringstream out;
    out << "{\"ok\":true,\"selected\":\"" << json_escape(app.selected_output_name)
        << "\",\"outputs\":[";
    for (size_t i = 0; i < app.outputs.size(); ++i) {
        if (i) out << ",";
        const auto& device = app.outputs[i];
        out << "{\"id\":" << device.id
            << ",\"name\":\"" << json_escape(device.name)
            << "\",\"description\":\"" << json_escape(device.description) << "\"}";
    }
    out << "]}";
    return out.str();
}

bool output_exists(App& app, const std::string& node_name) {
    refresh_outputs(app);
    std::lock_guard<std::mutex> lock(app.outputs_mutex);
    return std::any_of(app.outputs.begin(), app.outputs.end(),
                       [&](const OutputDevice& d) { return d.name == node_name; });
}

std::string selected_output_name_copy(App& app) {
    std::lock_guard<std::mutex> lock(app.outputs_mutex);
    return app.selected_output_name;
}

std::string status_json(App& app) {
    std::lock_guard<std::mutex> lock(app.dsp_mutex);
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(4);

    out << "{\"ok\":true"
        << ",\"preset\":\"" << app.current_preset << "\""
        << ",\"modified\":" << (app.preset_modified ? "true" : "false")
        << ",\"power\":" << (app.power_on ? "true" : "false")
        << ",\"peak_guard\":" << (app.peak_guard_enabled ? "true" : "false")
        << ",\"output\":\"" << json_escape(selected_output_name_copy(app)) << "\""
        << ",\"effects\":{"
        << "\"clarity\":" << app.dsp.getEffectValue(DfxDsp::Fidelity) * 10.0f
        << ",\"ambience\":" << app.dsp.getEffectValue(DfxDsp::Ambience) * 10.0f
        << ",\"surround\":" << app.dsp.getEffectValue(DfxDsp::Surround) * 10.0f
        << ",\"dynamic\":" << app.dsp.getEffectValue(DfxDsp::DynamicBoost) * 10.0f
        << ",\"bass\":" << app.dsp.getEffectValue(DfxDsp::Bass) * 10.0f
        << "}"
        << ",\"audio\":{"
        << "\"volume_leveling\":" << app.dsp.getVolumeLeveling()
        << ",\"master_gain\":" << app.dsp.getMasterGain()
        << ",\"balance\":" << app.dsp.getBalance()
        << ",\"filter_q\":" << app.dsp.getFilterQ()
        << "}"
        << ",\"bands\":" << app.dsp.getNumEqBands()
        << ",\"engine\":{\"backend\":\""
        << (app.using_native_tap ? "CoreAudioTap" : "BlackHole")
        << "\",\"rate\":" << kRate
        << ",\"channels\":" << kChannels
        << ",\"latency_frames\":" << kWindowsPrimeFrames
        << ",\"latency_ms\":" << kWindowsAverageDelayMs
        << "}"
        << ",\"eq\":[";

    for (int band = 0; band < app.dsp.getNumEqBands(); ++band) {
        if (band) out << ",";
        out << "{\"f\":" << app.dsp.getEqBandFrequency(band)
            << ",\"g\":" << app.dsp.getEqBandBoostCut(band) << "}";
    }

    out << "]"
        << ",\"meters\":{"
        << "\"input_peak\":" << app.raw_peak_in.load(std::memory_order_relaxed)
        << ",\"output_peak\":" << app.peak_out.load(std::memory_order_relaxed)
        << "}"
        << ",\"io\":{"
        << "\"input_frames\":" << app.input_frames.load(std::memory_order_relaxed)
        << ",\"output_frames\":" << app.output_frames.load(std::memory_order_relaxed)
        << ",\"underruns\":" << app.ring.underruns.load(std::memory_order_relaxed)
        << ",\"overruns\":" << app.ring.overruns.load(std::memory_order_relaxed)
        << "}}";
    return out.str();
}

std::string presets_json() {
    std::ostringstream out;
    out << "{\"ok\":true,\"presets\":[";
    for (size_t i = 0; i < kFactoryPresets.size(); ++i) {
        if (i) out << ",";
        out << "\"" << kFactoryPresets[i].name << "\"";
    }
    out << "]}";
    return out.str();
}

bool set_output_device(App& app, const std::string& node_name);

std::string handle_control_command(App& app, const std::string& input) {
    const std::string command = trim_copy(input);
    if (command.empty()) return "{\"ok\":false,\"error\":\"empty command\"}";

    if (command == "STATUS") return status_json(app);
    if (command == "PRESETS") return presets_json();
    if (command == "OUTPUTS") return outputs_json(app);
    if (command.rfind("OUTPUT ", 0) == 0) {
        const std::string node_name = trim_copy(command.substr(7));
        if (node_name.empty() || !output_exists(app, node_name))
            return "{\"ok\":false,\"error\":\"unknown output device\"}";
        if (!set_output_device(app, node_name))
            return "{\"ok\":false,\"error\":\"output switch failed\"}";
        return "{\"ok\":true}";
    }
    if (command == "METERS") {
        const float input_peak = app.raw_peak_in.exchange(0.0f, std::memory_order_relaxed);
        const float output_peak = app.peak_out.exchange(0.0f, std::memory_order_relaxed);
        std::ostringstream out;
        out.setf(std::ios::fixed);
        out.precision(6);
        out << "{\"ok\":true,\"input\":" << input_peak
            << ",\"output\":" << output_peak << "}";
        return out.str();
    }

    if (command == "SPECTRUM") {
        float bands[10] = {};
        {
            std::lock_guard<std::mutex> lock(app.dsp_mutex);
            app.dsp.getSpectrumBandValues(bands, 10);
        }

        std::ostringstream out;
        out.setf(std::ios::fixed);
        out.precision(6);
        out << "{\"ok\":true,\"bands\":[";
        for (int i = 0; i < 10; ++i) {
            if (i) out << ",";
            float value = std::isfinite(bands[i]) ? bands[i] : 0.0f;
            value = std::clamp(value, 0.0f, 1.0f);
            out << value;
        }
        out << "]}";
        return out.str();
    }

    if (command.rfind("PRESET ", 0) == 0) {
        const std::string name = trim_copy(command.substr(7));
        const auto factory_path = preset_path_for_name(name);
        if (factory_path.empty())
            return "{\"ok\":false,\"error\":\"unknown preset\"}";

        std::lock_guard<std::mutex> lock(app.dsp_mutex);
        if (!autosave_current_preset_unlocked(app))
            return "{\"ok\":false,\"error\":\"autosave failed\"}";

        const auto autosave = autosave_path_for_name(name);
        std::error_code ec;
        const bool has_autosave = std::filesystem::exists(autosave, ec);
        const auto& load_path = has_autosave ? autosave : factory_path;
        if (!apply_preset(app, load_path))
            return "{\"ok\":false,\"error\":\"preset load failed\"}";

        app.current_preset = name;
        app.preset_modified = has_autosave;
        app.preset_dirty = false;
        save_preset_name(name);
        return "{\"ok\":true}";
    }

    if (command == "RESET") {
        std::lock_guard<std::mutex> lock(app.dsp_mutex);
        const std::string name = app.current_preset == "Custom" ? "General" : app.current_preset;
        const auto path = preset_path_for_name(name);
        if (path.empty() || !apply_preset(app, path))
            return "{\"ok\":false,\"error\":\"reset failed\"}";

        std::error_code ec;
        std::filesystem::remove(autosave_path_for_name(name), ec);
        app.current_preset = name;
        app.preset_modified = false;
        app.preset_dirty = false;
        save_preset_name(name);
        return "{\"ok\":true}";
    }

    if (command.rfind("POWER ", 0) == 0) {
        const std::string value = trim_copy(command.substr(6));
        if (value != "0" && value != "1")
            return "{\"ok\":false,\"error\":\"power expects 0 or 1\"}";

        const bool on = value == "1";
        std::lock_guard<std::mutex> lock(app.dsp_mutex);
        app.dsp.powerOn(on);
        app.power_on = on;
        save_audio_settings_unlocked(app);
        return "{\"ok\":true}";
    }

    if (command.rfind("EFFECT ", 0) == 0) {
        std::istringstream stream(command.substr(7));
        std::string effect_name, value_text;
        stream >> effect_name >> value_text;

        DfxDsp::Effect effect;
        float value = 0.0f;
        if (!effect_from_name(effect_name, effect) || !parse_float(value_text, value) ||
            value < 0.0f || value > 10.0f)
            return "{\"ok\":false,\"error\":\"invalid effect command\"}";

        std::lock_guard<std::mutex> lock(app.dsp_mutex);
        app.dsp.setEffectValue(effect, value);
        mark_preset_modified(app);
        if (!autosave_current_preset_unlocked(app))
            return "{\"ok\":false,\"error\":\"autosave failed\"}";
        return "{\"ok\":true}";
    }

    if (command.rfind("EQ ", 0) == 0 || command.rfind("EQFREQ ", 0) == 0) {
        const bool frequency = command.rfind("EQFREQ ", 0) == 0;
        const size_t prefix = frequency ? 7 : 3;

        std::istringstream stream(command.substr(prefix));
        std::string band_text, value_text;
        stream >> band_text >> value_text;

        int band = -1;
        float value = 0.0f;
        if (!parse_int(band_text, band) || !parse_float(value_text, value))
            return "{\"ok\":false,\"error\":\"invalid eq command\"}";

        std::lock_guard<std::mutex> lock(app.dsp_mutex);
        if (band < 0 || band >= app.dsp.getNumEqBands())
            return "{\"ok\":false,\"error\":\"eq band out of range\"}";

        if (frequency) {
            float min_freq = 0.0f, max_freq = 0.0f;
            app.dsp.getEqBandFrequencyRange(band, &min_freq, &max_freq);
            if (value < min_freq || value > max_freq)
                return "{\"ok\":false,\"error\":\"frequency out of range\"}";
            app.dsp.setEqBandFrequency(band, value);
        } else {
            if (value < -12.0f || value > 12.0f)
                return "{\"ok\":false,\"error\":\"gain out of range\"}";
            app.dsp.setEqBandBoostCut(band, value);
        }

        mark_preset_modified(app);
        if (!autosave_current_preset_unlocked(app))
            return "{\"ok\":false,\"error\":\"autosave failed\"}";
        return "{\"ok\":true}";
    }

    if (command.rfind("BANDS ", 0) == 0) {
        int bands = 0;
        if (!parse_int(trim_copy(command.substr(6)), bands) ||
            !(bands == 5 || bands == 10 || bands == 15 || bands == 20 || bands == 31))
            return "{\"ok\":false,\"error\":\"bands must be 5, 10, 15, 20, or 31\"}";

        std::lock_guard<std::mutex> lock(app.dsp_mutex);
        if (app.dsp.getNumEqBands() != bands)
            app.dsp.setNumBands(bands);
        save_audio_settings_unlocked(app);
        return "{\"ok\":true}";
    }

    if (command.rfind("AUDIO ", 0) == 0) {
        std::istringstream stream(command.substr(6));
        std::string control, value_text;
        stream >> control >> value_text;
        float value = 0.0f;
        if (!parse_float(value_text, value))
            return "{\"ok\":false,\"error\":\"invalid audio control\"}";

        std::lock_guard<std::mutex> lock(app.dsp_mutex);
        if (control == "volume") {
            if (value < 0.0f || value > 4.0f)
                return "{\"ok\":false,\"error\":\"volume range is 0..4\"}";
            value = std::round(value * 2.0f) / 2.0f;
            app.dsp.setVolumeLeveling(value);
        } else if (control == "master") {
            if (value < -20.0f || value > 20.0f)
                return "{\"ok\":false,\"error\":\"master range is -20..20\"}";
            value = std::round(value);
            app.dsp.setMasterGain(value);
        } else if (control == "balance") {
            if (value < -20.0f || value > 20.0f)
                return "{\"ok\":false,\"error\":\"balance range is -20..20\"}";
            value = std::round(value);
            app.dsp.setBalance(value);
        } else if (control == "filterq") {
            if (value < 1.0f || value > 3.0f)
                return "{\"ok\":false,\"error\":\"filterq range is 1..3\"}";
            value = std::round(value * 2.0f) / 2.0f;

            // GraphicEqSetFilterQ rebuilds the internal EQ grid. Preserve the
            // current centre frequencies so changing Q changes bandwidth only,
            // not the user's/factory preset frequency layout.
            if (std::fabs(app.dsp.getFilterQ() - value) >= 0.001f) {
                const int band_count = app.dsp.getNumEqBands();
                std::vector<float> frequencies;
                frequencies.reserve(static_cast<size_t>(band_count));
                for (int band = 0; band < band_count; ++band)
                    frequencies.push_back(app.dsp.getEqBandFrequency(band));

                app.dsp.setFilterQ(value);

                for (int band = 0; band < band_count; ++band)
                    app.dsp.setEqBandFrequency(
                        band, frequencies[static_cast<size_t>(band)]);
            }
        } else {
            return "{\"ok\":false,\"error\":\"unknown audio control\"}";
        }
        save_audio_settings_unlocked(app);
        return "{\"ok\":true}";
    }

    return "{\"ok\":false,\"error\":\"unknown command\"}";
}

void control_server(App* app) {
    const auto socket_path = control_socket_path();
    std::error_code ec;
    std::filesystem::create_directories(socket_path.parent_path(), ec);
    ::chmod(socket_path.parent_path().c_str(), 0700);
    ::unlink(socket_path.c_str());

    const int server = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (server < 0) {
        std::perror("FxSound control socket");
        return;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const std::string socket_string = socket_path.string();
    if (socket_string.size() >= sizeof(address.sun_path)) {
        std::fprintf(stderr, "FxSound control socket path is too long\n");
        ::close(server);
        return;
    }
    std::strncpy(address.sun_path, socket_string.c_str(), sizeof(address.sun_path) - 1);

    if (::bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        std::perror("FxSound control bind");
        ::close(server);
        return;
    }

    ::chmod(socket_path.c_str(), 0600);
    if (::listen(server, 8) < 0) {
        std::perror("FxSound control listen");
        ::close(server);
        ::unlink(socket_path.c_str());
        return;
    }

    app->control_fd = server;
    std::fprintf(stderr, "FxSound control: %s\n", socket_path.c_str());

    while (app->control_running.load(std::memory_order_relaxed)) {
        const auto now = std::chrono::steady_clock::now();
        if (now - app->last_autosave >= std::chrono::seconds(60)) {
            std::lock_guard<std::mutex> lock(app->dsp_mutex);
            if (app->preset_dirty)
                autosave_current_preset_unlocked(*app);
            else
                app->last_autosave = now;
        }

        pollfd item{server, POLLIN, 0};
        const int ready = ::poll(&item, 1, 250);
        if (ready <= 0 || !(item.revents & POLLIN)) continue;

        const int client = ::accept(server, nullptr, nullptr);
        if (client < 0) continue;

        char buffer[8192] = {};
        const ssize_t received = ::recv(client, buffer, sizeof(buffer) - 1, 0);
        if (received > 0) {
            buffer[received] = '\0';
            std::string response = handle_control_command(*app, buffer);
            response.push_back('\n');
            int no_sigpipe = 1;
            ::setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
            ::send(client, response.data(), response.size(), 0);
        }
        ::close(client);
    }

    ::close(server);
    app->control_fd = -1;
    ::unlink(socket_path.c_str());
}

void update_peak_max(std::atomic<float>& target, float value) {
    float current = target.load(std::memory_order_relaxed);
    while (value > current &&
           !target.compare_exchange_weak(current, value,
                                         std::memory_order_relaxed,
                                         std::memory_order_relaxed)) {
    }
}

void apply_peak_guard(App& app, float* samples, size_t frames) {
    float peak = 0.0f;
    for (size_t i = 0; i < frames * 2; ++i)
        peak = std::max(peak, std::fabs(samples[i]));
    update_peak_max(app.peak_in, peak);

    // Transparent by default: send the real DfxDsp output downstream unchanged.
    // Enabling FXSOUND_PEAK_GUARD=1 restores the Linux safety limiter.
    if (!app.peak_guard_enabled) {
        app.limiter_gain = 1.0f;
        update_peak_max(app.peak_out, peak);
        return;
    }

    float target = 1.0f;
    if (peak > kPeakCeiling && peak > 0.0f)
        target = kPeakCeiling / peak;

    if (target < app.limiter_gain)
        app.limiter_gain = target; // instant attack
    else
        app.limiter_gain += (1.0f - app.limiter_gain) * 0.035f; // gentle release

    float out_peak = 0.0f;
    for (size_t i = 0; i < frames * 2; ++i) {
        samples[i] *= app.limiter_gain;
        out_peak = std::max(out_peak, std::fabs(samples[i]));
    }
    update_peak_max(app.peak_out, out_peak);
}


size_t input_frames_from(const AudioBufferList* list) {
    if (!list || list->mNumberBuffers == 0) return 0;
    const auto& b = list->mBuffers[0];
    if (!b.mData || b.mNumberChannels == 0) return 0;
    return b.mDataByteSize / (sizeof(float) * b.mNumberChannels);
}

size_t output_frames_from(const AudioBufferList* list) {
    return input_frames_from(list);
}

void read_stereo(const AudioBufferList* list, size_t offset, size_t frames, float* dst) {
    if (!list || !dst || list->mNumberBuffers == 0) return;
    if (list->mNumberBuffers == 1 && list->mBuffers[0].mNumberChannels >= 2) {
        const auto* src = static_cast<const float*>(list->mBuffers[0].mData) + offset * 2;
        std::memcpy(dst, src, frames * 2 * sizeof(float));
        return;
    }
    if (list->mNumberBuffers >= 2) {
        const auto* l = static_cast<const float*>(list->mBuffers[0].mData) + offset;
        const auto* r = static_cast<const float*>(list->mBuffers[1].mData) + offset;
        for (size_t i = 0; i < frames; ++i) {
            dst[i * 2] = l[i];
            dst[i * 2 + 1] = r[i];
        }
    }
}

void write_stereo(AudioBufferList* list, size_t frames, const float* src) {
    if (!list || !src || list->mNumberBuffers == 0) return;
    if (list->mNumberBuffers == 1 && list->mBuffers[0].mNumberChannels >= 2) {
        std::memcpy(list->mBuffers[0].mData, src, frames * 2 * sizeof(float));
        return;
    }
    if (list->mNumberBuffers >= 2) {
        auto* l = static_cast<float*>(list->mBuffers[0].mData);
        auto* r = static_cast<float*>(list->mBuffers[1].mData);
        for (size_t i = 0; i < frames; ++i) {
            l[i] = src[i * 2];
            r[i] = src[i * 2 + 1];
        }
    }
}

OSStatus capture_io(AudioDeviceID, const AudioTimeStamp*,
                    const AudioBufferList* input, const AudioTimeStamp*,
                    AudioBufferList*, const AudioTimeStamp*, void* userdata) {
    auto* app = static_cast<App*>(userdata);
    if (!app || !input) return noErr;

    const size_t total = input_frames_from(input);
    size_t offset = 0;
    while (offset < total) {
        const size_t frames = std::min(kMaxCallbackFrames, total - offset);
        float* samples = app->capture_scratch.data();
        read_stereo(input, offset, frames, samples);

        float raw_peak = 0.0f;
        for (size_t i = 0; i < frames * kChannels; ++i)
            raw_peak = std::max(raw_peak, std::fabs(samples[i]));
        update_peak_max(app->raw_peak_in, raw_peak);

        int rc = 0;
        {
            std::unique_lock<std::mutex> lock(app->dsp_mutex, std::try_to_lock);
            if (lock.owns_lock()) {
                rc = app->dsp.processAudio(reinterpret_cast<short int*>(samples),
                                           reinterpret_cast<short int*>(samples),
                                           static_cast<int>(frames), 0);
            }
        }
        if (rc == 0) {
            apply_peak_guard(*app, samples, frames);
            app->ring.push(samples, frames);
            app->input_frames.fetch_add(frames, std::memory_order_relaxed);
        }
        offset += frames;
    }
    return noErr;
}

OSStatus output_io(AudioDeviceID, const AudioTimeStamp*,
                   const AudioBufferList*, const AudioTimeStamp*,
                   AudioBufferList* output, const AudioTimeStamp*, void* userdata) {
    auto* app = static_cast<App*>(userdata);
    if (!app || !output) return noErr;
    const size_t total = output_frames_from(output);
    size_t offset = 0;
    while (offset < total) {
        const size_t frames = std::min(kMaxCallbackFrames, total - offset);
        float* samples = app->output_scratch.data();
        app->ring.pop(samples, frames);

        if (offset == 0 && frames == total) {
            write_stereo(output, frames, samples);
        } else if (output->mNumberBuffers == 1 && output->mBuffers[0].mNumberChannels >= 2) {
            auto* dst = static_cast<float*>(output->mBuffers[0].mData) + offset * 2;
            std::memcpy(dst, samples, frames * 2 * sizeof(float));
        } else if (output->mNumberBuffers >= 2) {
            auto* l = static_cast<float*>(output->mBuffers[0].mData) + offset;
            auto* r = static_cast<float*>(output->mBuffers[1].mData) + offset;
            for (size_t i = 0; i < frames; ++i) {
                l[i] = samples[i * 2];
                r[i] = samples[i * 2 + 1];
            }
        }
        app->output_frames.fetch_add(frames, std::memory_order_relaxed);
        offset += frames;
    }
    return noErr;
}

void stop_output(App& app) {
    if (app.output_device != kAudioObjectUnknown && app.output_proc) {
        AudioDeviceStop(app.output_device, app.output_proc);
        AudioDeviceDestroyIOProcID(app.output_device, app.output_proc);
    }
    app.output_proc = nullptr;
    app.output_device = kAudioObjectUnknown;
}

bool start_output(App& app, AudioDeviceID id) {
    if (id == kAudioObjectUnknown ||
        device_channels(id, kAudioDevicePropertyScopeOutput) < 2)
        return false;
    stop_output(app);
    set_device_rate(id, kRate);
    set_device_buffer(id, 512);
    AudioDeviceIOProcID proc = nullptr;
    if (AudioDeviceCreateIOProcID(id, output_io, &app, &proc) != noErr || !proc)
        return false;
    if (AudioDeviceStart(id, proc) != noErr) {
        AudioDeviceDestroyIOProcID(id, proc);
        return false;
    }
    app.output_device = id;
    app.output_proc = proc;
    app.ring.primed.store(false, std::memory_order_relaxed);
    return true;
}

bool start_capture(App& app, AudioDeviceID id) {
    if (id == kAudioObjectUnknown ||
        device_channels(id, kAudioDevicePropertyScopeInput) < 2)
        return false;
    set_device_rate(id, kRate);
    set_device_buffer(id, 512);
    AudioDeviceIOProcID proc = nullptr;
    if (AudioDeviceCreateIOProcID(id, capture_io, &app, &proc) != noErr || !proc)
        return false;
    if (AudioDeviceStart(id, proc) != noErr) {
        AudioDeviceDestroyIOProcID(id, proc);
        return false;
    }
    app.capture_device = id;
    app.capture_proc = proc;
    return true;
}

void stop_capture(App& app) {
    if (app.capture_device != kAudioObjectUnknown && app.capture_proc) {
        AudioDeviceStop(app.capture_device, app.capture_proc);
        AudioDeviceDestroyIOProcID(app.capture_device, app.capture_proc);
    }
    app.capture_proc = nullptr;
    app.capture_device = kAudioObjectUnknown;
}

bool set_output_device(App& app, const std::string& uid) {
    const auto id = find_device_by_uid(uid);
    if (id == kAudioObjectUnknown ||
        id == app.capture_device ||
        device_channels(id, kAudioDevicePropertyScopeOutput) < 2)
        return false;

    const std::string previous = selected_output_name_copy(app);
    const auto previous_id = find_device_by_uid(previous);

    if (!start_output(app, id)) {
        if (previous_id != kAudioObjectUnknown && previous_id != app.capture_device)
            start_output(app, previous_id);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(app.outputs_mutex);
        app.selected_output_name = uid;
    }
    save_output_name(uid);
    return true;
}

void handle_signal(int) {
    if (g_app) g_app->running.store(false, std::memory_order_relaxed);
}

} // namespace

int main(int argc, char** argv) {
    App app;
    g_app = &app;
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    refresh_outputs(app);

    if (argc > 1 && std::string(argv[1]) == "--list") {
        for (auto id : all_audio_devices()) {
            std::printf("%u\t%s\tuid=%s\tin=%u out=%u\n",
                        (unsigned)id, device_name(id).c_str(), device_uid(id).c_str(),
                        (unsigned)device_channels(id, kAudioDevicePropertyScopeInput),
                        (unsigned)device_channels(id, kAudioDevicePropertyScopeOutput));
        }
        return 0;
    }

    configure_dsp(app);

    app.original_default_output = default_output_device();
    app.original_system_output = system_output_device();

    std::string target = load_saved_output_name();
    AudioDeviceID target_id = find_device_by_uid(target);
    if (target_id == kAudioObjectUnknown ||
        device_channels(target_id, kAudioDevicePropertyScopeOutput) < 2 ||
        device_name(target_id).find("BlackHole") != std::string::npos) {
        target_id = app.original_default_output;
        if (target_id == kAudioObjectUnknown ||
            device_channels(target_id, kAudioDevicePropertyScopeOutput) < 2 ||
            device_name(target_id).find("BlackHole") != std::string::npos) {
            std::lock_guard<std::mutex> lock(app.outputs_mutex);
            target_id = app.outputs.empty() ? kAudioObjectUnknown : app.outputs.front().id;
        }
        target = device_uid(target_id);
    }

    if (target_id == kAudioObjectUnknown || target.empty()) {
        std::fprintf(stderr, "FxSound macOS: no physical stereo output found.\n");
        return 5;
    }

    {
        std::lock_guard<std::mutex> lock(app.outputs_mutex);
        app.selected_output_name = target;
    }

    std::fprintf(stderr, "FxSound macOS startup: starting physical output %s...\n",
                 device_name(target_id).c_str());
    if (!start_output(app, target_id)) {
        std::fprintf(stderr, "FxSound macOS: could not start output device %s.\n",
                     device_name(target_id).c_str());
        return 6;
    }
    std::fprintf(stderr, "FxSound macOS startup: physical output ready.\n");

    // Apple's process-tap API is promising, but on macOS 26.6 the HAL can
    // block indefinitely inside AudioDeviceStart for a private tap aggregate.
    // Keep it available for development, but default to the proven BlackHole
    // backend until the native path is reliable across macOS versions.
    const bool enable_native_tap = [] {
        const char* value = std::getenv("FXSOUND_EXPERIMENTAL_NATIVE_TAP");
        return value && std::string(value) == "1";
    }();

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    char native_tap_error[256] = {};
    const bool native_tap_started = enable_native_tap
        && fxsoundNativeTapStart(capture_io,
                                 &app,
                                 &app.capture_device,
                                 &app.capture_proc,
                                 &app.native_tap_handle,
                                 native_tap_error,
                                 sizeof(native_tap_error));

    if (native_tap_started) {
        app.using_native_tap = true;
        std::fprintf(stderr, "FxSound macOS startup: native CoreAudio system tap ready.\n");

        // One-time migration from the old BlackHole backend: if a previous
        // FxSound session left BlackHole as macOS's default output, restore
        // the selected physical device. Native taps do not need to own the
        // system default and we intentionally do not restore BlackHole later.
        const auto is_legacy_blackhole = [](AudioDeviceID id) {
            return id != kAudioObjectUnknown
                && device_name(id).find("BlackHole") != std::string::npos;
        };
        if (is_legacy_blackhole(app.original_default_output)) {
            set_default_output_device(target_id);
            app.original_default_output = target_id;
            std::fprintf(stderr,
                "FxSound macOS migration: restored physical default output to %s.\n",
                device_name(target_id).c_str());
        }
        if (is_legacy_blackhole(app.original_system_output)) {
            set_system_output_device(target_id);
            app.original_system_output = target_id;
            std::fprintf(stderr,
                "FxSound macOS migration: restored physical system output to %s.\n",
                device_name(target_id).c_str());
        }
    } else {
        if (enable_native_tap) {
            std::fprintf(stderr,
                "FxSound macOS: native system tap unavailable: %s; trying BlackHole fallback.\n",
                native_tap_error[0] ? native_tap_error : "unknown error");
        } else {
            std::fprintf(stderr,
                "FxSound macOS: using stable BlackHole backend; native system tap is experimental on this macOS release.\n");
        }

        app.capture_device = find_blackhole();
        if (app.capture_device == kAudioObjectUnknown) {
            std::fprintf(stderr,
                "FxSound macOS: BlackHole fallback is not installed or not visible.\n");
            stop_output(app);
            return 4;
        }

        if (!start_capture(app, app.capture_device)) {
            std::fprintf(stderr, "FxSound macOS: could not start BlackHole capture.\n");
            stop_output(app);
            return 7;
        }

        auto restore_target_is_virtual = [&](AudioDeviceID id) {
            return id == kAudioObjectUnknown
                || id == app.capture_device
                || device_name(id).find("BlackHole") != std::string::npos;
        };
        if (restore_target_is_virtual(app.original_default_output))
            app.original_default_output = target_id;
        if (restore_target_is_virtual(app.original_system_output))
            app.original_system_output = target_id;

        if (!set_default_output_device(app.capture_device)) {
            std::fprintf(stderr, "FxSound macOS: could not set BlackHole as default output.\n");
            stop_capture(app);
            stop_output(app);
            return 8;
        }
        if (!set_system_output_device(app.capture_device)) {
            std::fprintf(stderr, "FxSound macOS: could not set BlackHole as system output.\n");
            if (app.original_default_output != kAudioObjectUnknown)
                set_default_output_device(app.original_default_output);
            stop_capture(app);
            stop_output(app);
            return 9;
        }
        app.changed_system_defaults = true;
        std::fprintf(stderr, "FxSound macOS startup: BlackHole fallback ready.\n");
    }

    save_output_name(target);
    std::fprintf(stderr,
        "FxSound macOS ready: %s -> DfxDsp -> %s; 48kHz stereo; preset=%s\n",
        app.using_native_tap ? "CoreAudio system tap" : "BlackHole 2ch",
        device_name(target_id).c_str(), app.current_preset.c_str());

    app.control_thread = std::thread(control_server, &app);

    auto next_device_check = std::chrono::steady_clock::now();

    while (app.running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        const auto now = std::chrono::steady_clock::now();
        if (now < next_device_check)
            continue;
        next_device_check = now + std::chrono::seconds(2);

        refresh_outputs(app);

        const std::string selected = selected_output_name_copy(app);
        const AudioDeviceID selected_id = find_device_by_uid(selected);

        if (selected_id != kAudioObjectUnknown) {
            // CoreAudio object IDs can change when a USB/Bluetooth device is
            // unplugged and reconnected even though its stable UID stays the same.
            if (app.output_device != selected_id)
                start_output(app, selected_id);
            continue;
        }

        // Selected output disappeared. Fall back to the first available real
        // output so BlackHole never leaves the user with silence.
        std::string fallback;
        {
            std::lock_guard<std::mutex> lock(app.outputs_mutex);
            if (!app.outputs.empty())
                fallback = app.outputs.front().name;
        }

        if (!fallback.empty()) {
            std::fprintf(stderr, "FxSound macOS: output disappeared; falling back to %s\n",
                         fallback.c_str());
            set_output_device(app, fallback);
        }
    }

    app.control_running.store(false, std::memory_order_relaxed);
    if (app.control_fd >= 0)
        ::shutdown(app.control_fd, SHUT_RDWR);
    if (app.control_thread.joinable())
        app.control_thread.join();

    if (app.changed_system_defaults) {
        if (app.original_default_output != kAudioObjectUnknown)
            set_default_output_device(app.original_default_output);
        if (app.original_system_output != kAudioObjectUnknown)
            set_system_output_device(app.original_system_output);
    }

    if (app.using_native_tap && app.native_tap_handle) {
        fxsoundNativeTapStop(app.native_tap_handle);
        app.native_tap_handle = nullptr;
        app.capture_proc = nullptr;
        app.capture_device = kAudioObjectUnknown;
    } else {
        stop_capture(app);
    }
    stop_output(app);

    {
        std::lock_guard<std::mutex> lock(app.dsp_mutex);
        autosave_current_preset_unlocked(app);
        save_audio_settings_unlocked(app);
    }

    std::fprintf(stderr,
        "FxSound macOS stopped: in=%llu out=%llu underruns=%llu overruns=%llu\n",
        (unsigned long long)app.input_frames.load(),
        (unsigned long long)app.output_frames.load(),
        (unsigned long long)app.ring.underruns.load(),
        (unsigned long long)app.ring.overruns.load());
    return 0;
}
