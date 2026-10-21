#include "coop/config.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>

namespace coop {
namespace {

std::string trim(const std::string& s) {
    size_t a = 0;
    size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool parseInt(const std::string& v, long long& out) {
    if (v.empty()) return false;
    errno = 0;
    char* end = nullptr;
    out = std::strtoll(v.c_str(), &end, 10);
    return errno == 0 && end != nullptr && *end == '\0';
}

bool parseFloat(const std::string& v, float& out) {
    if (v.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const double d = std::strtod(v.c_str(), &end);
    if (errno != 0 || end == nullptr || *end != '\0' || !std::isfinite(d)) return false;
    out = static_cast<float>(d);
    return true;
}

bool parseBool(const std::string& v, bool& out) {
    const std::string l = lower(v);
    if (l == "true" || l == "1" || l == "yes" || l == "on") {
        out = true;
        return true;
    }
    if (l == "false" || l == "0" || l == "no" || l == "off") {
        out = false;
        return true;
    }
    return false;
}

using Setter = std::function<bool(const std::string&, std::string&)>;  // value, error -> ok

template <typename T>
Setter intSetter(T& target, long long lo, long long hi) {
    return [&target, lo, hi](const std::string& v, std::string& err) {
        long long n;
        if (!parseInt(v, n)) {
            err = "expected a whole number";
            return false;
        }
        if (n < lo || n > hi) {
            err = "must be between " + std::to_string(lo) + " and " + std::to_string(hi);
            return false;
        }
        target = static_cast<T>(n);
        return true;
    };
}

Setter floatSetter(float& target, float lo, float hi) {
    return [&target, lo, hi](const std::string& v, std::string& err) {
        float f;
        if (!parseFloat(v, f)) {
            err = "expected a number";
            return false;
        }
        if (f < lo || f > hi) {
            std::ostringstream os;
            os << "must be between " << lo << " and " << hi;
            err = os.str();
            return false;
        }
        target = f;
        return true;
    };
}

Setter boolSetter(bool& target) {
    return [&target](const std::string& v, std::string& err) {
        if (!parseBool(v, target)) {
            err = "expected true or false";
            return false;
        }
        return true;
    };
}

Setter textSetter(std::string& target, size_t maxLen) {
    return [&target, maxLen](const std::string& v, std::string& err) {
        if (v.size() > maxLen) {
            err = "longer than " + std::to_string(maxLen) + " characters";
            return false;
        }
        target = v;
        return true;
    };
}

Setter modeSetter(MovementMode& target) {
    return [&target](const std::string& v, std::string& err) {
        const std::string l = lower(v);
        if (l == "hybrid") {
            target = MovementMode::Hybrid;
        } else if (l == "teleport") {
            target = MovementMode::Teleport;
        } else {
            err = "expected Hybrid or Teleport";
            return false;
        }
        return true;
    };
}

Setter mountedTaskSetter(MountedTask& target) {
    return [&target](const std::string& v, std::string& err) {
        const std::string l = lower(v);
        if (l == "rider") {
            target = MountedTask::Rider;
        } else if (l == "horse") {
            target = MountedTask::Horse;
        } else if (l == "anymeans") {
            target = MountedTask::AnyMeans;
        } else {
            err = "expected AnyMeans, Horse or Rider";
            return false;
        }
        return true;
    };
}

Setter keySetter(int& target) {
    return [&target](const std::string& v, std::string& err) {
        const int vk = parseKeyName(v);
        if (vk < 0) {
            err = "unknown key name (use F1-F24, A-Z, 0-9, NUMPAD0-9, INSERT, DELETE, HOME, END, PAGEUP, PAGEDOWN or NONE)";
            return false;
        }
        target = vk;
        return true;
    };
}

std::map<std::string, Setter> buildSetters(Config& c) {
    std::map<std::string, Setter> s;
    s["network.hostport"] = intSetter(c.hostPort, 1, 65535);
    s["network.joinaddress"] = textSetter(c.joinAddress, 253);
    s["network.joinport"] = intSetter(c.joinPort, 1, 65535);
    s["network.maxplayers"] = intSetter(c.maxPlayers, 2, 4);
    s["network.sendratehz"] = intSetter(c.sendRateHz, 5, 60);

    s["player.name"] = textSetter(c.name, 24);
    s["player.model"] = textSetter(c.model, 48);
    s["player.outfit"] = intSetter(c.outfit, -1, 999);
    s["player.fallbackhorse"] = textSetter(c.fallbackHorse, 48);

    s["sync.interpdelayms"] = intSetter(c.interpDelayMs, 0, 1000);
    s["sync.maxextrapolationms"] = intSetter(c.maxExtrapolationMs, 0, 2000);
    s["sync.teleportdistance"] = floatSetter(c.teleportDistance, 2.f, 500.f);
    s["sync.onfootmovement"] = modeSetter(c.onFootMovement);
    s["sync.mountedmovement"] = modeSetter(c.mountedMovement);
    s["sync.clocksync"] = boolSetter(c.clockSync);
    s["sync.teleportonjoin"] = boolSetter(c.teleportOnJoin);

    s["tuning.correctionstart"] = floatSetter(c.correctionStart, 0.f, 50.f);
    s["tuning.correctiongain"] = floatSetter(c.correctionGain, 0.f, 20.f);
    s["tuning.retaskms"] = intSetter(c.retaskMs, 50, 5000);
    s["tuning.lookaheadseconds"] = floatSetter(c.lookaheadSeconds, 0.f, 3.f);
    s["tuning.mountedtask"] = mountedTaskSetter(c.mountedTask);

    s["display.overlay"] = boolSetter(c.overlay);
    s["display.blips"] = boolSetter(c.blips);
    s["display.blipstyle"] = textSetter(c.blipStyle, 64);

    s["hotkeys.host"] = keySetter(c.keyHost);
    s["hotkeys.join"] = keySetter(c.keyJoin);
    s["hotkeys.leave"] = keySetter(c.keyLeave);
    s["hotkeys.toggleoverlay"] = keySetter(c.keyOverlay);
    s["hotkeys.regroup"] = keySetter(c.keyRegroup);
    s["hotkeys.reloadconfig"] = keySetter(c.keyReload);

    s["debug.verboselog"] = boolSetter(c.verboseLog);
    s["debug.statsintervalms"] = intSetter(c.statsIntervalMs, 0, 600000);
    return s;
}

}  // namespace

bool parseConfig(const std::string& text, Config& cfg, std::vector<std::string>& warnings) {
    std::map<std::string, Setter> setters = buildSetters(cfg);
    std::istringstream in(text);
    std::string raw;
    std::string section;
    int lineNo = 0;
    while (std::getline(in, raw)) {
        ++lineNo;
        if (lineNo == 1 && raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF &&
            static_cast<unsigned char>(raw[1]) == 0xBB && static_cast<unsigned char>(raw[2]) == 0xBF) {
            raw = raw.substr(3);  // UTF-8 BOM from Notepad
        }
        std::string line = trim(raw);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        const std::string where = "line " + std::to_string(lineNo) + ": ";
        if (line.front() == '[') {
            if (line.back() != ']') {
                warnings.push_back(where + "section header is missing ']'");
                continue;
            }
            section = lower(trim(line.substr(1, line.size() - 2)));
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            warnings.push_back(where + "expected Key = Value");
            continue;
        }
        const std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        // Inline comments need whitespace before them so values stay intact.
        for (size_t i = 1; i < value.size(); ++i) {
            if ((value[i] == ';' || value[i] == '#') && std::isspace(static_cast<unsigned char>(value[i - 1]))) {
                value = trim(value.substr(0, i));
                break;
            }
        }
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        auto it = setters.find(section + "." + lower(key));
        if (it == setters.end()) {
            warnings.push_back(where + "unknown setting [" + section + "] " + key + " (typo?)");
            continue;
        }
        std::string err;
        if (!it->second(value, err)) {
            warnings.push_back(where + "[" + section + "] " + key + " = " + value + ": " + err + ", keeping the default");
        }
    }
    return true;
}

bool loadConfigFile(const std::string& path, Config& cfg, std::vector<std::string>& warnings) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    return parseConfig(ss.str(), cfg, warnings);
}

int parseKeyName(const std::string& name) {
    const std::string n = upper(trim(name));
    if (n.empty()) return -1;
    if (n == "NONE" || n == "OFF") return 0;
    if (n.size() >= 2 && n[0] == 'F' && std::all_of(n.begin() + 1, n.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)) != 0; })) {
        const int f = std::atoi(n.c_str() + 1);
        if (f >= 1 && f <= 24) return 0x70 + f - 1;
        return -1;
    }
    if (n.size() == 1 && n[0] >= 'A' && n[0] <= 'Z') return n[0];
    if (n.size() == 1 && n[0] >= '0' && n[0] <= '9') return n[0];
    if (n.size() == 7 && n.compare(0, 6, "NUMPAD") == 0 && n[6] >= '0' && n[6] <= '9') return 0x60 + (n[6] - '0');
    static const std::map<std::string, int> named = {
        {"INSERT", 0x2D}, {"DELETE", 0x2E}, {"HOME", 0x24},    {"END", 0x23},
        {"PAGEUP", 0x21}, {"PAGEDOWN", 0x22}, {"PAUSE", 0x13}, {"SCROLLLOCK", 0x91},
    };
    auto it = named.find(n);
    return it == named.end() ? -1 : it->second;
}

