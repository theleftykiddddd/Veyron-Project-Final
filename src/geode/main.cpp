#include <Geode/Geode.hpp>
#include "veyron/WindowAnalyzer.hpp"

using namespace geode::prelude;

$execute {
    const auto bands = veyron::WindowAnalyzer::defaultBands();
    log::info(
        "Veyron Proyect v0.2.0 loaded for Geometry Dash 2.2081. {} default analysis bands loaded.",
        bands.size());
    log::info("PauseLayer controls, PlayLayer HUD, input capture and scheduler-driven playback are registered.");
    log::info("Real timing-window measurement is intentionally reported as unknown until a validated backend exists.");
}
