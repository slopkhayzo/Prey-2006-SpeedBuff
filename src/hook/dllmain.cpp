#include <windows.h>
#include <winternl.h>

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <string>

#include "config.h"
#include "game_layout.h"
#include "preyspeedbuff_version.h"

namespace {

using namespace preyspeedbuff;

using AdjustSpeedFn = void(__thiscall*)(std::uint8_t* player);
using SetSpeedFn = void(__thiscall*)(std::uint8_t* physics, float walk, float crouch);

// The loaded and unloaded notification payloads share this layout.
struct LdrDllNotificationData {
    ULONG flags;
    const UNICODE_STRING* fullDllName;
    const UNICODE_STRING* baseDllName;
    PVOID dllBase;
    ULONG sizeOfImage;
};
using LdrDllNotificationFn = VOID(CALLBACK*)(ULONG reason,
                                             const LdrDllNotificationData* data,
                                             PVOID context);
using LdrRegisterDllNotificationFn = LONG(NTAPI*)(ULONG flags,
                                                  LdrDllNotificationFn callback,
                                                  PVOID context, PVOID* cookie);
using LdrUnregisterDllNotificationFn = LONG(NTAPI*)(PVOID cookie);

constexpr ULONG kLdrDllNotificationLoaded = 1;
constexpr ULONG kLdrDllNotificationUnloaded = 2;
constexpr std::size_t kTrampolineSize = kAdjustSpeedStolenBytes + 5;

HMODULE g_pluginModule = nullptr;
HANDLE g_log = INVALID_HANDLE_VALUE;
SRWLOCK g_logLock = SRWLOCK_INIT;

// Written before registration and read-only afterwards.
Config g_config;

// Serializes installation between the bootstrap thread and the loader callback.
// Never held across a call that can take the loader lock.
SRWLOCK g_hookLock = SRWLOCK_INIT;
PVOID g_notificationCookie = nullptr;
LdrUnregisterDllNotificationFn g_unregisterNotification = nullptr;
std::uint8_t* g_hookedModule = nullptr;
std::uint8_t* g_trampoline = nullptr;
std::array<std::uint8_t, kAdjustSpeedStolenBytes> g_originalBytes{};

// Valid only while the hook is installed.
AdjustSpeedFn g_originalAdjustSpeed = nullptr;
SetSpeedFn g_setSpeed = nullptr;
const std::uint8_t* g_gameModule = nullptr;

std::atomic<DWORD> g_initializationState{0};
std::atomic<bool> g_loggedFirstOverride{false};

void Log(const char* format, ...) {
    if (g_log == INVALID_HANDLE_VALUE) return;
    char line[512]{};
    va_list arguments;
    va_start(arguments, format);
    const int length = _vsnprintf_s(line, sizeof(line), _TRUNCATE, format, arguments);
    va_end(arguments);
    if (length <= 0) return;
    AcquireSRWLockExclusive(&g_logLock);
    DWORD written = 0;
    WriteFile(g_log, line, static_cast<DWORD>(length), &written, nullptr);
    ReleaseSRWLockExclusive(&g_logLock);
}

float ReadCvarFloat(std::uintptr_t cvarRva) {
    const std::uint8_t* internal = nullptr;
    std::memcpy(&internal, g_gameModule + cvarRva + kCvarInternalOffset,
                sizeof(internal));
    float value = 0.0f;
    std::memcpy(&value, internal + kCvarFloatOffset, sizeof(value));
    return value;
}

// Lets retail AdjustSpeed run in full, then replaces only the result it handed
// to SetSpeed. Stamina, view bob, and the pm_* cvars stay retail, so nothing
// carries over into saves, the console, or a later multiplayer session.
void __fastcall HookedAdjustSpeed(std::uint8_t* player, void* /*edx*/) {
    g_originalAdjustSpeed(player);

    if (g_gameModule[kIsMultiplayerRva] != 0 ||
        player[kPlayerSpectatingOffset] != 0 ||
        player[kPlayerNoclipOffset] != 0) {
        return;
    }

    float runFraction = 0.0f;
    std::memcpy(&runFraction, player + kPlayerRunFractionOffset, sizeof(runFraction));
    const float walk = g_config.walk.has_value() ? *g_config.walk
                                                 : ReadCvarFloat(kWalkSpeedCvarRva);
    const float run = g_config.run.has_value() ? *g_config.run
                                               : ReadCvarFloat(kRunSpeedCvarRva);
    const float crouch = g_config.crouch.has_value()
                             ? *g_config.crouch
                             : ReadCvarFloat(kCrouchSpeedCvarRva);
    const float speed = walk * (1.0f - runFraction) + run * runFraction;
    g_setSpeed(player + kPlayerPhysicsOffset, speed, crouch);

    if (!g_loggedFirstOverride.exchange(true, std::memory_order_relaxed)) {
        Log("speed: first override applied; walk=%.2f run=%.2f crouch=%.2f\r\n",
            walk, run, crouch);
    }
}

void EncodeRelativeJump(std::uint8_t* at, const std::uint8_t* origin,
                        const void* destination) {
    const auto relative = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(destination) -
        reinterpret_cast<std::intptr_t>(origin + 5));
    at[0] = 0xe9;
    std::memcpy(at + 1, &relative, sizeof(relative));
}

