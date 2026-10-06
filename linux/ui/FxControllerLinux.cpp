/*
 * FxSound Linux UI controller.
 * AGPL-3.0-or-later, matching upstream FxSound.
 */
#include "FxControllerLinux.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace
{
std::string controlSocketPath()
{
    if (const char* runtime = std::getenv("XDG_RUNTIME_DIR"))
        return std::string(runtime) + "/fxsound-linux/control.sock";

    return "/run/user/" + std::to_string(static_cast<unsigned long>(::getuid()))
        + "/fxsound-linux/control.sock";
}

bool responseOk(const juce::String& response)
{
    const juce::var root = juce::JSON::parse(response);
    if (auto* object = root.getDynamicObject())
        return static_cast<bool>(object->getProperty("ok"));
    return false;
}
}

FxController::FxController()
{
    effects_.fill(0.0f);
    refresh();
}

juce::String FxController::sendCommand(const juce::String& command) const
{
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return {};

    sockaddr_un address{};
    address.sun_family = AF_UNIX;

    const std::string path = controlSocketPath();
    if (path.size() >= sizeof(address.sun_path))
    {
        ::close(fd);
        return {};
    }

    std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        ::close(fd);
        return {};
    }

    std::string payload = command.toStdString();
    payload.push_back('\n');
    if (::send(fd, payload.data(), payload.size(), MSG_NOSIGNAL) < 0)
    {
        ::close(fd);
        return {};
    }

    ::shutdown(fd, SHUT_WR);

    std::string response;
    char buffer[8192];
    for (;;)
    {
        const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0)
            break;
        response.append(buffer, static_cast<size_t>(n));
        if (response.find('\n') != std::string::npos)
            break;
    }

    ::close(fd);
    return juce::String(response).trim();
}

bool FxController::parseStatus(const juce::String& json)
{
    const juce::var root = juce::JSON::parse(json);
    auto* object = root.getDynamicObject();
    if (!object || !static_cast<bool>(object->getProperty("ok")))
        return false;

    current_preset_ = object->getProperty("preset").toString();
    power_on_ = static_cast<bool>(object->getProperty("power"));
    current_output_node_ = object->getProperty("output").toString();

    if (auto* effects = object->getProperty("effects").getDynamicObject())
    {
        effects_[FxEffects::Fidelity] =
            static_cast<float>(static_cast<double>(effects->getProperty("clarity"))) / 10.0f;
        effects_[FxEffects::Ambience] =
            static_cast<float>(static_cast<double>(effects->getProperty("ambience"))) / 10.0f;
        effects_[FxEffects::Surround] =
            static_cast<float>(static_cast<double>(effects->getProperty("surround"))) / 10.0f;
        effects_[FxEffects::DynamicBoost] =
            static_cast<float>(static_cast<double>(effects->getProperty("dynamic"))) / 10.0f;
        effects_[FxEffects::Bass] =
            static_cast<float>(static_cast<double>(effects->getProperty("bass"))) / 10.0f;
    }

    if (auto* audio = object->getProperty("audio").getDynamicObject())
    {
        volume_leveling_ = static_cast<float>(static_cast<double>(audio->getProperty("volume_leveling")));
        master_gain_ = static_cast<float>(static_cast<double>(audio->getProperty("master_gain")));
        balance_ = static_cast<float>(static_cast<double>(audio->getProperty("balance")));
        filter_q_ = static_cast<float>(static_cast<double>(audio->getProperty("filter_q")));
    }

    num_eq_bands_ = static_cast<int>(object->getProperty("bands"));
    eq_frequency_.clear();
    eq_gain_.clear();

    if (auto* eq = object->getProperty("eq").getArray())
    {
        eq_frequency_.reserve(eq->size());
        eq_gain_.reserve(eq->size());

        for (const auto& band : *eq)
        {
            if (auto* b = band.getDynamicObject())
            {
                eq_frequency_.push_back(static_cast<float>(static_cast<double>(b->getProperty("f"))));
                eq_gain_.push_back(static_cast<float>(static_cast<double>(b->getProperty("g"))));
            }
        }
    }

    connected_ = true;
    return true;
}

