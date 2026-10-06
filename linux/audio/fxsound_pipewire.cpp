/*
 * FxSound Linux PipeWire bridge
 * Uses the upstream FxSound DfxDsp engine (AGPL-3.0-or-later).
 */
#include "DfxDsp.h"
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>
#include <spa/utils/result.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cctype>
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
constexpr size_t kRingFrames = 1u << 17; // ~2.7 s safety buffer
constexpr float kPeakCeiling = 0.977f;   // about -0.2 dBFS

struct Ring {
    std::vector<float> data = std::vector<float>(kRingFrames * kChannels, 0.0f);
    std::atomic<uint64_t> read_pos{0};
    std::atomic<uint64_t> write_pos{0};
    std::atomic<uint64_t> overruns{0};
    std::atomic<uint64_t> underruns{0};

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
        size_t take = std::min(frames, available);
        for (size_t i = 0; i < take; ++i) {
            size_t idx = static_cast<size_t>((r + i) % kRingFrames) * kChannels;
            dst[i * 2] = data[idx];
            dst[i * 2 + 1] = data[idx + 1];
        }
        if (take < frames) {
            std::fill(dst + take * 2, dst + frames * 2, 0.0f);
            underruns.fetch_add(1, std::memory_order_relaxed);
        }
        read_pos.store(r + take, std::memory_order_release);
        return take;
    }
};

struct OutputDevice {
    uint32_t id{PW_ID_ANY};
    std::string name;
    std::string description;
};

