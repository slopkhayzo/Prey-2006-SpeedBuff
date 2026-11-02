#include "config.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <set>

namespace preyspeedbuff {
namespace {

std::string Trim(std::string value) {
    const auto isSpace = [](unsigned char character) {
        return character == ' ' || character == '\t' || character == '\r' ||
               character == '\n';
    };
    const auto first = std::find_if_not(value.begin(), value.end(), isSpace);
    const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
    if (first >= last) return {};
    return std::string(first, last);
}

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
        if (character >= 'A' && character <= 'Z') {
            return static_cast<char>(character - 'A' + 'a');
        }
        return static_cast<char>(character);
    });
    return value;
}

std::optional<bool> ParseBoolean(const std::string& value) {
    const std::string normalized = LowerAscii(value);
    if (normalized == "true" || normalized == "yes" || normalized == "1" ||
        normalized == "on") {
        return true;
    }
    if (normalized == "false" || normalized == "no" || normalized == "0" ||
        normalized == "off") {
        return false;
    }
    return std::nullopt;
}

bool ParseSpeed(const std::string& value, std::optional<float>& speed) {
    if (LowerAscii(value) == "game") {
        speed.reset();
        return true;
    }
    float parsed = 0.0f;
    const char* const end = value.data() + value.size();
    const auto [next, status] = std::from_chars(value.data(), end, parsed);
    if (status != std::errc{} || next != end || !std::isfinite(parsed) ||
        parsed < 0.0f || parsed > kMaximumSpeed) {
        return false;
    }
    speed = parsed;
    return true;
}

} // namespace

bool LoadConfig(const std::filesystem::path& path, Config& config,
                std::string& error) {
    std::ifstream file(path);
    if (!file) {
        error = "could not open configuration file: " + path.string();
        return false;
    }

    Config loaded;
    std::set<std::string> seen;
    bool inSection = false;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        const std::string where = " at line " + std::to_string(lineNumber);
        if (lineNumber == 1 && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xef &&
            static_cast<unsigned char>(line[1]) == 0xbb &&
            static_cast<unsigned char>(line[2]) == 0xbf) {
            line.erase(0, 3);
        }
        line = Trim(line);
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            const std::string section =
                LowerAscii(Trim(line.substr(1, line.size() - 2)));
            if (section != "speed") {
                error = "unknown section [" + section + "]" + where;
                return false;
            }
            inSection = true;
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos || !inSection) {
            error = "expected key=value inside [speed]" + where;
            return false;
        }
        const std::string key = LowerAscii(Trim(line.substr(0, separator)));
        const std::string value = Trim(line.substr(separator + 1));
        if (!seen.insert(key).second) {
            error = "duplicate key " + key + where;
            return false;
        }
        if (key == "enabled") {
            const std::optional<bool> enabled = ParseBoolean(value);
            if (!enabled.has_value()) {
                error = "enabled must be true or false" + where;
                return false;
            }
            loaded.enabled = *enabled;
        } else if (key == "walk" || key == "run" || key == "crouch") {
            std::optional<float>& speed = key == "walk"  ? loaded.walk
                                          : key == "run" ? loaded.run
                                                         : loaded.crouch;
            if (!ParseSpeed(value, speed)) {
                error = key + " must be game or a number from 0 to 100000" + where;
                return false;
            }
        } else {
            error = "unknown key " + key + where;
            return false;
        }
    }
    if (!file.eof()) {
        error = "failed while reading configuration file: " + path.string();
        return false;
    }

    config = loaded;
    return true;
}

} // namespace preyspeedbuff