bool WriteCode(std::uint8_t* target, const std::uint8_t* bytes, std::size_t size) {
    DWORD oldProtection = 0;
    if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &oldProtection)) {
        return false;
    }
    std::memcpy(target, bytes, size);
    FlushInstructionCache(GetCurrentProcess(), target, size);
    DWORD ignored = 0;
    VirtualProtect(target, size, oldProtection, &ignored);
    return std::memcmp(target, bytes, size) == 0;
}

// Runs under the loader lock when called from the DLL notification, which is
// why this uses only kernel32 services and leaves the module list alone.
void InstallHookLocked(std::uint8_t* module) {
    if (g_hookedModule != nullptr) return;

    const char* const rejection = ValidateGameModule(module);
    if (rejection != nullptr) {
        Log("error: gamex86.dll rejected at %p: %s; speed override inactive\r\n",
            module, rejection);
        return;
    }

    auto* trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, kTrampolineSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) {
        Log("error: could not allocate the AdjustSpeed trampoline\r\n");
        return;
    }

    auto* target = module + kAdjustSpeedRva;
    std::memcpy(g_originalBytes.data(), target, g_originalBytes.size());
    std::memcpy(trampoline, target, kAdjustSpeedStolenBytes);
    EncodeRelativeJump(trampoline + kAdjustSpeedStolenBytes,
                       trampoline + kAdjustSpeedStolenBytes,
                       target + kAdjustSpeedStolenBytes);

    g_gameModule = module;
    g_setSpeed = reinterpret_cast<SetSpeedFn>(module + kSetSpeedRva);
    g_originalAdjustSpeed = reinterpret_cast<AdjustSpeedFn>(trampoline);

    std::array<std::uint8_t, kAdjustSpeedStolenBytes> detour{};
    detour.fill(0x90);
    EncodeRelativeJump(detour.data(), target,
                       reinterpret_cast<const void*>(&HookedAdjustSpeed));
    if (!WriteCode(target, detour.data(), detour.size())) {
        WriteCode(target, g_originalBytes.data(), g_originalBytes.size());
        VirtualFree(trampoline, 0, MEM_RELEASE);
        Log("error: AdjustSpeed detour could not be written\r\n");
        return;
    }

    g_trampoline = trampoline;
    g_hookedModule = module;
    Log("speed: hhPlayer::AdjustSpeed hook installed; gamex86.dll at %p\r\n", module);
}

void ForgetHookLocked() {
    VirtualFree(g_trampoline, 0, MEM_RELEASE);
    g_trampoline = nullptr;
    g_hookedModule = nullptr;
    g_originalAdjustSpeed = nullptr;
    g_setSpeed = nullptr;
    g_gameModule = nullptr;
}

void RestoreHookLocked() {
    if (g_hookedModule == nullptr) return;
    auto* target = g_hookedModule + kAdjustSpeedRva;
    std::array<std::uint8_t, kAdjustSpeedStolenBytes> detour{};
    detour.fill(0x90);
    EncodeRelativeJump(detour.data(), target,
                       reinterpret_cast<const void*>(&HookedAdjustSpeed));
    if (std::memcmp(target, detour.data(), detour.size()) != 0) {
        // Something hooked over us, and restoring would silently remove it.
        // Only a test ever unloads the plugin; the game keeps ASIs until exit.
        Log("warning: AdjustSpeed was re-hooked; leaving it in place\r\n");
        return;
    }
    WriteCode(target, g_originalBytes.data(), g_originalBytes.size());
    ForgetHookLocked();
}

bool IsGameModuleName(const UNICODE_STRING* name) {
    static constexpr wchar_t kName[] = L"gamex86.dll";
    constexpr USHORT kLength = static_cast<USHORT>(std::size(kName) - 1);
    if (name == nullptr || name->Length != kLength * sizeof(wchar_t)) return false;
    for (USHORT index = 0; index < kLength; ++index) {
        wchar_t character = name->Buffer[index];
        if (character >= L'A' && character <= L'Z') character += L'a' - L'A';
        if (character != kName[index]) return false;
    }
    return true;
}

