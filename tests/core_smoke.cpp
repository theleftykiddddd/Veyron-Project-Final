#include "veyron/FormatIO.hpp"
#include "veyron/Macro.hpp"
#include "veyron/Recorder.hpp"
#include "veyron/Replay.hpp"
#include "veyron/WindowAnalyzer.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, std::string_view description) {
    if (condition) return;
    ++g_failures;
    std::cerr << "[FAIL] " << description << '\n';
}

bool sameEvent(
    const veyron::InputEvent& left,
    const veyron::InputEvent& right
) {
    return left.frame == right.frame &&
        left.button == right.button &&
        left.player2 == right.player2 &&
        left.down == right.down;
}

void testWindowAnalysis() {
    const auto bands = veyron::WindowAnalyzer::defaultBands();

    check(bands.size() == 9, "default analyzer exposes nine bands");
    check(!bands.empty() && bands.front().colorRGB == 0xF04B68,
          "first analyzer band retains its expected color");
    check(veyron::WindowAnalyzer::findBand(bands, 1) == 0,
          "one-frame interval maps to the first band");
    check(veyron::WindowAnalyzer::findBand(bands, 3) == 2,
          "three-frame interval maps to the third band");
    check(veyron::WindowAnalyzer::findBand(bands, 24) == 8,
          "24-frame interval maps to the final band");

    const std::vector<veyron::MeasuredClick> sample = {
        {10, 1, false},
        {20, 3, false},
        {30, 8, false},
        {40, -1, false}
    };
    const auto stats = veyron::WindowAnalyzer::summarize(sample, bands);

    check(stats.measured == 3 && stats.unknown == 1,
          "window stats separate measured and unknown samples");
    check(stats.minimumFrames == 1 && stats.maximumFrames == 8,
          "window stats retain min/max interval");
    check(std::abs(stats.averageFrames - 4.0) < 0.001,
          "window stats calculate the average interval");

    check(
        veyron::WindowAnalyzer::toneFrequencyHz(1) <
            veyron::WindowAnalyzer::toneFrequencyHz(24),
        "tone pitch increases across the configured interval range");
}

veyron::Macro makeTestMacro() {
    veyron::Macro macro;
    macro.metadata.title = "Veyron regression macro";
    macro.metadata.author = "Veyron tests";
    macro.metadata.gameVersion = 2.2081;
    macro.metadata.tickRate = 240.0;
    macro.metadata.sourceFormat = "veyron-vpr";

    // Two events share frame 6 so stable ordering can be checked.
    macro.inputs = {
        {5, 1, false, true},
        {6, 1, false, false},
        {6, 2, true, true}
    };
    return macro;
}

void testMacroValidationAndStableSort() {
    auto macro = makeTestMacro();
    check(macro.isValid(), "well-formed macro passes validation");

    macro.inputs = {
        {6, 1, false, false},
        {5, 1, false, true},
        {6, 2, true, true}
    };
    check(!macro.isValid(), "unsorted events fail validation");

    macro.sortByFrameStable();
    check(macro.isValid(), "stable sorting produces a valid event sequence");
    check(macro.inputs.size() == 3 &&
              macro.inputs[0].frame == 5 &&
              macro.inputs[1].button == 1 &&
              !macro.inputs[1].down &&
              macro.inputs[2].button == 2 &&
              macro.inputs[2].player2 &&
              macro.inputs[2].down,
          "stable sorting preserves original order for equal-frame events");

    macro.metadata.tickRate = std::numeric_limits<double>::quiet_NaN();
    check(!macro.isValid(), "non-finite tick rate fails macro validation");
}

veyron::Macro testRecorder() {
    veyron::Recorder recorder;
    veyron::MacroMetadata metadata;
    metadata.title = "Recorder regression";
    metadata.author = "Veyron tests";
    metadata.gameVersion = 2.2081;
    metadata.tickRate = 240.0;

    check(recorder.begin(metadata, 100), "recorder enters recording state");
    check(!recorder.begin(metadata, 100),
          "recorder refuses to begin a second recording while active");

    check(!recorder.record(1, false, true, 99),
          "recorder rejects events earlier than the recording origin");
    check(recorder.record(1, false, true, 105),
          "recorder captures a press");
    check(recorder.record(1, false, false, 106),
          "recorder captures a release");
    check(recorder.record(2, true, true, 106),
          "recorder captures a second-player event on the same frame");

    auto result = recorder.stop();
    check(result.has_value(), "recorder returns a macro when stopped");
    check(recorder.state() == veyron::RecorderState::Stopped,
          "recorder enters stopped state");

    if (!result) return {};
    check(result->inputs.size() == 3, "recorder retains every accepted event");
    check(result->inputs[0].frame == 5 &&
              result->inputs[1].frame == 6 &&
              result->inputs[2].frame == 6,
          "recorder converts absolute ticks to recording-relative frames");
    check(result->inputs[2].player2,
          "recorder preserves the second-player flag");
    return *result;
}

