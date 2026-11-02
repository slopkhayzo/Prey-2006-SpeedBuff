// Exercises the INI parser without the game.

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "config.h"

namespace {

using preyspeedbuff::Config;
using preyspeedbuff::LoadConfig;

int g_failures = 0;

bool Load(const std::string& text, Config& config, std::string& error) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("PreySpeedBuffConfigTest-" + std::to_string(GetCurrentProcessId()) + ".ini");
    {
        std::ofstream file(path, std::ios::binary);
        file << text;
    }
    const bool loaded = LoadConfig(path, config, error);
    std::filesystem::remove(path);
    return loaded;
}

void ExpectRejected(const char* name, const std::string& text) {
    Config config;
    std::string error;
    if (Load(text, config, error) || error.empty()) {
        std::printf("FAIL %s: expected a rejection\n", name);
        ++g_failures;
    }
}

void Expect(const char* name, bool condition) {
    if (!condition) {
        std::printf("FAIL %s\n", name);
        ++g_failures;
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2) {
        Config shipped;
        std::string error;
        if (!LoadConfig(argv[1], shipped, error)) {
            std::printf("FAIL shipped ini: %s\n", error.c_str());
            return 1;
        }
        Expect("shipped ini overrides a speed", shipped.HasOverride());
    }

    {
        Config config;
        std::string error;
        Expect("full config loads",
               Load("\xef\xbb\xbf; comment\r\n[Speed]\r\nENABLED = yes\r\n"
                    "walk = 300\r\nrun=512.5\r\ncrouch = GAME\r\n",
                    config, error));
        Expect("enabled", config.enabled);
        Expect("walk", config.walk == 300.0f);
        Expect("run", config.run == 512.5f);
        Expect("crouch keeps the game value", !config.crouch.has_value());
        Expect("has override", config.HasOverride());
    }
    {
        Config config;
        std::string error;
        Expect("missing keys keep game values", Load("[speed]\n", config, error));
        Expect("all game means no override", !config.HasOverride());
    }
    {
        Config config;
        std::string error;
        Expect("zero and maximum load",
               Load("[speed]\nwalk=0\nrun=100000\n", config, error));
        Expect("zero is an override", config.walk == 0.0f);
    }
    {
        Config config;
        std::string error;
        Expect("disabled loads", Load("[speed]\nenabled=false\nwalk=300\n", config, error));
        Expect("disabled means no override", !config.HasOverride());
    }

    ExpectRejected("negative", "[speed]\nwalk=-1\n");
    ExpectRejected("too large", "[speed]\nwalk=100001\n");
    ExpectRejected("nan", "[speed]\nwalk=nan\n");
    ExpectRejected("infinity", "[speed]\nwalk=inf\n");
    ExpectRejected("trailing text", "[speed]\nwalk=300 units\n");
    ExpectRejected("empty value", "[speed]\nwalk=\n");
    ExpectRejected("bad boolean", "[speed]\nenabled=maybe\n");
    ExpectRejected("unknown key", "[speed]\nsprint=300\n");
    ExpectRejected("unknown section", "[patch]\nenabled=true\n");
    ExpectRejected("duplicate key", "[speed]\nwalk=1\nwalk=2\n");
    ExpectRejected("key outside a section", "walk=300\n");
    ExpectRejected("line without a separator", "[speed]\nwalk\n");

    if (g_failures != 0) return 1;
    std::printf("config tests passed\n");
    return 0;
}