bool FxController::loadPresets()
{
    const juce::var root = juce::JSON::parse(sendCommand("PRESETS"));
    auto* object = root.getDynamicObject();
    if (!object || !static_cast<bool>(object->getProperty("ok")))
        return false;

    presets_.clear();
    if (auto* values = object->getProperty("presets").getArray())
        for (const auto& value : *values)
            presets_.add(value.toString());

    return !presets_.isEmpty();
}

bool FxController::loadOutputs()
{
    const juce::var root = juce::JSON::parse(sendCommand("OUTPUTS"));
    auto* object = root.getDynamicObject();
    if (!object || !static_cast<bool>(object->getProperty("ok")))
        return false;

    current_output_node_ = object->getProperty("selected").toString();
    outputs_.clear();

    if (auto* values = object->getProperty("outputs").getArray())
    {
        outputs_.reserve(static_cast<size_t>(values->size()));
        for (const auto& value : *values)
        {
            if (auto* output = value.getDynamicObject())
            {
                OutputDevice device;
                device.node_name = output->getProperty("name").toString();
                device.description = output->getProperty("description").toString();
                if (device.node_name.isNotEmpty())
                {
                    if (device.description.isEmpty())
                        device.description = device.node_name;
                    outputs_.push_back(std::move(device));
                }
            }
        }
    }

    return !outputs_.empty();
}

bool FxController::refresh()
{
    connected_ = parseStatus(sendCommand("STATUS"));
    if (connected_)
    {
        loadPresets();
        loadOutputs();
    }
    return connected_;
}

bool FxController::setPresetName(const juce::String& name)
{
    if (!responseOk(sendCommand("PRESET " + name)))
        return false;

    refresh();
    return true;
}

void FxController::setPowerState(bool on)
{
    if (responseOk(sendCommand(juce::String("POWER ") + (on ? "1" : "0"))))
        power_on_ = on;
}

juce::String FxController::getOutputDescription(int index) const
{
    if (index < 0 || static_cast<size_t>(index) >= outputs_.size())
        return {};
    return outputs_[static_cast<size_t>(index)].description;
}

juce::String FxController::getOutputNodeName(int index) const
{
    if (index < 0 || static_cast<size_t>(index) >= outputs_.size())
        return {};
    return outputs_[static_cast<size_t>(index)].node_name;
}

int FxController::getSelectedOutputIndex() const
{
    for (size_t i = 0; i < outputs_.size(); ++i)
        if (outputs_[i].node_name == current_output_node_)
            return static_cast<int>(i);
    return -1;
}

bool FxController::setOutputDevice(int index)
{
    if (index < 0 || static_cast<size_t>(index) >= outputs_.size())
        return false;

    const auto node = outputs_[static_cast<size_t>(index)].node_name;
    if (!responseOk(sendCommand("OUTPUT " + node)))
        return false;

    current_output_node_ = node;
    return true;
}

const char* FxController::effectCommandName(FxEffects::EffectType effect)
{
    switch (effect)
    {
        case FxEffects::Fidelity:     return "clarity";
        case FxEffects::Ambience:     return "ambience";
        case FxEffects::Surround:     return "surround";
        case FxEffects::DynamicBoost: return "dynamic";
        case FxEffects::Bass:         return "bass";
        default:                      return "clarity";
    }
}

float FxController::getEffectValue(FxEffects::EffectType effect) const
{
    const int index = static_cast<int>(effect);
    if (index < 0 || index >= FxEffects::NumEffects)
        return 0.0f;
    return effects_[static_cast<size_t>(index)];
}

void FxController::setEffectValue(FxEffects::EffectType effect, float value)
{
    const int index = static_cast<int>(effect);
    if (index < 0 || index >= FxEffects::NumEffects)
        return;

    const float clamped = juce::jlimit(0.0f, 10.0f, value);
    if (responseOk(sendCommand("EFFECT " + juce::String(effectCommandName(effect))
                               + " " + juce::String(clamped, 4))))
        effects_[static_cast<size_t>(index)] = clamped / 10.0f;
}

