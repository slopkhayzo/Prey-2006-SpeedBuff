#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace preyspeedbuff {

// These RVAs describe the retail Prey 1.4 base/gamex86.dll. A different build is
// accepted only if ValidateGameModule proves every one still has the same role.
inline constexpr std::uintptr_t kPreferredImageBase = 0x10000000;
inline constexpr std::uintptr_t kAdjustSpeedRva = 0x00081490;
inline constexpr std::uintptr_t kSetSpeedRva = 0x0012a960;
inline constexpr std::uintptr_t kIsMultiplayerRva = 0x00730c44;
inline constexpr std::uintptr_t kCrouchSpeedCvarRva = 0x0038316c;
inline constexpr std::uintptr_t kWalkSpeedCvarRva = 0x003831a0;
inline constexpr std::uintptr_t kRunSpeedCvarRva = 0x003831d4;

// idCVar layout, as read by the retail code.
inline constexpr std::size_t kCvarNameOffset = 0x04;
inline constexpr std::size_t kCvarFloatOffset = 0x28;
inline constexpr std::size_t kCvarInternalOffset = 0x2c;

// hhPlayer fields read by AdjustSpeed.
inline constexpr std::size_t kPlayerNoclipOffset = 0x15b8;
inline constexpr std::size_t kPlayerSpectatingOffset = 0x17e7;
inline constexpr std::size_t kPlayerPhysicsOffset = 0x18a8;
inline constexpr std::size_t kPlayerRunFractionOffset = 0x1ca8;

// push ecx; push esi; mov esi, ecx; cmp byte ptr [esi+17e7h], 0. The following
// je is relative, so the detour stops on this instruction boundary.
inline constexpr std::size_t kAdjustSpeedStolenBytes = 11;

#include "adjust_speed_signature.inc"

// Returns nullptr if the mapped image has the retail layout, otherwise a short
// reason. Only reads the image and makes no CRT or module calls, so it is safe
// to run from a DLL notification with the loader lock held.
const char* ValidateGameModule(const std::uint8_t* module);

} // namespace preyspeedbuff
