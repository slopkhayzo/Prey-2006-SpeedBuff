// Runs the plugin against the real retail gamex86.dll without starting the game.
// The DLL is mapped read-only into this process and its unmodified AdjustSpeed
// runs on a zeroed fake player, so these checks cover the shipped machine code,
// the detour, and the validation against a genuine image.
//
// usage: PreySpeedBuffRetailTest <PreySpeedBuff.asi> <retail gamex86.dll> <scenario>

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "game_layout.h"

namespace {

using namespace preyspeedbuff;

constexpr int kSkipped = 77;

// Retail layout details only this test needs.
constexpr std::uintptr_t kStaminaCvarRva = 0x00383340;
constexpr std::uintptr_t kStaminaThresholdCvarRva = 0x00383374;
constexpr std::uintptr_t kStaminaRateCvarRva = 0x003833a8;
constexpr std::uintptr_t kNoclipSpeedCvarRva = 0x00383208;
constexpr std::uintptr_t kSpectateSpeedCvarRva = 0x0038323c;
constexpr std::size_t kPlayerButtonsOffset = 0x12ac;
constexpr std::size_t kPlayerForwardOffset = 0x12ad;
constexpr std::size_t kPlayerStaminaOffset = 0x17b8;
constexpr std::size_t kPhysicsWalkSpeedOffset = 0x2b8;
constexpr std::size_t kPhysicsCrouchSpeedOffset = 0x2bc;
constexpr std::size_t kPhysicsMovementFlagsOffset = 0x228;
constexpr std::size_t kFakePlayerSize = 0x4000;
constexpr std::uint8_t kButtonRun = 0x02;
constexpr std::uint8_t kPmfDucked = 0x01;

using AdjustSpeedFn = void(__thiscall*)(std::uint8_t* player);
using GetStateFn = DWORD(*)();
using IsInstalledFn = BOOL(*)();

int g_failures = 0;

void Fail(const char* what) {
    std::printf("FAIL %s\n", what);
    ++g_failures;
}

void ExpectSpeed(const char* what, float actual, float expected) {
    if (std::fabs(actual - expected) > 0.01f) {
        std::printf("FAIL %s: got %.3f, expected %.3f\n", what, actual, expected);
        ++g_failures;
    }
}

class FakeGame {
public:
    explicit FakeGame(std::uint8_t* module) : module_(module), player_(kFakePlayerSize) {
        // Never registered with an engine, so each internalVar points at itself
        // and floatValue is still zero. Install the values player.def applies.
        SetCvar(kWalkSpeedCvarRva, 180.0f);
        SetCvar(kRunSpeedCvarRva, 220.0f);
        SetCvar(kCrouchSpeedCvarRva, 80.0f);
        SetCvar(kStaminaCvarRva, 10.0f);
        SetCvar(kStaminaThresholdCvarRva, 4.0f);
        SetCvar(kStaminaRateCvarRva, 0.2f);
        SetCvar(kNoclipSpeedCvarRva, 200.0f);
        SetCvar(kSpectateSpeedCvarRva, 450.0f);
    }

    void SetCvar(std::uintptr_t rva, float value) {
        std::memcpy(module_ + rva + kCvarFloatOffset, &value, sizeof(value));
    }
    void SetMultiplayer(bool value) { module_[kIsMultiplayerRva] = value ? 1 : 0; }
    void Reset() { std::fill(player_.begin(), player_.end(), std::uint8_t{0}); }
    void Press(std::uint8_t buttons, std::int8_t forward) {
        player_[kPlayerButtonsOffset] = buttons;
        player_[kPlayerForwardOffset] = static_cast<std::uint8_t>(forward);
    }
    void SetStamina(float value) {
        std::memcpy(&player_[kPlayerStaminaOffset], &value, sizeof(value));
    }
    float Stamina() const { return ReadFloat(kPlayerStaminaOffset); }
    void SetByte(std::size_t offset, std::uint8_t value) { player_[offset] = value; }
    void SetCrouching() {
        player_[kPlayerPhysicsOffset + kPhysicsMovementFlagsOffset] = kPmfDucked;
    }
    void Tick() {
        reinterpret_cast<AdjustSpeedFn>(module_ + kAdjustSpeedRva)(player_.data());
    }
    float WalkSpeed() const {
        return ReadFloat(kPlayerPhysicsOffset + kPhysicsWalkSpeedOffset);
    }
    float CrouchSpeed() const {
        return ReadFloat(kPlayerPhysicsOffset + kPhysicsCrouchSpeedOffset);
    }

private:
    float ReadFloat(std::size_t offset) const {
        float value = 0.0f;
        std::memcpy(&value, &player_[offset], sizeof(value));
        return value;
    }