void FxController::setNumEqBands(int num_bands)
{
    if (responseOk(sendCommand("BANDS " + juce::String(num_bands))))
    {
        num_eq_bands_ = num_bands;
        refresh();
    }
}

void FxController::setVolumeLeveling(float value)
{
    value = std::round(value * 2.0f) / 2.0f;
    if (responseOk(sendCommand("AUDIO volume " + juce::String(value, 4))))
        volume_leveling_ = value;
}

void FxController::setBalance(float value)
{
    value = std::round(value);
    if (responseOk(sendCommand("AUDIO balance " + juce::String(value, 4))))
        balance_ = value;
}

void FxController::setMasterGain(float value)
{
    value = std::round(value);
    if (responseOk(sendCommand("AUDIO master " + juce::String(value, 4))))
        master_gain_ = value;
}

void FxController::setFilterQ(float value)
{
    value = std::round(value * 2.0f) / 2.0f;
    if (responseOk(sendCommand("AUDIO filterq " + juce::String(value, 4))))
        filter_q_ = value;
}

float FxController::getEqBandFrequency(int band_num) const
{
    if (band_num < 0 || static_cast<size_t>(band_num) >= eq_frequency_.size())
        return 0.0f;
    return eq_frequency_[static_cast<size_t>(band_num)];
}

void FxController::setEqBandFrequency(int band_num, float freq)
{
    if (band_num < 0 || static_cast<size_t>(band_num) >= eq_frequency_.size())
        return;

    if (responseOk(sendCommand("EQFREQ " + juce::String(band_num)
                               + " " + juce::String(freq, 4))))
        eq_frequency_[static_cast<size_t>(band_num)] = freq;
}

void FxController::getEqBandFrequencyRange(int band_num, float* min_freq, float* max_freq) const
{
    if (!min_freq || !max_freq || band_num < 0
        || static_cast<size_t>(band_num) >= eq_frequency_.size())
    {
        if (min_freq) *min_freq = 20.0f;
        if (max_freq) *max_freq = 20000.0f;
        return;
    }

    const size_t i = static_cast<size_t>(band_num);
    const float current = eq_frequency_[i];

    *min_freq = (i == 0)
        ? juce::jmax(10.0f, current * 0.7f)
        : std::sqrt(eq_frequency_[i - 1] * current);

    *max_freq = (i + 1 >= eq_frequency_.size())
        ? juce::jmin(24000.0f, current * 1.3f)
        : std::sqrt(current * eq_frequency_[i + 1]);
}

float FxController::getEqBandBoostCut(int band_num) const
{
    if (band_num < 0 || static_cast<size_t>(band_num) >= eq_gain_.size())
        return 0.0f;
    return eq_gain_[static_cast<size_t>(band_num)];
}

void FxController::setEqBandBoostCut(int band_num, float boost)
{
    if (band_num < 0 || static_cast<size_t>(band_num) >= eq_gain_.size())
        return;

    const float clamped = juce::jlimit(-12.0f, 12.0f, boost);
    if (responseOk(sendCommand("EQ " + juce::String(band_num)
                               + " " + juce::String(clamped, 4))))
        eq_gain_[static_cast<size_t>(band_num)] = clamped;
}

void FxController::getSpectrumBandValues(juce::Array<float>& values) const
{
    values.clearQuick();
    const juce::var root = juce::JSON::parse(sendCommand("SPECTRUM"));
    if (auto* object = root.getDynamicObject())
    {
        if (static_cast<bool>(object->getProperty("ok")))
        {
            if (auto* bands = object->getProperty("bands").getArray())
            {
                for (const auto& value : *bands)
                    values.add(juce::jlimit(0.0f, 1.0f,
                        static_cast<float>(static_cast<double>(value))));
            }
        }
    }

    while (values.size() < NUM_SPECTRUM_BANDS)
        values.add(0.0f);
}
