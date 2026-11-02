#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace preyspeedbuff {

inline constexpr float kMaximumSpeed = 100000.0f;

struct Config {
    bool enabled = true;
    // An empty value keeps the game's own pm_* cvar for that movement state.
    std::optional<float> walk;
    std::optional<float> run;
    std::optional<float> crouch;

    bool HasOverride() const {
        return enabled && (walk.has_value() || run.has_value() || crouch.has_value());
    }
};

bool LoadConfig(const std::filesystem::path& path, Config& config,
                std::string& error);

} // namespace preyspeedbuff