    std::uint8_t* module_;
    std::vector<std::uint8_t> player_;
};

void CheckRetailBehavior(std::uint8_t* module) {
    FakeGame game(module);
    game.Reset();
    game.Tick();
    ExpectSpeed("retail walk", game.WalkSpeed(), 180.0f);
    ExpectSpeed("retail crouch", game.CrouchSpeed(), 80.0f);
    game.Reset();
    game.Press(kButtonRun, 127);
    game.SetStamina(10.0f);
    game.Tick();
    ExpectSpeed("retail run", game.WalkSpeed(), 220.0f);
}

// Expects walk=300 run=500 crouch=100.
void CheckOverrideBehavior(std::uint8_t* module) {
    FakeGame game(module);

    game.Reset();
    game.SetStamina(10.0f);
    game.Tick();
    ExpectSpeed("walking", game.WalkSpeed(), 300.0f);
    ExpectSpeed("crouch speed", game.CrouchSpeed(), 100.0f);

    game.Reset();
    game.Press(kButtonRun, 127);
    game.SetStamina(10.0f);
    game.Tick();
    ExpectSpeed("running", game.WalkSpeed(), 500.0f);
    ExpectSpeed("stamina still drains", game.Stamina(), 10.0f - 0.016f);

    // Below the threshold retail blends toward walking; 1.984 / 4 = 0.496.
    game.Reset();
    game.Press(kButtonRun, 127);
    game.SetStamina(2.0f);
    game.Tick();
    ExpectSpeed("stamina blend", game.WalkSpeed(), 300.0f * 0.504f + 500.0f * 0.496f);

    game.Reset();
    game.Press(kButtonRun, 127);
    game.SetStamina(0.0f);
    game.Tick();
    ExpectSpeed("exhausted", game.WalkSpeed(), 300.0f);

    game.Reset();
    game.SetCrouching();
    game.Press(kButtonRun, 127);
    game.SetStamina(10.0f);
    game.Tick();
    ExpectSpeed("no stamina drain while crouched", game.Stamina(), 10.0f);
    ExpectSpeed("crouch speed while crouched", game.CrouchSpeed(), 100.0f);

    // Player.def and the console change the cvars; the INI values must still win.
    game.SetCvar(kWalkSpeedCvarRva, 999.0f);
    game.SetCvar(kCrouchSpeedCvarRva, 999.0f);
    game.Reset();
    game.Tick();
    ExpectSpeed("override survives a cvar change", game.WalkSpeed(), 300.0f);
    ExpectSpeed("crouch override survives a cvar change", game.CrouchSpeed(), 100.0f);
    game.SetCvar(kWalkSpeedCvarRva, 180.0f);
    game.SetCvar(kCrouchSpeedCvarRva, 80.0f);

    game.Reset();
    game.SetByte(kPlayerNoclipOffset, 1);
    game.Tick();
    ExpectSpeed("noclip stays retail", game.WalkSpeed(), 200.0f);
    ExpectSpeed("noclip crouch stays retail", game.CrouchSpeed(), 80.0f);

    game.Reset();
    game.SetByte(kPlayerSpectatingOffset, 1);
    game.Tick();
    ExpectSpeed("spectating stays retail", game.WalkSpeed(), 450.0f);

    game.SetMultiplayer(true);
    game.Reset();
    game.Tick();
    ExpectSpeed("multiplayer walk stays retail", game.WalkSpeed(), 180.0f);
    ExpectSpeed("multiplayer crouch stays retail", game.CrouchSpeed(), 80.0f);
    game.Reset();
    game.Press(kButtonRun, 127);
    game.SetStamina(10.0f);
    game.Tick();
    ExpectSpeed("multiplayer run stays retail", game.WalkSpeed(), 220.0f);
    game.SetMultiplayer(false);
}

struct Harness {
    std::filesystem::path directory;
    HMODULE plugin = nullptr;
    IsInstalledFn isInstalled = nullptr;

    bool LoadPlugin(const std::filesystem::path& asi, const std::string& ini) {
        std::filesystem::copy_file(
            asi, directory / L"PreySpeedBuff.asi",
            std::filesystem::copy_options::overwrite_existing);
        std::ofstream(directory / L"PreySpeedBuff.ini", std::ios::binary) << ini;
        plugin = LoadLibraryW((directory / L"PreySpeedBuff.asi").c_str());
        if (plugin == nullptr) {
            Fail("plugin did not load");
            return false;
        }
        const auto getState = reinterpret_cast<GetStateFn>(reinterpret_cast<void*>(
            GetProcAddress(plugin, "PreySpeedBuffGetInitializationState")));
        isInstalled = reinterpret_cast<IsInstalledFn>(reinterpret_cast<void*>(
            GetProcAddress(plugin, "PreySpeedBuffIsHookInstalled")));
        if (getState == nullptr || isInstalled == nullptr) {
            Fail("plugin exports are missing");
            return false;
        }
        for (int attempt = 0; attempt < 500 && getState() == 0; ++attempt) Sleep(10);
        if (getState() == 0) {
            Fail("plugin did not finish initializing");
            return false;
        }
        return true;
    }

