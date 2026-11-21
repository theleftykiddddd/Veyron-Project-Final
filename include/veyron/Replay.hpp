#pragma once

#include "veyron/Macro.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace veyron {

enum class ReplayState { Idle, Playing, Paused, Finished };

// Scheduler only: this class does not inject input into Geometry Dash.
class ReplayScheduler {
public:
    bool load(Macro macro);
    void start();
    void pause();
    void resume();
    void stop();

    // Removes and returns every event due at or before this macro frame.
    // The integration layer can now inject the exact press/release data.
    std::vector<InputEvent> drainDueEvents(std::uint64_t frame);

    // Backwards-compatible counter used by the original standalone tests.
    std::size_t advanceTo(std::uint64_t frame);

    [[nodiscard]] const Macro& macro() const { return m_macro; }
    [[nodiscard]] ReplayState state() const { return m_state; }
    [[nodiscard]] std::size_t cursor() const { return m_cursor; }

private:
    Macro m_macro;
    std::size_t m_cursor = 0;
    ReplayState m_state = ReplayState::Idle;
};

} // namespace veyron