struct App {
    pw_main_loop* loop{};
    pw_context* context{};
    pw_core* core{};
    pw_registry* registry{};
    spa_hook registry_listener{};
    pw_stream* sink{};
    pw_stream* output{};
    DfxDsp dsp;
    std::mutex dsp_mutex;
    std::string current_preset{"General"};
    bool preset_modified{false};
    bool power_on{true};
    std::atomic<bool> control_running{true};
    int control_fd{-1};
    std::thread control_thread;
    Ring ring;
    // Preserve upstream FxSound output by default. The optional Linux-side
    // peak guard is troubleshooting-only and must be explicitly enabled.
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

std::filesystem::path state_dir() {
    if (const char* home = std::getenv("HOME"))
        return std::filesystem::path(home) / ".config/fxsound-linux";
    return std::filesystem::path(".");
}

std::filesystem::path preset_dir() {
    if (const char* home = std::getenv("HOME"))
        return std::filesystem::path(home) / ".local/share/fxsound-linux/presets";
    return std::filesystem::path(".");
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

std::filesystem::path preset_path_for_name(const std::string& name) {
    if (const auto* preset = find_factory_preset(name))
        return preset_dir() / preset->file;
    return {};
}

std::filesystem::path default_preset_path(App& app) {
    if (const char* requested = std::getenv("FXSOUND_PRESET_PATH")) {
        app.current_preset = "Custom";
        return std::filesystem::path(requested);
    }

    app.current_preset = load_saved_preset_name();
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

    app.dsp.eqOn(true);
    return true;
}

void configure_dsp(App& app) {
    if (app.dsp.setSignalFormat(32, 2, kRate, 32) != 0) {
        std::fprintf(stderr, "FxSound: setSignalFormat failed\n");
        return;
    }

    app.dsp.powerOn(true);

    const auto preset = default_preset_path(app);
    if (!apply_preset(app, preset)) {
        std::fprintf(stderr, "FxSound: preset load failed; leaving native DSP flat rather than using guessed tuning\n");
    }

    // Preserve FxSound's own defaults here. Extra Linux-side loudness shaping
    // would make the result diverge from the Windows preset.
    app.dsp.setVolumeLeveling(0.0f);
    app.dsp.setNormalization(0.0f);
    app.dsp.setFilterQ(1.0f);
    app.dsp.setMasterGain(0.0f);
    app.dsp.setBalance(0.0f);
}


std::string trim_copy(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::filesystem::path control_socket_path() {
    if (const char* runtime = std::getenv("XDG_RUNTIME_DIR"))
        return std::filesystem::path(runtime) / "fxsound-linux/control.sock";

    if (const char* home = std::getenv("HOME"))
        return std::filesystem::path(home) / ".cache/fxsound-linux/control.sock";

    return std::filesystem::path("/tmp/fxsound-linux-control.sock");
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

void registry_global(void* userdata, uint32_t id, uint32_t,
                     const char* type, uint32_t,
                     const spa_dict* props) {
    auto* app = static_cast<App*>(userdata);
    if (!app || !type || !props || std::strcmp(type, PW_TYPE_INTERFACE_Node) != 0)
        return;

    const char* media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
    const char* node_name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
    if (!media_class || !node_name || std::strcmp(media_class, "Audio/Sink") != 0)
        return;
    if (std::strcmp(node_name, "fxsound_sink") == 0)
        return;

    const char* description = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
    if (!description || !*description)
        description = spa_dict_lookup(props, PW_KEY_NODE_NICK);
    if (!description || !*description)
        description = node_name;

    std::lock_guard<std::mutex> lock(app->outputs_mutex);
    auto it = std::find_if(app->outputs.begin(), app->outputs.end(),
                           [id](const OutputDevice& d) { return d.id == id; });
    OutputDevice value{id, node_name, description};
    if (it == app->outputs.end())
        app->outputs.push_back(std::move(value));
    else
        *it = std::move(value);

    std::sort(app->outputs.begin(), app->outputs.end(),
              [](const OutputDevice& a, const OutputDevice& b) {
                  return a.description < b.description;
              });
}

void registry_global_remove(void* userdata, uint32_t id) {
    auto* app = static_cast<App*>(userdata);
    if (!app) return;

    std::lock_guard<std::mutex> lock(app->outputs_mutex);
    app->outputs.erase(
        std::remove_if(app->outputs.begin(), app->outputs.end(),
                       [id](const OutputDevice& d) { return d.id == id; }),
        app->outputs.end());
}

const pw_registry_events kRegistryEvents = {
    PW_VERSION_REGISTRY_EVENTS,
    .global = registry_global,
    .global_remove = registry_global_remove,
};

std::string outputs_json(App& app) {
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
        const auto path = preset_path_for_name(name);
        if (path.empty())
            return "{\"ok\":false,\"error\":\"unknown preset\"}";

        std::lock_guard<std::mutex> lock(app.dsp_mutex);
        if (!apply_preset(app, path))
            return "{\"ok\":false,\"error\":\"preset load failed\"}";

        app.current_preset = name;
        app.preset_modified = false;
        save_preset_name(name);
        return "{\"ok\":true}";
    }

    if (command == "RESET") {
        std::lock_guard<std::mutex> lock(app.dsp_mutex);
        const auto path = preset_path_for_name(app.current_preset == "Custom" ? "General" : app.current_preset);
        if (path.empty() || !apply_preset(app, path))
            return "{\"ok\":false,\"error\":\"reset failed\"}";
        app.preset_modified = false;
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
        app.preset_modified = true;
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

        app.preset_modified = true;
        return "{\"ok\":true}";
    }

    if (command.rfind("BANDS ", 0) == 0) {
        int bands = 0;
        if (!parse_int(trim_copy(command.substr(6)), bands) ||
            !(bands == 5 || bands == 10 || bands == 15 || bands == 20 || bands == 31))
            return "{\"ok\":false,\"error\":\"bands must be 5, 10, 15, 20, or 31\"}";

        std::lock_guard<std::mutex> lock(app.dsp_mutex);
        app.dsp.setNumBands(bands);
        app.preset_modified = true;
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
            app.dsp.setVolumeLeveling(value);
        } else if (control == "master") {
            if (value < -20.0f || value > 20.0f)
                return "{\"ok\":false,\"error\":\"master range is -20..20\"}";
            app.dsp.setMasterGain(value);
        } else if (control == "balance") {
            if (value < -20.0f || value > 20.0f)
                return "{\"ok\":false,\"error\":\"balance range is -20..20\"}";
            app.dsp.setBalance(value);
        } else if (control == "filterq") {
            if (value < 1.0f || value > 3.0f)
                return "{\"ok\":false,\"error\":\"filterq range is 1..3\"}";
            app.dsp.setFilterQ(value);
        } else {
            return "{\"ok\":false,\"error\":\"unknown audio control\"}";
        }
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

    const int server = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
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
        pollfd item{server, POLLIN, 0};
        const int ready = ::poll(&item, 1, 250);
        if (ready <= 0 || !(item.revents & POLLIN)) continue;

        const int client = ::accept4(server, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) continue;

        char buffer[8192] = {};
        const ssize_t received = ::recv(client, buffer, sizeof(buffer) - 1, 0);
        if (received > 0) {
            buffer[received] = '\0';
            std::string response = handle_control_command(*app, buffer);
            response.push_back('\n');
            ::send(client, response.data(), response.size(), MSG_NOSIGNAL);
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

void on_sink_state(void*, pw_stream_state old_state, pw_stream_state state, const char* error) {
    std::fprintf(stderr, "FxSound sink: %s -> %s%s%s\n",
        pw_stream_state_as_string(old_state), pw_stream_state_as_string(state),
        error ? " (" : "", error ? error : "");
    if (error) std::fprintf(stderr, ")\n");
}

void on_output_state(void*, pw_stream_state old_state, pw_stream_state state, const char* error) {
    std::fprintf(stderr, "FxSound output: %s -> %s%s%s\n",
        pw_stream_state_as_string(old_state), pw_stream_state_as_string(state),
        error ? " (" : "", error ? error : "");
    if (error) std::fprintf(stderr, ")\n");
}

void on_param_changed(void*, uint32_t id, const struct spa_pod* param) {
    if (id != SPA_PARAM_Format || !param) return;

    spa_audio_info_raw info{};
    if (spa_format_audio_raw_parse(param, &info) >= 0) {
        std::fprintf(stderr,
            "FxSound negotiated audio: format=%d rate=%u channels=%u\n",
            (int)info.format, info.rate, info.channels);
    }
}

void on_sink_process(void* userdata) {
    auto* app = static_cast<App*>(userdata);
    pw_buffer* pb = pw_stream_dequeue_buffer(app->sink);
    if (!pb) return;
    spa_buffer* sb = pb->buffer;
    if (!sb || sb->n_datas == 0) { pw_stream_queue_buffer(app->sink, pb); return; }
    spa_data& d = sb->datas[0];
    if (!d.data || !d.chunk) { pw_stream_queue_buffer(app->sink, pb); return; }

    auto* ptr = reinterpret_cast<float*>(static_cast<uint8_t*>(d.data) + d.chunk->offset);
    size_t frames = d.chunk->size / (sizeof(float) * kChannels);
    if (frames > 0) {
        float raw_peak = 0.0f;
        for (size_t i = 0; i < frames * kChannels; ++i)
            raw_peak = std::max(raw_peak, std::fabs(ptr[i]));
        update_peak_max(app->raw_peak_in, raw_peak);

        int rc = 0;
        {
            // Never block PipeWire's real-time callback while the UI is changing
            // a preset or control. If the lock is busy, pass this single block
            // through unchanged and resume FxSound processing on the next block.
            std::unique_lock<std::mutex> lock(app->dsp_mutex, std::try_to_lock);
            if (lock.owns_lock()) {
                rc = app->dsp.processAudio(reinterpret_cast<short int*>(ptr),
                                           reinterpret_cast<short int*>(ptr),
                                           static_cast<int>(frames), 0);
            }
        }
        if (rc == 0) {
            apply_peak_guard(*app, ptr, frames);
            app->ring.push(ptr, frames);
            app->input_frames.fetch_add(frames, std::memory_order_relaxed);
        }
    }
    pw_stream_queue_buffer(app->sink, pb);
}

void on_output_process(void* userdata) {
    auto* app = static_cast<App*>(userdata);
    pw_buffer* pb = pw_stream_dequeue_buffer(app->output);
    if (!pb) return;
    spa_buffer* sb = pb->buffer;
    if (!sb || sb->n_datas == 0) { pw_stream_queue_buffer(app->output, pb); return; }
    spa_data& d = sb->datas[0];
    if (!d.data || !d.chunk) { pw_stream_queue_buffer(app->output, pb); return; }

    size_t max_frames = d.maxsize / (sizeof(float) * kChannels);
    size_t frames = pb->requested ? std::min<size_t>(pb->requested, max_frames) : max_frames;
    auto* ptr = reinterpret_cast<float*>(d.data);
    app->ring.pop(ptr, frames);
    d.chunk->offset = 0;
    d.chunk->stride = sizeof(float) * kChannels;
    d.chunk->size = static_cast<uint32_t>(frames * sizeof(float) * kChannels);
    app->output_frames.fetch_add(frames, std::memory_order_relaxed);
    pw_stream_queue_buffer(app->output, pb);
}

const pw_stream_events kSinkEvents = {
    PW_VERSION_STREAM_EVENTS,
    .state_changed = on_sink_state,
    .param_changed = on_param_changed,
    .process = on_sink_process,
};

const pw_stream_events kOutputEvents = {
    PW_VERSION_STREAM_EVENTS,
    .state_changed = on_output_state,
    .param_changed = on_param_changed,
    .process = on_output_process,
};

int create_output_stream(App& app) {
    const char* output_name = std::getenv("FXSOUND_OUTPUT_NAME");
    if (!output_name || !*output_name) output_name = "fxsound_processed_output";

    if (app.output) {
        pw_stream_destroy(app.output);
        app.output = nullptr;
    }

    const std::string selected_output = selected_output_name_copy(app);

    pw_properties* out_props = nullptr;
    if (!selected_output.empty()) {
        out_props = pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Playback",
            PW_KEY_MEDIA_ROLE, "DSP",
            PW_KEY_NODE_NAME, output_name,
            PW_KEY_NODE_DESCRIPTION, "FxSound Processed Output",
            PW_KEY_TARGET_OBJECT, selected_output.c_str(),
            PW_KEY_NODE_AUTOCONNECT, "true",
            PW_KEY_NODE_LATENCY, "256/48000",
            "audio.channels", "2",
            "audio.position", "[ FL FR ]",
            nullptr);
    } else {
        return -EINVAL;
    }

    app.output = pw_stream_new_simple(
        pw_main_loop_get_loop(app.loop),
        "FxSound Processed Output",
        out_props,
        &kOutputEvents,
        &app);
    if (!app.output) return -ENOMEM;

    uint8_t buffer[1024];
    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.rate = kRate;
    info.channels = kChannels;
    info.position[0] = SPA_AUDIO_CHANNEL_FL;
    info.position[1] = SPA_AUDIO_CHANNEL_FR;

    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const spa_pod* params[1] = {
        spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info)
    };

    const int rc = pw_stream_connect(
        app.output,
        PW_DIRECTION_OUTPUT,
        PW_ID_ANY,
        static_cast<pw_stream_flags>(
            PW_STREAM_FLAG_MAP_BUFFERS |
            PW_STREAM_FLAG_RT_PROCESS),
        params,
        1);

    if (rc < 0) {
        pw_stream_destroy(app.output);
        app.output = nullptr;
    }
    return rc;
}

int switch_output_on_loop(struct spa_loop*, bool, uint32_t,
                          const void* data, size_t size, void* userdata) {
    auto* app = static_cast<App*>(userdata);
    if (!app || !data || size == 0) return -EINVAL;

    const char* value = static_cast<const char*>(data);
    const size_t len = strnlen(value, size);
    if (len == 0 || len >= size) return -EINVAL;

    std::string previous;
    {
        std::lock_guard<std::mutex> lock(app->outputs_mutex);
        previous = app->selected_output_name;
        app->selected_output_name.assign(value, len);
    }

    const int rc = create_output_stream(*app);
    if (rc < 0) {
        {
            std::lock_guard<std::mutex> lock(app->outputs_mutex);
            app->selected_output_name = previous;
        }
        create_output_stream(*app);
        return rc;
    }
    return 0;
}

bool set_output_device(App& app, const std::string& node_name) {
    if (!app.loop || node_name.empty()) return false;

    const int rc = pw_loop_invoke(
        pw_main_loop_get_loop(app.loop),
        switch_output_on_loop,
        0,
        node_name.c_str(),
        node_name.size() + 1,
        true,
        &app);

    if (rc < 0) return false;
    save_output_name(node_name);
    return true;
}

int connect_streams(App& app) {
    const char* sink_name = std::getenv("FXSOUND_SINK_NAME");
    if (!sink_name || !*sink_name) sink_name = "fxsound_sink";

    const char* sink_description = std::getenv("FXSOUND_SINK_DESCRIPTION");
    if (!sink_description || !*sink_description) sink_description = "FxSound";

    auto* sink_props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Playback",
        PW_KEY_MEDIA_ROLE, "DSP",
        PW_KEY_MEDIA_CLASS, "Audio/Sink",
        PW_KEY_NODE_NAME, sink_name,
        PW_KEY_NODE_DESCRIPTION, sink_description,
        PW_KEY_NODE_NICK, sink_description,
        PW_KEY_NODE_VIRTUAL, "true",
        PW_KEY_NODE_AUTOCONNECT, "false",
        PW_KEY_NODE_LATENCY, "256/48000",
        "audio.channels", "2",
        "audio.position", "[ FL FR ]",
        nullptr);

    app.sink = pw_stream_new_simple(
        pw_main_loop_get_loop(app.loop),
        "FxSound",
        sink_props,
        &kSinkEvents,
        &app);
    if (!app.sink) return -ENOMEM;

    uint8_t buffer[1024];
    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.rate = kRate;
    info.channels = kChannels;
    info.position[0] = SPA_AUDIO_CHANNEL_FL;
    info.position[1] = SPA_AUDIO_CHANNEL_FR;

    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const spa_pod* params[1] = {
        spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info)
    };

    int rc = pw_stream_connect(
        app.sink,
        PW_DIRECTION_INPUT,
        PW_ID_ANY,
        static_cast<pw_stream_flags>(
            PW_STREAM_FLAG_MAP_BUFFERS |
            PW_STREAM_FLAG_RT_PROCESS),
        params,
        1);
    if (rc < 0) return rc;

    return create_output_stream(app);
}

void handle_signal(int) {
    if (g_app && g_app->loop) pw_main_loop_quit(g_app->loop);
}

} // namespace

