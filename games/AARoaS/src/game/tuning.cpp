// Recoil System (Settings::weapon_tuning, the overlay's General: Original or Revised). The game reads each weapon's
// recoil, sway, accuracy and fire rates from System\AACTuning.ini (sections such as [AGP_Inventory.Weapon_M16A2_Rifle]:
// RecoilPitch, RecoilYaw, fRecoilFalloff, fWaverLimit, ...). The player's own values sit in weapon_tuning.ini beside
// the exe, only the lines they change under the same section names. On Revised, as the game first opens its file
// (text.cpp's patch hook, GameInfo::patch_file) each of those lines takes the place of the lines with its key in that
// section (a key given more than once replaces them all, for the lists); a section or key the game lacks is added.
// On Original the file is the disc's. The game's own file is never changed: the result is the overrides\ stand-in
// files.cpp writes. Read once at start: a change takes effect the next start.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "app/app.h"
#include "core/log.h"

namespace {

constexpr const char* kTuningFile = "weapon_tuning.ini";

std::string Upper(std::string s) {
    for (char& c : s) c = char(std::toupper(uint8_t(c)));
    return s;
}

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// a line's key: the text before '=', trimmed and in capitals; "" for a header, comment or blank
std::string KeyOf(const std::string& line) {
    std::string t = Trim(line);
    if (t.empty() || t[0] == '[' || t[0] == ';' || t[0] == '#') return {};
    size_t eq = t.find('=');
    return eq == std::string::npos ? std::string() : Upper(Trim(t.substr(0, eq)));
}

bool IsHeader(const std::string& line, std::string* name = nullptr) {
    std::string t = Trim(line);
    if (t.size() < 2 || t.front() != '[' || t.back() != ']') return false;
    if (name) *name = Upper(Trim(t.substr(1, t.size() - 2)));
    return true;
}

struct Section {
    std::string header;                  // as the player wrote it
    std::vector<std::string> keys;       // in the player's order, each once
    std::map<std::string, std::vector<std::string>> lines;  // by key: the player's lines for it
};

std::vector<Section> ReadCustom(const std::filesystem::path& file) {
    std::vector<Section> out;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        std::string name;
        if (IsHeader(line, &name)) {
            out.push_back({Trim(line)});
            continue;
        }
        std::string key = KeyOf(line);
        if (key.empty() || out.empty()) continue;
        Section& s = out.back();
        if (!s.lines.count(key)) s.keys.push_back(key);
        s.lines[key].push_back(Trim(line));
    }
    return out;
}

}  // namespace

// text.cpp's patch hook, for System\AACTuning.ini: true always, so Original writes the disc's file back over a
// Revised stand-in
bool PatchWeaponTuning(std::vector<uint8_t>& data) {
    if (!app::kGame.weapon_tuning || app::Get().weapon_tuning != 1) {  // offered and on, else the game's
        LOGR("weapon tuning: original");
        return true;
    }
    std::filesystem::path file = app::ExeDir() / kTuningFile;
    std::vector<Section> custom = ReadCustom(file);
    if (custom.empty()) {
        LOGR("weapon tuning: revised, but %s has no sections: original", file.string().c_str());
        return true;
    }
    // the game's lines, without their line ends
    std::vector<std::string> lines;
    {
        std::string text(data.begin(), data.end()), line;
        std::istringstream in(text);
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
    }
    int replaced = 0, added = 0;
    for (const Section& s : custom) {
        std::string want;
        IsHeader(s.header, &want);
        size_t begin = lines.size();
        for (size_t i = 0; i < lines.size(); i++) {
            std::string name;
            if (IsHeader(lines[i], &name) && name == want) {
                begin = i + 1;
                break;
            }
        }
        if (begin == lines.size()) {  // a section the game lacks: added at the end
            lines.push_back(s.header);
            for (const std::string& key : s.keys)
                for (const std::string& l : s.lines.at(key)) lines.push_back(l), added++;
            LOGR("weapon tuning: %s is not in the game's file, added", s.header.c_str());
            continue;
        }
        for (const std::string& key : s.keys) {
            size_t end = begin;
            while (end < lines.size() && !IsHeader(lines[end])) end++;
            // the game's lines with this key out, the player's in at the first one's place (else the section's end)
            size_t at = end;
            for (size_t i = end; i-- > begin;) {
                if (KeyOf(lines[i]) != key) continue;
                lines.erase(lines.begin() + std::ptrdiff_t(i));
                at = i;
                replaced++;
            }
            if (at > lines.size()) at = lines.size();
            const auto& mine = s.lines.at(key);
            lines.insert(lines.begin() + std::ptrdiff_t(at), mine.begin(), mine.end());
            added += int(mine.size());
        }
    }
    std::string out;
    for (const std::string& l : lines) out += l + "\r\n";
    data.assign(out.begin(), out.end());
    LOGR("weapon tuning: revised, %zu sections from %s (%d lines in, %d of the game's out)", custom.size(),
         file.string().c_str(), added, replaced);
    return true;
}