void testReplayScheduler(const veyron::Macro& macro) {
    veyron::ReplayScheduler replay;
    check(replay.load(macro), "scheduler accepts a valid macro");
    check(replay.state() == veyron::ReplayState::Idle,
          "loaded scheduler starts idle");

    replay.start();
    check(replay.state() == veyron::ReplayState::Playing,
          "scheduler enters playing state");

    auto beforeFirst = replay.drainDueEvents(4);
    check(beforeFirst.empty(), "scheduler does not emit future events");

    auto first = replay.drainDueEvents(5);
    check(first.size() == 1 && sameEvent(first[0], macro.inputs[0]),
          "scheduler emits the first due event exactly");

    replay.pause();
    check(replay.state() == veyron::ReplayState::Paused,
          "scheduler enters paused state");
    check(replay.drainDueEvents(6).empty(),
          "paused scheduler does not drain due events");

    replay.resume();
    check(replay.state() == veyron::ReplayState::Playing,
          "scheduler resumes playback");

    auto sameFrame = replay.drainDueEvents(6);
    check(sameFrame.size() == 2,
          "scheduler emits every event with the same due frame");
    check(sameFrame.size() == 2 &&
              sameEvent(sameFrame[0], macro.inputs[1]) &&
              sameEvent(sameFrame[1], macro.inputs[2]),
          "scheduler preserves stable ordering for same-frame events");
    check(replay.state() == veyron::ReplayState::Finished,
          "scheduler marks playback finished after the final event");

    replay.start();
    check(replay.state() == veyron::ReplayState::Playing &&
              replay.cursor() == 0,
          "scheduler can restart a finished macro from frame zero");

    replay.stop();
    check(replay.state() == veyron::ReplayState::Idle &&
              replay.cursor() == 0,
          "stopping resets scheduler state and cursor");
}

void testVprRoundTrip(const veyron::Macro& macro) {
    std::error_code ec;
    const auto path = std::filesystem::temp_directory_path(ec) /
        "veyron-v0.3-core-regression.vpr";

    if (ec) {
        check(false, "temporary directory is available for VPR round-trip");
        return;
    }

    auto exported = veyron::exportVpr(macro, path.string());
    check(exported.ok, "valid macro exports to VPR");

    veyron::Macro imported;
    auto loaded = veyron::importVpr(path.string(), imported);
    check(loaded.ok, "exported VPR imports successfully");
    check(imported.metadata.title == macro.metadata.title &&
              imported.metadata.author == macro.metadata.author,
          "VPR round-trip preserves metadata");
    check(imported.inputs.size() == macro.inputs.size(),
          "VPR round-trip preserves event count");

    bool eventsMatch = imported.inputs.size() == macro.inputs.size();
    if (eventsMatch) {
        for (std::size_t i = 0; i < macro.inputs.size(); ++i) {
            if (!sameEvent(imported.inputs[i], macro.inputs[i])) {
                eventsMatch = false;
                break;
            }
        }
    }
    check(eventsMatch, "VPR round-trip preserves each input event");

    veyron::Macro emptyMacro;
    auto rejected = veyron::exportVpr(emptyMacro, path.string());
    check(!rejected.ok, "empty macro is rejected by the VPR exporter");

    std::filesystem::remove(path, ec);
}

} // namespace

int main() {
    testWindowAnalysis();
    testMacroValidationAndStableSort();

    const auto captured = testRecorder();
    if (!captured.inputs.empty()) {
        testReplayScheduler(captured);
        testVprRoundTrip(captured);
    } else {
        check(false, "recorder test produced a macro for downstream tests");
    }

    if (g_failures != 0) {
        std::cerr << g_failures << " regression check(s) failed.\n";
        return 1;
    }

    std::cout << "All Veyron core regression checks passed.\n";
    return 0;
}