std::string keyName(int vk) {
    if (vk == 0) return "NONE";
    if (vk >= 0x70 && vk <= 0x87) return "F" + std::to_string(vk - 0x70 + 1);
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, static_cast<char>(vk));
    if (vk >= 0x60 && vk <= 0x69) return "NUMPAD" + std::to_string(vk - 0x60);
    switch (vk) {
        case 0x2D: return "INSERT";
        case 0x2E: return "DELETE";
        case 0x24: return "HOME";
        case 0x23: return "END";
        case 0x21: return "PAGEUP";
        case 0x22: return "PAGEDOWN";
        case 0x13: return "PAUSE";
        case 0x91: return "SCROLLLOCK";
        default: break;
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "VK_%02X", vk);
    return buf;
}

const char* toString(MovementMode m) { return m == MovementMode::Teleport ? "Teleport" : "Hybrid"; }

const char* toString(MountedTask t) {
    switch (t) {
        case MountedTask::Rider: return "Rider";
        case MountedTask::Horse: return "Horse";
        case MountedTask::AnyMeans: return "AnyMeans";
    }
    return "?";
}

std::string describeConfig(const Config& c) {
    std::ostringstream os;
    os << "host port " << c.hostPort << ", join " << c.joinAddress << ":" << c.joinPort << ", max players "
       << c.maxPlayers << ", send " << c.sendRateHz << " Hz | name '" << c.name << "', model " << c.model
       << ", outfit " << c.outfit << " | interp " << c.interpDelayMs << " ms, extrapolate "
       << c.maxExtrapolationMs << " ms, teleport " << c.teleportDistance << " m, on foot "
       << toString(c.onFootMovement) << ", mounted " << toString(c.mountedMovement) << " | tuning correction "
       << c.correctionStart << " m x" << c.correctionGain << ", retask " << c.retaskMs << " ms, lookahead "
       << c.lookaheadSeconds << " s, mounted task " << toString(c.mountedTask) << " | keys host "
       << keyName(c.keyHost) << " join " << keyName(c.keyJoin) << " leave " << keyName(c.keyLeave) << " regroup "
       << keyName(c.keyRegroup) << " overlay " << keyName(c.keyOverlay) << " reload " << keyName(c.keyReload);
    return os.str();
}

}  // namespace coop
