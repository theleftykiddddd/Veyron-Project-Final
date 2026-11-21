// Veyron Proyect - Geode integration for GD 2.2081 / Geode 5.11.2.
// The platform-neutral recorder and scheduler remain in src/core; this file
// connects them to the game's simulation hooks and Cocos UI.

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <SimpleAudioEngine.h>

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
CCLabelBMFont* g_hudLabel = nullptr;

std::uint64_t g_gameFrame = 0;
std::uint64_t g_replayStartFrame = 0;
std::uint64_t g_lastRealPressFrame = 0;
std::uint64_t g_lastReplayPressFrame = 0;
bool g_hasRealPressFrame = false;
bool g_hasReplayPressFrame = false;
bool g_injectedCall = false;
bool g_runtimeHudVisible = true;
bool g_runtimeMuted = false;
bool g_replayKeysTemporarilyReleased = false;
bool g_replayFinishedThisTick = false;
float g_hudRefreshTimer = 0.f;
std::deque<std::uint64_t> g_recentPressFrames;
std::set<HeldInput> g_replayHeldInputs;
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
    if (!mod) return {};

    std::error_code ec;
    auto toneDir = mod->getSaveDir() / "tones";
    std::filesystem::create_directories(toneDir, ec);
    if (ec) return {};

    const auto safeVolume = std::clamp(volume, 0.0, 1.0);
    const auto roundedFrequency = static_cast<int>(std::lround(frequencyHz));
    const auto volumePercent = static_cast<int>(std::lround(safeVolume * 100.0));
    auto path = toneDir / ("veyron_" + std::to_string(roundedFrequency) +
                           "hz_" + std::to_string(volumePercent) + "v.wav");
    if (std::filesystem::exists(path, ec) && !ec) return path;
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
    return path;
}

