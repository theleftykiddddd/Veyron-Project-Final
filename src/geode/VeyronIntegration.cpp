// Veyron Proyect - Geode integration for GD 2.2081 / Geode 5.11.2.
// The platform-neutral recorder and scheduler remain in src/core; this file
// connects them to the game's simulation hooks and Cocos UI.

#include <Geode/Geode.hpp>
#include <Geode/cocos/layers_scenes_transitions_nodes/CCLayer.h>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>

#include "veyron/FormatIO.hpp"
#include "veyron/Recorder.hpp"
#include "veyron/Replay.hpp"
#include "veyron/WindowAnalyzer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;

namespace veyron::geode_integration {

namespace {

constexpr double kTargetTickRate = 240.0;
constexpr std::uint32_t kAudioSampleRate = 22'050;
constexpr double kAudioDurationSeconds = 0.085;

struct HeldInput {
    int button = 1;
    bool player2 = false;

    bool operator<(HeldInput const& other) const {
        if (button != other.button) return button < other.button;
        return player2 < other.player2;
    }
};

veyron::Recorder g_recorder;
veyron::ReplayScheduler g_replay;
std::optional<veyron::Macro> g_currentMacro;
PlayLayer* g_activePlayLayer = nullptr;
CCLayerColor* g_hudPanel = nullptr;
CCLayerColor* g_hudAccent = nullptr;
CCLabelBMFont* g_hudLabel = nullptr;

std::uint64_t g_gameFrame = 0;
std::uint64_t g_replayStartFrame = 0;
std::uint64_t g_lastRealPressFrame = 0;
std::uint64_t g_lastReplayPressFrame = 0;
bool g_hasRealPressFrame = false;
bool g_hasReplayPressFrame = false;
bool g_injectedCall = false;
bool g_runtimeHudVisible = true;
bool g_replayKeysTemporarilyReleased = false;
bool g_replayFinishedThisTick = false;
float g_hudRefreshTimer = 0.f;
std::deque<std::uint64_t> g_recentPressFrames;
std::set<HeldInput> g_replayHeldInputs;
std::unordered_map<std::string, std::filesystem::path> g_toneFileCache;
std::string g_lastStatus = "Ready";

bool settingBool(char const* key, bool fallback) {
    auto mod = Mod::get();
    if (!mod) return fallback;
    return mod->getSettingValue<bool>(key);
}

double settingDouble(char const* key, double fallback) {
    auto mod = Mod::get();
    if (!mod) return fallback;
    return mod->getSettingValue<double>(key);
}

std::string settingString(char const* key, std::string fallback) {
    auto mod = Mod::get();
    if (!mod) return fallback;
    auto value = mod->getSettingValue<std::string>(key);
    return value.empty() ? std::move(fallback) : value;
}

std::filesystem::path macroPath() {
    auto mod = Mod::get();
    if (!mod) return {};

    auto path = mod->getSettingValue<std::filesystem::path>("macro-file");
    if (path.empty()) path = mod->getSaveDir() / "latest.vpr";
    return path;
}

std::string stateName() {
    if (g_recorder.state() == veyron::RecorderState::Recording) return "REC";
    switch (g_replay.state()) {
        case veyron::ReplayState::Playing: return "PLAY";
        case veyron::ReplayState::Paused: return "PAUSED";
        case veyron::ReplayState::Finished: return "DONE";
        case veyron::ReplayState::Idle: break;
    }
    return g_currentMacro ? "READY" : "IDLE";
}

std::uint64_t recordedInputCount() {
    if (g_recorder.state() == veyron::RecorderState::Recording) {
        return static_cast<std::uint64_t>(g_recorder.current().inputs.size());
    }
    return g_currentMacro
        ? static_cast<std::uint64_t>(g_currentMacro->inputs.size())
        : 0;
}

void trimRecentPresses() {
    while (!g_recentPressFrames.empty() &&
           g_gameFrame >= g_recentPressFrames.front() &&
           g_gameFrame - g_recentPressFrames.front() >=
               static_cast<std::uint64_t>(kTargetTickRate)) {
        g_recentPressFrames.pop_front();
    }
}

std::size_t recentCpsCount() {
    trimRecentPresses();
    return g_recentPressFrames.size();
}

// Writes an actual sine-wave WAV instead of relying on Cocos' pitch parameter.
// The legacy Windows audio backend does not consistently support pitch changes.
std::filesystem::path makeToneFile(double frequencyHz, double volume) {
    auto mod = Mod::get();
    if (!mod || !std::isfinite(frequencyHz) || !std::isfinite(volume)) return {};

    const auto safeVolume = std::clamp(volume, 0.0, 1.0);
    if (safeVolume <= 0.0 || frequencyHz < 20.0 || frequencyHz > 20'000.0) return {};

    const auto roundedFrequency = static_cast<int>(std::lround(frequencyHz));
    const auto volumePercent = static_cast<int>(std::lround(safeVolume * 100.0));
    const auto fileName = "veyron_" + std::to_string(roundedFrequency) +
                          "hz_" + std::to_string(volumePercent) + "v.wav";

    if (auto it = g_toneFileCache.find(fileName); it != g_toneFileCache.end()) {
        return it->second;
    }

    std::error_code ec;
    auto toneDir = mod->getSaveDir() / "tones";
    std::filesystem::create_directories(toneDir, ec);
    if (ec) return {};

    auto path = toneDir / fileName;
    if (std::filesystem::exists(path, ec) && !ec) {
        g_toneFileCache.emplace(fileName, path);
        return path;
    }
    ec.clear();

    constexpr std::uint16_t channels = 1;
    constexpr std::uint16_t bitsPerSample = 16;
    constexpr std::uint32_t bytesPerSample = bitsPerSample / 8;
    const auto sampleCount = static_cast<std::uint32_t>(
        static_cast<double>(kAudioSampleRate) * kAudioDurationSeconds);
    const auto dataBytes = sampleCount * bytesPerSample * channels;

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return {};

    auto writeU16 = [&file](std::uint16_t value) {
        const std::array<char, 2> bytes = {
            static_cast<char>(value & 0xff),
            static_cast<char>((value >> 8) & 0xff)
        };
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    };
    auto writeU32 = [&file](std::uint32_t value) {
        const std::array<char, 4> bytes = {
            static_cast<char>(value & 0xff),
            static_cast<char>((value >> 8) & 0xff),
            static_cast<char>((value >> 16) & 0xff),
            static_cast<char>((value >> 24) & 0xff)
        };
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    };

    file.write("RIFF", 4);
    writeU32(36 + dataBytes);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    writeU32(16);
    writeU16(1); // PCM
    writeU16(channels);
    writeU32(kAudioSampleRate);
    writeU32(kAudioSampleRate * bytesPerSample * channels);
    writeU16(bytesPerSample * channels);
    writeU16(bitsPerSample);
    file.write("data", 4);
    writeU32(dataBytes);

    constexpr double kTwoPi = 6.28318530717958647692;
    const double amplitude = 0.34 * safeVolume * 32767.0;
    const double fadeInSamples = kAudioSampleRate * 0.004;
    const double fadeOutSamples = kAudioSampleRate * 0.012;

    for (std::uint32_t i = 0; i < sampleCount; ++i) {
        const double fadeIn = std::clamp(static_cast<double>(i) / fadeInSamples, 0.0, 1.0);
        const double remaining = static_cast<double>(sampleCount - i);
        const double fadeOut = std::clamp(remaining / fadeOutSamples, 0.0, 1.0);
        const double envelope = std::min(fadeIn, fadeOut);
        const double sample = std::sin(kTwoPi * frequencyHz * i / kAudioSampleRate) *
                              amplitude * envelope;
        const auto pcm = static_cast<std::int16_t>(std::lround(sample));
        writeU16(static_cast<std::uint16_t>(pcm));
    }

    if (!file) {
        file.close();
        std::filesystem::remove(path, ec);
        return {};
    }
    g_toneFileCache.emplace(fileName, path);
    return path;
}

void playToneForInterval(std::uint64_t frame, bool replaySource) {
    if (!settingBool("audio-enabled", true)) return;

    auto& previousFrame = replaySource ? g_lastReplayPressFrame : g_lastRealPressFrame;
    auto& hasPrevious = replaySource ? g_hasReplayPressFrame : g_hasRealPressFrame;
    int intervalFrames = 1;
    if (hasPrevious && frame >= previousFrame) {
        intervalFrames = static_cast<int>(std::clamp<std::uint64_t>(
            frame - previousFrame, 1, 24));
    }
    previousFrame = frame;
    hasPrevious = true;

    double minHz = std::clamp(settingDouble("audio-min-hz", 95.0), 50.0, 500.0);
    double maxHz = std::clamp(settingDouble("audio-max-hz", 880.0), 200.0, 2000.0);
    if (maxHz <= minHz) maxHz = std::min(2000.0, minHz + 1.0);

    const double frequency = veyron::WindowAnalyzer::toneFrequencyHz(
        intervalFrames, 1, 24, minHz, maxHz);
    const auto volume = std::clamp(settingDouble("master-volume", 0.55), 0.0, 1.0);
    const auto file = makeToneFile(frequency, volume);
    if (file.empty()) return;

    // Use Geometry Dash's FMOD backend. SimpleAudioEngine is not shipped
    // by the Geode SDK used for this target.
    const auto fileName = geode::utils::string::pathToString(file);
    if (auto* audio = FMODAudioEngine::get()) {
        audio->playEffect(fileName, 1.f, 0.f, 1.f);
    }
}

void rawInject(PlayLayer* layer, bool down, int button, bool player2) {
    if (!layer) return;
    const bool previousGuard = g_injectedCall;
    g_injectedCall = true;
    layer->handleButton(down, button, !player2);
    g_injectedCall = previousGuard;
}

void releasePhysicalReplayInputs(PlayLayer* layer, bool clearLogicalState) {
    if (!layer) {
        if (clearLogicalState) g_replayHeldInputs.clear();
        g_replayKeysTemporarilyReleased = false;
        return;
    }

    // If playback was paused, the physical releases have already happened.
    // Avoid emitting duplicate release events when STOP/reset/quit follows.
    if (!g_replayKeysTemporarilyReleased) {
        for (auto const& held : g_replayHeldInputs) {
            rawInject(layer, false, held.button, held.player2);
        }
    }

    g_replayKeysTemporarilyReleased =
        !clearLogicalState && !g_replayHeldInputs.empty();
    if (clearLogicalState) {
        g_replayHeldInputs.clear();
        g_replayKeysTemporarilyReleased = false;
    }
}

void restorePhysicalReplayInputs(PlayLayer* layer) {
    if (!layer || !g_replayKeysTemporarilyReleased) return;
    for (auto const& held : g_replayHeldInputs) {
        rawInject(layer, true, held.button, held.player2);
    }
    g_replayKeysTemporarilyReleased = false;
}

void stopReplay(PlayLayer* layer) {
    releasePhysicalReplayInputs(layer, true);
    g_replay.stop();
    g_hasReplayPressFrame = false;
    g_lastReplayPressFrame = 0;
    g_replayFinishedThisTick = false;
}

void injectReplayEvent(PlayLayer* layer, veyron::InputEvent const& event) {
    if (!layer) return;
    const HeldInput key{event.button, event.player2};

    if (event.down) g_replayHeldInputs.insert(key);
    else g_replayHeldInputs.erase(key);

    rawInject(layer, event.down, event.button, event.player2);
    if (event.down) playToneForInterval(event.frame, true);
}

void processReplayTick(PlayLayer* layer) {
    if (!layer || g_replay.state() != veyron::ReplayState::Playing) return;
    if (g_gameFrame < g_replayStartFrame) {
        g_lastStatus = "Replay clock reset; stopped safely";
        stopReplay(layer);
        return;
    }

    const auto relativeFrame = g_gameFrame - g_replayStartFrame;
    auto due = g_replay.drainDueEvents(relativeFrame);
    for (auto const& event : due) injectReplayEvent(layer, event);

    // Defer the final safety release until after the vanilla simulation pass.
    // This gives a final press-only event one simulation tick before cleanup.
    if (g_replay.state() == veyron::ReplayState::Finished) {
        g_replayFinishedThisTick = true;
    }
}

bool saveCurrentMacro(bool report = true) {
    if (!g_currentMacro) {
        g_lastStatus = "No macro to save";
        if (report) FLAlertLayer::create("Veyron", "There is no recorded or loaded macro to save.", "OK")->show();
        return false;
    }

    auto path = macroPath();
    if (path.empty()) {
        g_lastStatus = "Invalid macro path";
        return false;
    }

    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            g_lastStatus = "Could not create macro directory: " + ec.message();
            if (report) {
                FLAlertLayer::create("Veyron", g_lastStatus.c_str(), "OK")->show();
            }
            return false;
        }
    }
    const auto result = veyron::exportVpr(
        *g_currentMacro, geode::utils::string::pathToString(path));
    g_lastStatus = result.ok
        ? ("Saved: " + geode::utils::string::pathToString(path.filename()))
        : result.message;
    if (report) {
        FLAlertLayer::create("Veyron", result.message.c_str(), "OK")->show();
    }
    return result.ok;
}

bool loadCurrentMacro(bool report = true) {
    if (g_recorder.state() == veyron::RecorderState::Recording) {
        g_lastStatus = "Stop recording before loading another macro";
        if (report) {
            FLAlertLayer::create("Veyron", g_lastStatus.c_str(), "OK")->show();
        }
        return false;
    }

    auto path = macroPath();
    if (path.empty()) {
        g_lastStatus = "Invalid macro path";
        return false;
    }

    veyron::Macro loaded;
    const auto result = veyron::importVpr(
        geode::utils::string::pathToString(path), loaded);
    if (!result.ok) {
        g_lastStatus = result.message;
        if (report) FLAlertLayer::create("Veyron", result.message.c_str(), "OK")->show();
        return false;
    }

    // Replacing a macro while replay holds a key must release it first.
    if (g_activePlayLayer) stopReplay(g_activePlayLayer);
    else stopReplay(nullptr);
    if (!g_replay.load(loaded)) {
        g_lastStatus = "Macro could not be scheduled";
        return false;
    }
    g_currentMacro = std::move(loaded);
    g_lastStatus = "Loaded: " + geode::utils::string::pathToString(path.filename());
    if (report) FLAlertLayer::create("Veyron", "VPR macro loaded successfully.", "OK")->show();
    return true;
}

void finishRecording(bool autoSave) {
    auto stopped = g_recorder.stop();
    if (!stopped) return;

    g_currentMacro = std::move(*stopped);
    if (g_currentMacro->inputs.empty()) {
        g_lastStatus = "Recording contained no inputs";
        return;
    }

    g_lastStatus = "Recording stopped (" +
        std::to_string(g_currentMacro->inputs.size()) + " events)";
    if (autoSave && settingBool("auto-save", true)) saveCurrentMacro(false);
}

void beginRecording() {
    if (!g_activePlayLayer) {
        g_lastStatus = "Open a level before recording";
        return;
    }

    stopReplay(g_activePlayLayer);
    veyron::MacroMetadata metadata;
    metadata.title = "Veyron Macro";
    metadata.author = "Veyron Proyect";
    metadata.gameVersion = 2.2081;
    metadata.tickRate = kTargetTickRate;
    metadata.sourceFormat = "veyron-vpr";

    if (g_recorder.begin(std::move(metadata), g_gameFrame)) {
        g_currentMacro.reset();
        g_recentPressFrames.clear();
        g_hasRealPressFrame = false;
        g_lastStatus = "Recording started";
    } else {
        g_lastStatus = "Recorder is already active";
    }
}

void startOrTogglePlayback() {
    auto* layer = g_activePlayLayer;
    if (!layer) {
        g_lastStatus = "Open a level before playback";
        return;
    }

    if (g_recorder.state() == veyron::RecorderState::Recording) {
        g_lastStatus = "Stop recording before playback";
        return;
    }

    if (g_replay.state() == veyron::ReplayState::Playing) {
        g_replay.pause();
        releasePhysicalReplayInputs(layer, false);
        g_lastStatus = "Playback paused";
        return;
    }

    if (g_replay.state() == veyron::ReplayState::Paused) {
        restorePhysicalReplayInputs(layer);
        g_replay.resume();
        g_lastStatus = "Playback resumed";
        return;
    }

    if (!g_currentMacro || !g_replay.load(*g_currentMacro)) {
        g_lastStatus = "Load or record a valid VPR macro first";
        return;
    }

    if (settingBool("safe-mode", true) && !layer->m_isPracticeMode) {
        g_lastStatus = "Safe Mode: playback requires Practice Mode";
        FLAlertLayer::create(
            "Veyron Safe Mode",
            "Macro playback is blocked outside Practice Mode. Enable Practice Mode or turn off Safe Mode in the mod settings.",
            "OK")->show();
        return;
    }

    g_replayHeldInputs.clear();
    g_replayKeysTemporarilyReleased = false;
    g_hasReplayPressFrame = false;
    g_replayStartFrame = g_gameFrame;
    g_replay.start();
    g_lastStatus = "Playback started";
}

ccColor3B hudAccentColor() {
    const auto state = stateName();
    if (state == "REC") return {65, 222, 145};
    if (state == "PLAY") return {55, 205, 232};
    if (state == "PAUSED") return {245, 185, 82};
    if (state == "DONE") return {155, 171, 190};
    return {91, 150, 205};
}

std::string buildHudText() {
    std::string text = "VEYRON  /  " + stateName();

    if (settingBool("hud-show-safe-mode", true) &&
        settingBool("safe-mode", true)) {
        text += "  /  SAFE";
    }

    if (settingBool("hud-show-frame", true)) {
        text += "\nFRAME    " + std::to_string(g_gameFrame);
    }
    if (settingBool("hud-show-inputs", true)) {
        text += "\nINPUTS   " + std::to_string(recordedInputCount());
    }
    if (settingBool("hud-show-cps", true)) {
        text += "\nCPS ~    " + std::to_string(recentCpsCount());
    }
    // Do not imply that the project measures hitbox windows: the backend
    // required for that feature has not been validated/implemented yet.
    text += "\nWINDOW   UNMEASURED";

    if (settingBool("hud-show-last-action", true) && !g_lastStatus.empty()) {
        auto lastAction = g_lastStatus;
        constexpr std::size_t maxStatusChars = 32;
        if (lastAction.size() > maxStatusChars) {
            lastAction.resize(maxStatusChars - 3);
            lastAction += "...";
        }
        text += "\n" + lastAction;
    }

    return text;
}

void clearHud() {
    if (g_hudPanel) {
        g_hudPanel->removeFromParentAndCleanup(true);
    }
    g_hudPanel = nullptr;
    g_hudAccent = nullptr;
    g_hudLabel = nullptr;
}

void updateHudText() {
    if (!g_hudLabel || !g_activePlayLayer) return;

    const auto text = buildHudText();
    g_hudLabel->setString(text.c_str());

    if (g_hudAccent) {
        g_hudAccent->setColor(hudAccentColor());
    }
}

void attachHud(PlayLayer* layer, bool readPersistentSetting = true) {
    if (!layer) return;
    clearHud();

    if (readPersistentSetting) {
        g_runtimeHudVisible = settingBool("hud-enabled", true);
    }
    if (!g_runtimeHudVisible) return;

    constexpr float panelWidth = 244.f;
    constexpr float panelHeight = 120.f;

    // A translucent panel makes the HUD readable over bright level decoration.
    g_hudPanel = CCLayerColor::create(
        ccColor4B{12, 18, 29, 226}, panelWidth, panelHeight);
    if (!g_hudPanel) return;

    const float hudScale = static_cast<float>(
        std::clamp(settingDouble("hud-scale", 0.80), 0.5, 1.5));
    g_hudPanel->setScale(hudScale);
    const auto opacity = std::clamp(settingDouble("hud-opacity", 0.88), 0.25, 1.0);
    g_hudPanel->setOpacity(static_cast<GLubyte>(std::lround(opacity * 255.0)));
    g_hudPanel->setZOrder(10'000);
    layer->addChild(g_hudPanel, 10'000);

    g_hudAccent = CCLayerColor::create(
        ccColor4B{55, 205, 232, 255}, panelWidth, 3.f);
    if (g_hudAccent) {
        g_hudAccent->setAnchorPoint({0.f, 0.f});
        g_hudAccent->setPosition({0.f, panelHeight - 3.f});
        g_hudPanel->addChild(g_hudAccent, 2);
    }

    g_hudLabel = CCLabelBMFont::create("", "chatFont.fnt");
    if (!g_hudLabel) {
        clearHud();
        return;
    }
    g_hudLabel->setAnchorPoint({0.f, 1.f});
    g_hudLabel->setPosition({11.f, panelHeight - 10.f});
    g_hudLabel->setScale(0.43f);
    g_hudLabel->setColor({226, 236, 247});
    g_hudPanel->addChild(g_hudLabel, 3);

    const auto win = CCDirector::sharedDirector()->getWinSize();
    const auto position = settingString("hud-position", "top-right");
    if (position == "top-left") {
        g_hudPanel->setAnchorPoint({0.f, 1.f});
        g_hudPanel->setPosition({14.f, win.height - 14.f});
    } else if (position == "bottom-left") {
        g_hudPanel->setAnchorPoint({0.f, 0.f});
        g_hudPanel->setPosition({14.f, 14.f});
    } else if (position == "bottom-right") {
        g_hudPanel->setAnchorPoint({1.f, 0.f});
        g_hudPanel->setPosition({win.width - 14.f, 14.f});
    } else {
        g_hudPanel->setAnchorPoint({1.f, 1.f});
        g_hudPanel->setPosition({win.width - 14.f, win.height - 14.f});
    }

    updateHudText();
}

void toggleHud() {
    g_runtimeHudVisible = !g_runtimeHudVisible;

    // Keep the pause-popup toggle synchronized with Geode's persistent setting.
    if (auto mod = Mod::get()) {
        mod->setSettingValue<bool>("hud-enabled", g_runtimeHudVisible);
    }

    if (!g_activePlayLayer) return;
    if (!g_runtimeHudVisible) {
        clearHud();
    } else {
        attachHud(g_activePlayLayer, false);
        g_runtimeHudVisible = true;
    }
}

void toggleMute() {
    const bool currentlyEnabled = settingBool("audio-enabled", true);
    const bool nextEnabled = !currentlyEnabled;

    if (auto mod = Mod::get()) {
        mod->setSettingValue<bool>("audio-enabled", nextEnabled);
    }

    // Do not carry an old interval across a mute/unmute transition.
    g_hasRealPressFrame = false;
    g_hasReplayPressFrame = false;
    g_lastRealPressFrame = 0;
    g_lastReplayPressFrame = 0;
    g_lastStatus = nextEnabled ? "Chimes enabled" : "Chimes muted";
}

// Flat, fixed-size controls avoid ButtonSprite's text-dependent widths.
// All buttons use the same geometry so translated/long labels cannot overlap.
CCLayerColor* makeControlButton(
    char const* caption,
    ccColor4B accent,
    ccColor4B faceColor,
    float width,
    float height,
    CCLabelBMFont** labelOut = nullptr
) {
    auto frame = CCLayerColor::create(accent, width, height);
    if (!frame) return nullptr;
    frame->setAnchorPoint({0.5f, 0.5f});

    auto face = CCLayerColor::create(
        faceColor, width - 4.f, height - 4.f);
    if (!face) return nullptr;
    face->setAnchorPoint({0.f, 0.f});
    face->setPosition({2.f, 2.f});
    frame->addChild(face, 1);

    auto stripe = CCLayerColor::create(
        accent, 3.f, height - 12.f);
    if (stripe) {
        stripe->setAnchorPoint({0.f, 0.f});
        stripe->setPosition({5.f, 6.f});
        face->addChild(stripe, 1);
    }

    auto label = CCLabelBMFont::create(caption, "bigFont.fnt");
    if (label) {
        label->setScale(0.36f);
        label->setColor({242, 247, 252});
        label->setPosition({(width - 4.f) / 2.f, (height - 4.f) / 2.f});
        face->addChild(label, 2);
        if (labelOut) *labelOut = label;
    }

    return frame;
}

class VeyronControlPopup final : public Popup {
protected:
    bool init() {
        constexpr float popupWidth = 360.f;
        constexpr float popupHeight = 250.f;
        if (!Popup::init(popupWidth, popupHeight)) return false;

        this->setTitle("VEYRON PROYECT", "goldFont.fnt", 0.62f, 16.f);
        if (m_title) m_title->setColor({207, 233, 246});

        const auto size = m_mainLayer->getContentSize();

        // Keep the Geometry Dash nine-slice border, but replace the brown body
        // with a dark panel. The overlay sits behind all controls.
        if (m_bgSprite) {
            m_bgSprite->setColor({105, 128, 158});
            m_bgSprite->setOpacity(255);
        }

        auto backdrop = CCLayerColor::create(
            ccColor4B{12, 17, 27, 246},
            size.width - 18.f, size.height - 18.f);
        if (backdrop) {
            backdrop->setAnchorPoint({0.f, 0.f});
            backdrop->setPosition({9.f, 9.f});
            m_mainLayer->addChild(backdrop, 1);
        }

        auto subtitle = CCLabelBMFont::create(
            "MACRO CONTROL CENTER", "chatFont.fnt");
        if (subtitle) {
            subtitle->setScale(0.46f);
            subtitle->setColor({133, 160, 184});
            subtitle->setPosition({size.width / 2.f, 211.f});
            m_mainLayer->addChild(subtitle, 3);
        }

        // Status card: one line for the current state, one for the last action.
        auto statusCard = CCLayerColor::create(
            ccColor4B{22, 31, 45, 255},
            size.width - 34.f, 42.f);
        if (statusCard) {
            statusCard->setAnchorPoint({0.f, 0.f});
            statusCard->setPosition({17.f, 152.f});
            m_mainLayer->addChild(statusCard, 2);

            auto statusAccent = CCLayerColor::create(
                ccColor4B{48, 204, 226, 255}, 3.f, 42.f);
            if (statusAccent) {
                statusAccent->setAnchorPoint({0.f, 0.f});
                statusCard->addChild(statusAccent, 1);
            }

            m_statusLabel = CCLabelBMFont::create("", "chatFont.fnt");
            if (m_statusLabel) {
                m_statusLabel->setAnchorPoint({0.f, 0.5f});
                m_statusLabel->setScale(0.48f);
                m_statusLabel->setColor({232, 240, 248});
                m_statusLabel->setPosition({12.f, 28.f});
                statusCard->addChild(m_statusLabel, 2);
            }

            m_detailLabel = CCLabelBMFont::create("", "chatFont.fnt");
            if (m_detailLabel) {
                m_detailLabel->setAnchorPoint({0.f, 0.5f});
                m_detailLabel->setScale(0.42f);
                m_detailLabel->setColor({139, 161, 184});
                m_detailLabel->setPosition({12.f, 11.f});
                statusCard->addChild(m_detailLabel, 2);
            }
        }

        auto menu = CCMenu::create();
        if (!menu) return false;
        menu->setPosition({size.width / 2.f, 129.f});
        m_mainLayer->addChild(menu, 5);

        auto addButton = [menu, this](
            char const* caption,
            ccColor4B accent,
            ccColor4B face,
            float width,
            CCPoint pos,
            SEL_MenuHandler callback,
            CCLabelBMFont** labelOut
        ) {
            auto visual = makeControlButton(
                caption, accent, face, width, 32.f, labelOut);
            if (!visual) return;

            auto item = CCMenuItemSpriteExtra::create(
                visual, this, callback);
            if (!item) return;
            item->setPosition(pos);
            menu->addChild(item);
        };

        constexpr ccColor4B recAccent{65, 222, 145, 255};
        constexpr ccColor4B recFace{25, 54, 46, 255};
        constexpr ccColor4B stopAccent{247, 99, 112, 255};
        constexpr ccColor4B stopFace{60, 31, 39, 255};
        constexpr ccColor4B playAccent{55, 205, 232, 255};
        constexpr ccColor4B playFace{23, 50, 63, 255};
        constexpr ccColor4B neutralAccent{106, 132, 161, 255};
        constexpr ccColor4B neutralFace{29, 39, 53, 255};
        constexpr ccColor4B hudAccent{177, 139, 255, 255};
        constexpr ccColor4B hudFace{43, 36, 62, 255};
        constexpr ccColor4B soundAccent{244, 185, 86, 255};
        constexpr ccColor4B soundFace{56, 45, 29, 255};

        // 3-column grid. Button width and row spacing are fixed and validated
        // against the popup width (94 px buttons, 24 px horizontal gaps).
        addButton("REC", recAccent, recFace, 94.f,
            {-118.f, 0.f}, menu_selector(VeyronControlPopup::onRecord), nullptr);
        addButton("STOP", stopAccent, stopFace, 94.f,
            {0.f, 0.f}, menu_selector(VeyronControlPopup::onStop), nullptr);
        addButton("PLAY", playAccent, playFace, 94.f,
            {118.f, 0.f}, menu_selector(VeyronControlPopup::onPlayPause),
            &m_playToggleLabel);

        addButton("SAVE", neutralAccent, neutralFace, 94.f,
            {-118.f, -44.f}, menu_selector(VeyronControlPopup::onSave), nullptr);
        addButton("LOAD", neutralAccent, neutralFace, 94.f,
            {0.f, -44.f}, menu_selector(VeyronControlPopup::onLoad), nullptr);
        addButton("HUD ON", hudAccent, hudFace, 94.f,
            {118.f, -44.f}, menu_selector(VeyronControlPopup::onHud),
            &m_hudToggleLabel);

        addButton("SOUND ON", soundAccent, soundFace, 138.f,
            {0.f, -88.f}, menu_selector(VeyronControlPopup::onMute),
            &m_soundToggleLabel);

        auto note = CCLabelBMFont::create(
            "TIMING WINDOWS ARE NOT MEASURED", "chatFont.fnt");
        if (note) {
            note->setScale(0.37f);
            note->setColor({111, 132, 153});
            note->setPosition({size.width / 2.f, 10.f});
            m_mainLayer->addChild(note, 2);
        }

        refreshStatus();
        return true;
    }

    void refreshStatus() {
        char stateText[180]{};
        std::snprintf(
            stateText, sizeof(stateText),
            "STATE  %s    FRAME  %llu    INPUTS  %llu",
            stateName().c_str(),
            static_cast<unsigned long long>(g_gameFrame),
            static_cast<unsigned long long>(recordedInputCount()));

        if (m_statusLabel) m_statusLabel->setString(stateText);
        if (m_detailLabel) m_detailLabel->setString(g_lastStatus.c_str());

        if (m_playToggleLabel) {
            const char* playText = "PLAY";
            if (g_replay.state() == veyron::ReplayState::Playing) {
                playText = "PAUSE";
            } else if (g_replay.state() == veyron::ReplayState::Paused) {
                playText = "RESUME";
            }
            m_playToggleLabel->setString(playText);
        }

        if (m_hudToggleLabel) {
            m_hudToggleLabel->setString(
                g_runtimeHudVisible ? "HUD ON" : "HUD OFF");
        }

        const bool soundEnabled = settingBool("audio-enabled", true);
        if (m_soundToggleLabel) {
            m_soundToggleLabel->setString(
                soundEnabled ? "SOUND ON" : "SOUND OFF");
        }
    }

    void onRecord(CCObject*) {
        if (g_recorder.state() == veyron::RecorderState::Recording) {
            g_lastStatus = "Already recording";
        } else {
            beginRecording();
        }
        refreshStatus();
    }

    void onStop(CCObject*) {
        const bool wasRecording =
            g_recorder.state() == veyron::RecorderState::Recording;
        if (wasRecording) finishRecording(true);

        if (g_activePlayLayer) stopReplay(g_activePlayLayer);
        else stopReplay(nullptr);

        if (!wasRecording) g_lastStatus = "Playback stopped";
        refreshStatus();
    }

    void onPlayPause(CCObject*) {
        startOrTogglePlayback();
        refreshStatus();
    }

    void onSave(CCObject*) {
        saveCurrentMacro(true);
        refreshStatus();
    }

    void onLoad(CCObject*) {
        loadCurrentMacro(true);
        refreshStatus();
    }

    void onHud(CCObject*) {
        toggleHud();
        refreshStatus();
    }

    void onMute(CCObject*) {
        toggleMute();
        refreshStatus();
    }

public:
    static VeyronControlPopup* create() {
        auto ret = new VeyronControlPopup();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

private:
    CCLabelBMFont* m_statusLabel = nullptr;
    CCLabelBMFont* m_detailLabel = nullptr;
    CCLabelBMFont* m_playToggleLabel = nullptr;
    CCLabelBMFont* m_hudToggleLabel = nullptr;
    CCLabelBMFont* m_soundToggleLabel = nullptr;
};

} // namespace

// This hook is the authoritative event boundary for actual GD button changes.
// The game input method is called directly again for playback; g_injectedCall
// prevents those synthetic events from being recorded as fresh user input.
class $modify(VeyronBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        auto* playLayer = g_activePlayLayer;
        const bool isLivePlayLayer = playLayer &&
            static_cast<GJBaseGameLayer*>(playLayer) == this;

        if (!g_injectedCall && isLivePlayLayer && button > 0) {
            const bool isPlayer2 = !isPlayer1;
            const bool capturePlayer = isPlayer2
                ? settingBool("capture-player2", true)
                : settingBool("capture-player1", true);

            if (capturePlayer && g_recorder.state() == veyron::RecorderState::Recording) {
                g_recorder.record(button, isPlayer2, down, g_gameFrame);
            }

            if (down) {
                g_recentPressFrames.push_back(g_gameFrame);
                trimRecentPresses();
                playToneForInterval(g_gameFrame, false);
            }
        }

        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto* playLayer = g_activePlayLayer;
        const bool isLivePlayLayer = playLayer &&
            static_cast<GJBaseGameLayer*>(playLayer) == this;

        // Drain events at the current tick before vanilla processes its queued
        // inputs. Advance the counter only after the non-half simulation pass,
        // so a captured frame-N event is replayed at the start of frame N rather
        // than being shifted one tick forward. This still needs runtime testing.
        if (isLivePlayLayer && !isHalfTick) {
            processReplayTick(playLayer);
        }

        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);

        if (isLivePlayLayer && !isHalfTick) {
            ++g_gameFrame;
            if (g_replayFinishedThisTick) {
                releasePhysicalReplayInputs(playLayer, true);
                g_replayFinishedThisTick = false;
                g_lastStatus = "Playback finished";
            }
        }
    }
};

class $modify(VeyronPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        // Defensive hand-off: if a previous layer did not reach onQuit before
        // a new one is initialized, finish its recording and release all
        // synthetic inputs while the old PlayLayer pointer is still valid.
        if (g_activePlayLayer && g_activePlayLayer != this) {
            auto* previousLayer = g_activePlayLayer;
            if (g_recorder.state() == veyron::RecorderState::Recording) {
                finishRecording(true);
            }
            stopReplay(previousLayer);
            clearHud();
            g_activePlayLayer = nullptr;
        }

        g_activePlayLayer = this;
        g_gameFrame = 0;
        g_hudRefreshTimer = 0.f;
        g_recentPressFrames.clear();
        g_hasRealPressFrame = false;
        g_hasReplayPressFrame = false;
        g_replayHeldInputs.clear();
        g_replayKeysTemporarilyReleased = false;
        g_replayFinishedThisTick = false;
        g_replay.stop();
        attachHud(this);

        log::info("Veyron attached to PlayLayer (GD 2.2081 target).");
        return true;
    }

