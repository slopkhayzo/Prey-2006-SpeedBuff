#include "game_layout.h"

#include <windows.h>

#include <cstring>

namespace preyspeedbuff {
namespace {

const IMAGE_NT_HEADERS32* ReadNtHeaders(const std::uint8_t* module) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    const auto* nt =
        reinterpret_cast<const IMAGE_NT_HEADERS32*>(module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        return nullptr;
    }
    return nt;
}

bool RangeHasSectionFlag(const IMAGE_NT_HEADERS32* nt, std::uintptr_t rva,
                         std::size_t size, DWORD flag) {
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        const std::uintptr_t begin = section[index].VirtualAddress;
        const std::uintptr_t end = begin + section[index].Misc.VirtualSize;
        if (rva >= begin && rva + size <= end) {
            return (section[index].Characteristics & flag) != 0;
        }
    }
    return false;
}

// Compares a code body against its file-relative bytes. Undoing the relocation
// delta instead of masking those operands also proves that each absolute
// reference still names the same RVA: the cvars, isMultiplayer, and constants.
template <std::size_t Size, std::size_t RelocationCount>
bool MatchesRetailBody(const std::uint8_t* live,
                       const std::array<std::uint8_t, Size>& expected,
                       const std::array<std::uint16_t, RelocationCount>& relocations,
                       std::uint32_t delta) {
    std::array<std::uint8_t, Size> normalized{};
    std::memcpy(normalized.data(), live, Size);
    for (const std::uint16_t offset : relocations) {
        std::uint32_t value = 0;
        std::memcpy(&value, normalized.data() + offset, sizeof(value));
        value -= delta;
        std::memcpy(normalized.data() + offset, &value, sizeof(value));
    }
    return std::memcmp(normalized.data(), expected.data(), Size) == 0;
}

bool CvarHasName(const std::uint8_t* module, std::uintptr_t imageSize,
                 std::uintptr_t cvarRva, const char* name) {
    std::uint32_t namePointer = 0;
    std::memcpy(&namePointer, module + cvarRva + kCvarNameOffset,
                sizeof(namePointer));
    const auto imageBase = reinterpret_cast<std::uintptr_t>(module);
    const std::size_t nameSize = std::strlen(name) + 1;
    if (namePointer < imageBase || namePointer + nameSize > imageBase + imageSize) {
        return false;
    }
    return std::memcmp(reinterpret_cast<const char*>(namePointer), name,
                       nameSize) == 0;
}

} // namespace

const char* ValidateGameModule(const std::uint8_t* module) {
    const IMAGE_NT_HEADERS32* nt = ReadNtHeaders(module);
    if (nt == nullptr) return "not an x86 PE32 image";

    const std::uintptr_t imageSize = nt->OptionalHeader.SizeOfImage;
    if (kIsMultiplayerRva >= imageSize) return "image is smaller than the retail layout";

    if (!RangeHasSectionFlag(nt, kAdjustSpeedRva, kAdjustSpeedBytes.size(),
                             IMAGE_SCN_MEM_EXECUTE) ||
        !RangeHasSectionFlag(nt, kSetSpeedRva, kSetSpeedBytes.size(),
                             IMAGE_SCN_MEM_EXECUTE)) {
        return "hook targets are not in executable sections";
    }
    if (!RangeHasSectionFlag(nt, kIsMultiplayerRva, 1, IMAGE_SCN_MEM_WRITE) ||
        !RangeHasSectionFlag(nt, kCrouchSpeedCvarRva,
                             kRunSpeedCvarRva + 0x34 - kCrouchSpeedCvarRva,
                             IMAGE_SCN_MEM_WRITE)) {
        return "game globals are not in writable sections";
    }

    const auto delta = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(module) - kPreferredImageBase);
    if (!MatchesRetailBody(module + kAdjustSpeedRva, kAdjustSpeedBytes,
                           kAdjustSpeedRelocations, delta)) {
        return "hhPlayer::AdjustSpeed differs from retail "
               "(unsupported build, or another mod already hooked it)";
    }
    if (std::memcmp(module + kSetSpeedRva, kSetSpeedBytes.data(),
                    kSetSpeedBytes.size()) != 0) {
        return "idPhysics_Player::SetSpeed differs from retail";
    }

    if (!CvarHasName(module, imageSize, kWalkSpeedCvarRva, "pm_walkspeed") ||
        !CvarHasName(module, imageSize, kRunSpeedCvarRva, "pm_runspeed") ||
        !CvarHasName(module, imageSize, kCrouchSpeedCvarRva, "pm_crouchspeed")) {
        return "speed cvar objects are not at their retail locations";
    }
    return nullptr;
}

} // namespace preyspeedbuff