void playToneForInterval(std::uint64_t frame, bool replaySource) {
    if (!settingBool("audio-enabled", true) || g_runtimeMuted) return;

    auto& previousFrame = replaySource ? g_lastReplayPressFrame : g_lastRealPressFrame;
    auto& hasPrevious = replaySource ? g_hasReplayPressFrame : g_hasRealPressFrame;
    int intervalFrames = 1;
    if (hasPrevious && frame >= previousFrame) {
        intervalFrames = static_cast<int>(std::clamp<std::uint64_t>(
            frame - previousFrame, 1, 24));
    }
    previousFrame = frame;
    hasPrevious = true;

    const double frequency = veyron::WindowAnalyzer::toneFrequencyHz(
        intervalFrames, 1, 24,
        settingDouble("audio-min-hz", 95.0),
        settingDouble("audio-max-hz", 880.0));
    const auto volume = settingDouble("master-volume", 0.55);
    const auto file = makeToneFile(frequency, volume);
    if (file.empty()) return;

    // Generated WAV files encode their own amplitude, so Cocos gain stays 1.
    const auto fileName = geode::utils::string::pathToString(file);
    CocosDenshion::SimpleAudioEngine::sharedEngine()->playEffect(
        fileName.c_str(), false, 1.f, 0.f, 1.f);
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

    for (auto const& held : g_replayHeldInputs) {
        rawInject(layer, false, held.button, held.player2);
    }
    g_replayKeysTemporarilyReleased = !clearLogicalState && !g_replayHeldInputs.empty();
    if (clearLogicalState) g_replayHeldInputs.clear();
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

void updateHudText() {
    if (!g_hudLabel || !g_activePlayLayer) return;

    const auto cps = recentCpsCount();
    const auto inputCount = recordedInputCount();
    const auto state = stateName();
    const auto safe = settingBool("safe-mode", true) ? "SAFE" : "FULL";

    char text[320]{};
    std::snprintf(
        text, sizeof(text),
        "VEYRON  [%s | %s]\nTICK: %llu   INPUTS: %llu\nCPS (1s): %zu   WINDOW: ?\n%s",
        state.c_str(), safe,
        static_cast<unsigned long long>(g_gameFrame),
        static_cast<unsigned long long>(inputCount),
        cps, g_lastStatus.c_str());
    g_hudLabel->setString(text);
}

void attachHud(PlayLayer* layer, bool readPersistentSetting = true) {
    if (!layer) return;
    if (g_hudLabel) {
        g_hudLabel->removeFromParentAndCleanup(true);
        g_hudLabel = nullptr;
    }

    if (readPersistentSetting) {
        g_runtimeHudVisible = settingBool("hud-enabled", true);
    }
    if (!g_runtimeHudVisible) return;

    g_hudLabel = CCLabelBMFont::create("VEYRON", "bigFont.fnt");
    if (!g_hudLabel) return;
    g_hudLabel->setAnchorPoint({1.f, 1.f});
    g_hudLabel->setScale(static_cast<float>(settingDouble("hud-scale", 0.80)));
    g_hudLabel->setOpacity(225);
    g_hudLabel->setZOrder(10'000);
    layer->addChild(g_hudLabel, 10'000);

    const auto win = CCDirector::sharedDirector()->getWinSize();
    const auto position = settingString("hud-position", "top-right");
    if (position == "top-left") {
        g_hudLabel->setAnchorPoint({0.f, 1.f});
        g_hudLabel->setPosition({14.f, win.height - 14.f});
    } else if (position == "bottom-left") {
        g_hudLabel->setAnchorPoint({0.f, 0.f});
        g_hudLabel->setPosition({14.f, 14.f});
    } else if (position == "bottom-right") {
        g_hudLabel->setAnchorPoint({1.f, 0.f});
        g_hudLabel->setPosition({win.width - 14.f, 14.f});
    } else {
        g_hudLabel->setAnchorPoint({1.f, 1.f});
        g_hudLabel->setPosition({win.width - 14.f, win.height - 14.f});
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
        if (g_hudLabel) g_hudLabel->removeFromParentAndCleanup(true);
        g_hudLabel = nullptr;
    } else {
        attachHud(g_activePlayLayer, false);
        g_runtimeHudVisible = true;
    }
}

void toggleMute() {
    g_runtimeMuted = !g_runtimeMuted;
    g_lastStatus = g_runtimeMuted ? "Chimes muted" : "Chimes enabled (settings permitting)";
}

class VeyronControlPopup final : public Popup {
protected:
    bool init() {
        if (!Popup::init(330.f, 228.f)) return false;
        this->setTitle("VEYRON PROYECT");

        auto const size = m_mainLayer->getContentSize();
        auto status = CCLabelBMFont::create("", "bigFont.fnt");
        if (status) {
            status->setScale(0.34f);
            status->setPosition({size.width / 2.f, size.height - 54.f});
            status->setID("veyron-status"_spr);
            m_mainLayer->addChild(status);
            m_statusLabel = status;
        }

        auto menu = CCMenu::create();
        menu->setPosition({size.width / 2.f, size.height / 2.f - 13.f});
        m_mainLayer->addChild(menu);

        auto addButton = [menu, this](char const* label, char const* sprite,
                                      CCPoint pos, SEL_MenuHandler callback) {
            auto visual = ButtonSprite::create(label, "bigFont.fnt", sprite, 0.62f);
            if (!visual) return;
            auto item = CCMenuItemSpriteExtra::create(visual, this, callback);
            if (!item) return;
            item->setPosition(pos);
            menu->addChild(item);
        };

        addButton("REC", "GJ_button_01.png", {-91.f, 35.f}, menu_selector(VeyronControlPopup::onRecord));
        addButton("STOP", "GJ_button_06.png", {0.f, 35.f}, menu_selector(VeyronControlPopup::onStop));
        addButton("PLAY / PAUSE", "GJ_button_02.png", {91.f, 35.f}, menu_selector(VeyronControlPopup::onPlayPause));
        addButton("SAVE VPR", "GJ_button_04.png", {-91.f, -12.f}, menu_selector(VeyronControlPopup::onSave));
        addButton("LOAD VPR", "GJ_button_05.png", {0.f, -12.f}, menu_selector(VeyronControlPopup::onLoad));
        addButton("HUD", "GJ_button_03.png", {91.f, -12.f}, menu_selector(VeyronControlPopup::onHud));

        auto soundButton = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("MUTE / SOUND", "bigFont.fnt", "GJ_button_03.png", 0.55f),
            this, menu_selector(VeyronControlPopup::onMute));
        if (soundButton) {
            soundButton->setPosition({0.f, -58.f});
            menu->addChild(soundButton);
        }

        auto note = CCLabelBMFont::create(
            "Window values are unmeasured. Options: Geode mod settings.",
            "chatFont.fnt");
        if (note) {
            note->setScale(0.48f);
            note->setPosition({size.width / 2.f, 19.f});
            m_mainLayer->addChild(note);
        }

        refreshStatus();
        return true;
    }

    void refreshStatus() {
        if (!m_statusLabel) return;
        std::string text = "STATE: " + stateName() + " | FRAMES: " +
            std::to_string(g_gameFrame) + " | INPUTS: " +
            std::to_string(recordedInputCount()) + "\n" + g_lastStatus;
        m_statusLabel->setString(text.c_str());
    }

    void onRecord(CCObject*) {
        if (g_recorder.state() == veyron::RecorderState::Recording) {
            g_lastStatus = "Already recording";
        } else beginRecording();
        refreshStatus();
    }

    void onStop(CCObject*) {
        const bool wasRecording = g_recorder.state() == veyron::RecorderState::Recording;
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

        g_activePlayLayer = this;
        g_gameFrame = 0;
        g_hudRefreshTimer = 0.f;
        g_recentPressFrames.clear();
        g_hasRealPressFrame = false;
        g_hasReplayPressFrame = false;
        g_runtimeMuted = false;
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
        if (g_hudRefreshTimer >= 0.10f) {
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
            if (g_hudLabel) {
                g_hudLabel->removeFromParentAndCleanup(true);
                g_hudLabel = nullptr;
            }
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

        auto sprite = ButtonSprite::create(
            "VEYRON", "bigFont.fnt", "GJ_button_05.png", 0.60f);
        if (!sprite) return;

        auto button = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(VeyronPauseLayer::onOpenVeyron));
        if (!button) return;

        menu->addChild(button);
        menu->setPosition({win.width - 66.f, win.height - 58.f});
        this->addChild(menu, 10'000);
    }

    void onOpenVeyron(CCObject*) {
        if (auto popup = VeyronControlPopup::create()) popup->show();
    }
};

} // namespace veyron::geode_integration