int main(int argc, char** argv) {
    pw_init(&argc, &argv);
    App app;
    g_app = &app;
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    if (const char* guard = std::getenv("FXSOUND_PEAK_GUARD")) {
        const std::string value = trim_copy(guard);
        app.peak_guard_enabled =
            value == "1" || value == "true" || value == "TRUE" ||
            value == "yes" || value == "YES" || value == "on" || value == "ON";
    }

    configure_dsp(app);

    if (const char* target_object = std::getenv("FXSOUND_TARGET_OBJECT");
        target_object && *target_object) {
        app.selected_output_name = target_object;
    } else {
        app.selected_output_name = load_saved_output_name();
    }
    if (app.selected_output_name.empty()) {
        std::fprintf(stderr,
            "FxSound: no physical output selected. Set FXSOUND_TARGET_OBJECT "
            "or install with linux/install-user.sh so the current physical sink is saved.\n");
        pw_deinit();
        return 4;
    }

    app.loop = pw_main_loop_new(nullptr);
    if (!app.loop) return 2;

    app.context = pw_context_new(pw_main_loop_get_loop(app.loop), nullptr, 0);
    if (!app.context) {
        pw_main_loop_destroy(app.loop);
        pw_deinit();
        return 2;
    }

    app.core = pw_context_connect(app.context, nullptr, 0);
    if (!app.core) {
        pw_context_destroy(app.context);
        pw_main_loop_destroy(app.loop);
        pw_deinit();
        return 2;
    }

    app.registry = pw_core_get_registry(app.core, PW_VERSION_REGISTRY, 0);
    if (!app.registry) {
        pw_core_disconnect(app.core);
        pw_context_destroy(app.context);
        pw_main_loop_destroy(app.loop);
        pw_deinit();
        return 2;
    }
    pw_registry_add_listener(app.registry, &app.registry_listener, &kRegistryEvents, &app);

    int rc = connect_streams(app);
    if (rc < 0) {
        std::fprintf(stderr, "FxSound PipeWire connect failed: %s\n", spa_strerror(rc));
        if (app.output) pw_stream_destroy(app.output);
        if (app.sink) pw_stream_destroy(app.sink);
        spa_hook_remove(&app.registry_listener);
        if (app.registry) pw_proxy_destroy(reinterpret_cast<pw_proxy*>(app.registry));
        if (app.core) pw_core_disconnect(app.core);
        if (app.context) pw_context_destroy(app.context);
        pw_main_loop_destroy(app.loop);
        pw_deinit();
        return 3;
    }

    std::fprintf(stderr,
        "FxSound real DSP ready: 48kHz stereo -> output=%s; preset=%s; "
        "Clarity=%.4f Ambience=%.4f Surround=%.4f Dynamic=%.4f Bass=%.4f EQ=%d-band\n",
        app.selected_output_name.c_str(),
        default_preset_path(app).c_str(),
        app.dsp.getEffectValue(DfxDsp::Fidelity) * 10.0f,
        app.dsp.getEffectValue(DfxDsp::Ambience) * 10.0f,
        app.dsp.getEffectValue(DfxDsp::Surround) * 10.0f,
        app.dsp.getEffectValue(DfxDsp::DynamicBoost) * 10.0f,
        app.dsp.getEffectValue(DfxDsp::Bass) * 10.0f,
        app.dsp.getNumEqBands());

    app.control_thread = std::thread(control_server, &app);
    pw_main_loop_run(app.loop);

    app.control_running.store(false, std::memory_order_relaxed);
    if (app.control_thread.joinable())
        app.control_thread.join();

    std::fprintf(stderr,
        "FxSound stopped: in=%llu out=%llu underruns=%llu overruns=%llu "
        "raw-peak=%.6f peak-in=%.6f peak-out=%.6f\n",
        (unsigned long long)app.input_frames.load(),
        (unsigned long long)app.output_frames.load(),
        (unsigned long long)app.ring.underruns.load(),
        (unsigned long long)app.ring.overruns.load(),
        app.raw_peak_in.load(), app.peak_in.load(), app.peak_out.load());

    if (app.output) pw_stream_destroy(app.output);
    if (app.sink) pw_stream_destroy(app.sink);
    spa_hook_remove(&app.registry_listener);
    if (app.registry) pw_proxy_destroy(reinterpret_cast<pw_proxy*>(app.registry));
    if (app.core) pw_core_disconnect(app.core);
    if (app.context) pw_context_destroy(app.context);
    pw_main_loop_destroy(app.loop);
    pw_deinit();
    return 0;
}