    std::string ReadLog() const {
        // The plugin keeps the log open with shared access.
        std::ifstream file(directory / L"PreySpeedBuff.log", std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), {});
    }

    void ExpectLog(const char* text) const {
        if (ReadLog().find(text) == std::string::npos) {
            std::printf("FAIL log does not contain \"%s\":\n%s\n", text,
                        ReadLog().c_str());
            ++g_failures;
        }
    }
};

const char kOverrideIni[] = "[speed]\nwalk = 300\nrun = 500\ncrouch = 100\n";

std::uint8_t* LoadGame(const std::filesystem::path& path) {
    auto* module = reinterpret_cast<std::uint8_t*>(LoadLibraryW(path.c_str()));
    if (module == nullptr) Fail("gamex86.dll did not load");
    return module;
}

bool FirstByteIsJump(const std::uint8_t* module) {
    return module[kAdjustSpeedRva] == 0xe9;
}

// Copies the DLL with one AdjustSpeed byte changed, as a different build would.
bool WriteAlteredGameDll(const std::filesystem::path& source,
                         const std::filesystem::path& destination) {
    std::ifstream input(source, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    const auto* nt =
        reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        const DWORD begin = section[index].VirtualAddress;
        if (kAdjustSpeedRva >= begin &&
            kAdjustSpeedRva < begin + section[index].SizeOfRawData) {
            const std::size_t offset =
                kAdjustSpeedRva + 0x40 - begin + section[index].PointerToRawData;
            bytes[offset] = static_cast<char>(bytes[offset] ^ 0xff);
            std::ofstream output(destination, std::ios::binary);
            output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            return output.good();
        }
    }
    return false;
}