    void update(float dt) {
        PlayLayer::update(dt);
        if (g_activePlayLayer != this || !g_hudLabel) return;

        g_hudRefreshTimer += dt;
        const float refreshInterval = static_cast<float>(
            std::clamp(settingDouble("hud-refresh-interval", 0.10), 0.05, 0.50));
        if (g_hudRefreshTimer >= refreshInterval) {
            g_hudRefreshTimer = 0.f;
            updateHudText();
        }
    }

    void resetLevel() {
        if (g_activePlayLayer == this) {
            if (g_recorder.state() == veyron::RecorderState::Recording) finishRecording(true);
            stopReplay(this);
            g_gameFrame = 0;
            g_recentPressFrames.clear();
            g_hasRealPressFrame = false;
            g_hasReplayPressFrame = false;
            g_lastStatus = "Level reset";
        }

        PlayLayer::resetLevel();
        if (g_activePlayLayer == this) attachHud(this, false);
    }

    void onQuit() {
        if (g_activePlayLayer == this) {
            if (g_recorder.state() == veyron::RecorderState::Recording) finishRecording(true);
            stopReplay(this);
            clearHud();
            g_activePlayLayer = nullptr;
            g_gameFrame = 0;
            g_recentPressFrames.clear();
            g_lastStatus = "Level closed";
        }

        PlayLayer::onQuit();
    }
};

class $modify(VeyronPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto const win = CCDirector::sharedDirector()->getWinSize();
        auto menu = CCMenu::create();
        if (!menu) return;
        menu->setID("veyron-pause-menu");

        auto sprite = ButtonSprite::create(
            "VEYRON", "bigFont.fnt", "GJ_button_05.png", 0.46f);
        if (!sprite) return;

        auto button = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(VeyronPauseLayer::onOpenVeyron));
        if (!button) return;

        button->setID("veyron-pause-button");
        menu->addChild(button);

        // The original top-right location overlaps the pause/settings gear on
        // common 16:9 layouts. Move the entry slightly inward and downward.
        menu->setPosition({win.width - 84.f, win.height - 142.f});
        this->addChild(menu, 10'000);
    }

    void onOpenVeyron(CCObject*) {
        if (auto popup = VeyronControlPopup::create()) popup->show();
    }
};

} // namespace veyron::geode_integration