// The engine can unload and reload gamex86.dll when the file system restarts, so
// this tracks every load instead of patching once.
VOID CALLBACK OnDllNotification(ULONG reason, const LdrDllNotificationData* data,
                                PVOID /*context*/) {
    if (!IsGameModuleName(data->baseDllName)) return;
    auto* module = static_cast<std::uint8_t*>(data->dllBase);
    AcquireSRWLockExclusive(&g_hookLock);
    if (reason == kLdrDllNotificationLoaded) {
        InstallHookLocked(module);
    } else if (reason == kLdrDllNotificationUnloaded && g_hookedModule == module) {
        ForgetHookLocked();
        Log("speed: gamex86.dll unloaded\r\n");
    }
    ReleaseSRWLockExclusive(&g_hookLock);
}

std::filesystem::path PluginDirectory() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(
            g_pluginModule, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) return {};
        if (length < path.size()) {
            path.resize(length);
            return std::filesystem::path(path).parent_path();
        }
        path.resize(path.size() * 2);
    }
}

void DescribeSpeed(const char* name, const std::optional<float>& speed) {
    if (speed.has_value()) {
        Log("config: %s=%.2f\r\n", name, *speed);
    } else {
        Log("config: %s=game\r\n", name);
    }
}

// Returns the final initialization state: 1 when active, 2 when inert.
DWORD Initialize() {
    const std::filesystem::path directory = PluginDirectory();
    if (directory.empty()) return 2;

    g_log = CreateFileW((directory / L"PreySpeedBuff.log").c_str(), FILE_APPEND_DATA,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    Log("PreySpeedBuff %s starting\r\n", PREYSPEEDBUFF_VERSION);

    std::string error;
    if (!LoadConfig(directory / L"PreySpeedBuff.ini", g_config, error)) {
        Log("error: %s; speed override inactive\r\n", error.c_str());
        return 2;
    }
    DescribeSpeed("walk", g_config.walk);
    DescribeSpeed("run", g_config.run);
    DescribeSpeed("crouch", g_config.crouch);
    if (!g_config.enabled) {
        Log("config: enabled=false; speed override inactive\r\n");
        return 2;
    }
    if (!g_config.HasOverride()) {
        Log("config: every speed is game; nothing to override\r\n");
        return 2;
    }

    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto registerNotification = reinterpret_cast<LdrRegisterDllNotificationFn>(
        reinterpret_cast<void*>(GetProcAddress(ntdll, "LdrRegisterDllNotification")));
    g_unregisterNotification = reinterpret_cast<LdrUnregisterDllNotificationFn>(
        reinterpret_cast<void*>(GetProcAddress(ntdll, "LdrUnregisterDllNotification")));
    if (registerNotification == nullptr || g_unregisterNotification == nullptr ||
        registerNotification(0, &OnDllNotification, nullptr, &g_notificationCookie) != 0) {
        Log("error: could not register for DLL notifications; "
            "speed override inactive\r\n");
        return 2;
    }

    // Covers a loader that ran us after the engine loaded the game module. This
    // lookup stays outside g_hookLock, which the loader callback also takes.
    auto* loaded = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(L"gamex86.dll"));
    if (loaded != nullptr) {
        AcquireSRWLockExclusive(&g_hookLock);
        InstallHookLocked(loaded);
        ReleaseSRWLockExclusive(&g_hookLock);
    } else {
        Log("speed: waiting for gamex86.dll\r\n");
    }
    return 1;
}

DWORD WINAPI BootstrapAsi(LPVOID) {
    g_initializationState.store(Initialize(), std::memory_order_release);
    FreeLibraryAndExitThread(g_pluginModule, 0);
}

} // namespace

extern "C" __declspec(dllexport) DWORD PreySpeedBuffGetInitializationState() {
    return g_initializationState.load(std::memory_order_acquire);
}

extern "C" __declspec(dllexport) BOOL PreySpeedBuffIsHookInstalled() {
    AcquireSRWLockShared(&g_hookLock);
    const bool installed = g_hookedModule != nullptr;
    ReleaseSRWLockShared(&g_hookLock);
    return installed ? TRUE : FALSE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        g_pluginModule = instance;
        // File I/O and hook setup wait for the bootstrap thread, which holds its
        // own module reference so an early FreeLibrary cannot unmap it.
        HMODULE reference = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                reinterpret_cast<LPCWSTR>(&BootstrapAsi),
                                &reference)) {
            return FALSE;
        }
        const HANDLE bootstrap =
            CreateThread(nullptr, 0, &BootstrapAsi, nullptr, 0, nullptr);
        if (bootstrap == nullptr) {
            FreeLibrary(reference);
            return FALSE;
        }
        CloseHandle(bootstrap);
        return TRUE;
    }
    if (reason == DLL_PROCESS_DETACH && reserved == nullptr) {
        if (g_notificationCookie != nullptr) {
            g_unregisterNotification(g_notificationCookie);
        }
        AcquireSRWLockExclusive(&g_hookLock);
        RestoreHookLocked();
        ReleaseSRWLockExclusive(&g_hookLock);
        if (g_log != INVALID_HANDLE_VALUE) CloseHandle(g_log);
    }
    return TRUE;
}