int Run(const std::filesystem::path& asi, const std::filesystem::path& gameDll,
        const std::string& scenario) {
    Harness harness;
    harness.directory = std::filesystem::current_path() / ("retail-" + scenario);
    std::filesystem::remove_all(harness.directory);
    std::filesystem::create_directories(harness.directory);

    if (scenario == "override") {
        if (!harness.LoadPlugin(asi, kOverrideIni)) return 1;
        harness.ExpectLog("waiting for gamex86.dll");
        std::uint8_t* module = LoadGame(gameDll);
        if (module == nullptr) return 1;
        if (!harness.isInstalled()) Fail("hook was not installed");
        CheckOverrideBehavior(module);
        harness.ExpectLog("first override applied; walk=300.00 run=500.00 crouch=100.00");
    } else if (scenario == "relocated") {
        // Occupying the preferred base forces the loader to relocate the image.
        void* blocker = VirtualAlloc(reinterpret_cast<void*>(kPreferredImageBase),
                                     0x10000, MEM_RESERVE, PAGE_NOACCESS);
        if (blocker == nullptr) Fail("could not reserve the preferred image base");
        if (!harness.LoadPlugin(asi, kOverrideIni)) return 1;
        std::uint8_t* module = LoadGame(gameDll);
        if (module == nullptr) return 1;
        if (reinterpret_cast<std::uintptr_t>(module) == kPreferredImageBase) {
            Fail("image was not relocated");
        }
        if (!harness.isInstalled()) Fail("hook was not installed on a relocated image");
        CheckOverrideBehavior(module);
    } else if (scenario == "late") {
        std::uint8_t* module = LoadGame(gameDll);
        if (module == nullptr) return 1;
        CheckRetailBehavior(module);
        if (!harness.LoadPlugin(asi, kOverrideIni)) return 1;
        if (!harness.isInstalled()) Fail("hook was not installed on a loaded module");
        CheckOverrideBehavior(module);
    } else if (scenario == "partial") {
        if (!harness.LoadPlugin(asi, "[speed]\nwalk = game\nrun = 500\ncrouch = game\n")) {
            return 1;
        }
        std::uint8_t* module = LoadGame(gameDll);
        if (module == nullptr) return 1;
        FakeGame game(module);
        game.Reset();
        game.Tick();
        ExpectSpeed("walk follows the cvar", game.WalkSpeed(), 180.0f);
        ExpectSpeed("crouch follows the cvar", game.CrouchSpeed(), 80.0f);
        game.SetCvar(kWalkSpeedCvarRva, 150.0f);
        game.Reset();
        game.Tick();
        ExpectSpeed("walk follows a changed cvar", game.WalkSpeed(), 150.0f);
        game.Reset();
        game.Press(kButtonRun, 127);
        game.SetStamina(10.0f);
        game.Tick();
        ExpectSpeed("run is overridden", game.WalkSpeed(), 500.0f);
    } else if (scenario == "disabled") {
        if (!harness.LoadPlugin(asi, "[speed]\nenabled = false\nwalk = 300\n")) return 1;
        std::uint8_t* module = LoadGame(gameDll);
        if (module == nullptr) return 1;
        if (harness.isInstalled() || FirstByteIsJump(module)) Fail("hook is present");
        CheckRetailBehavior(module);
        harness.ExpectLog("enabled=false");
    } else if (scenario == "allgame") {
        if (!harness.LoadPlugin(asi, "[speed]\nwalk = game\n")) return 1;
        std::uint8_t* module = LoadGame(gameDll);
        if (module == nullptr) return 1;
        if (harness.isInstalled() || FirstByteIsJump(module)) Fail("hook is present");
        CheckRetailBehavior(module);
        harness.ExpectLog("nothing to override");
    } else if (scenario == "invalid") {
        if (!harness.LoadPlugin(asi, "[speed]\nwalk = fast\n")) return 1;
        std::uint8_t* module = LoadGame(gameDll);
        if (module == nullptr) return 1;
        if (harness.isInstalled() || FirstByteIsJump(module)) Fail("hook is present");
        CheckRetailBehavior(module);
        harness.ExpectLog("walk must be game or a number");
    } else if (scenario == "altered") {
        const std::filesystem::path altered = harness.directory / L"gamex86.dll";
        if (!WriteAlteredGameDll(gameDll, altered)) {
            Fail("could not create the altered DLL");
            return 1;
        }
        if (!harness.LoadPlugin(asi, kOverrideIni)) return 1;
        std::uint8_t* module = LoadGame(altered);
        if (module == nullptr) return 1;
        if (harness.isInstalled() || FirstByteIsJump(module)) Fail("hook is present");
        harness.ExpectLog("differs from retail");
    } else if (scenario == "reload") {
        if (!harness.LoadPlugin(asi, kOverrideIni)) return 1;
        HMODULE first = LoadLibraryW(gameDll.c_str());
        if (first == nullptr) {
            Fail("gamex86.dll did not load");
            return 1;
        }
        if (!harness.isInstalled()) Fail("hook was not installed");
        FreeLibrary(first);
        if (GetModuleHandleW(L"gamex86.dll") != nullptr) Fail("gamex86.dll stayed loaded");
        if (harness.isInstalled()) Fail("hook state survived the unload");
        std::uint8_t* module = LoadGame(gameDll);
        if (module == nullptr) return 1;
        if (!harness.isInstalled()) Fail("hook was not reinstalled after a reload");
        CheckOverrideBehavior(module);
        harness.ExpectLog("gamex86.dll unloaded");
    } else if (scenario == "unload") {
        if (!harness.LoadPlugin(asi, kOverrideIni)) return 1;
        std::uint8_t* module = LoadGame(gameDll);
        if (module == nullptr) return 1;
        CheckOverrideBehavior(module);
        FreeLibrary(harness.plugin);
        // The bootstrap thread drops its reference as it exits.
        for (int attempt = 0; attempt < 500; ++attempt) {
            if (GetModuleHandleW(L"PreySpeedBuff.asi") == nullptr) break;
            Sleep(10);
        }
        if (GetModuleHandleW(L"PreySpeedBuff.asi") != nullptr) Fail("plugin stayed loaded");
        if (std::memcmp(module + kAdjustSpeedRva, kAdjustSpeedBytes.data(),
                        kAdjustSpeedStolenBytes) != 0) {
            Fail("AdjustSpeed bytes were not restored");
        }
        CheckRetailBehavior(module);
    } else {
        std::printf("unknown scenario %s\n", scenario.c_str());
        return 2;
    }

    if (g_failures != 0) {
        std::printf("--- plugin log ---\n%s\n", harness.ReadLog().c_str());
        return 1;
    }
    std::printf("scenario %s passed\n", scenario.c_str());
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 4) {
        std::printf("usage: PreySpeedBuffRetailTest <asi> <gamex86.dll> <scenario>\n");
        return 2;
    }
    const std::filesystem::path gameDll = argv[2];
    if (!std::filesystem::exists(gameDll)) {
        std::printf("skipped: retail gamex86.dll not available at this path\n");
        return kSkipped;
    }
    return Run(argv[1], gameDll, std::filesystem::path(argv[3]).string());
}
