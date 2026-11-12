// The game's English text for keyboard and mouse (GameInfo::patch_file): the strings that name a controller's buttons
// say the keys of the default layout instead.
//
// UI\XBox\strINTX.pkg holds the menus' text: each string a 4 (its type), its length in characters, then UTF-16, ended
// by a 0 and padded to 4 bytes, the file's tables pointing at them by offset. So a string keeps its length: a shorter
// one is filled out with spaces at its end.
#include <cstdint>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include "core/log.h"

namespace {

struct Swap {
    const char* from;
    const char* to;  // no longer than from
};

const Swap kEnglish[] = {
    // the menus' own keys (core/keyboard.cpp): A Enter, the shoulders Q and E
    {">> PLEASE PRESS START TO BEGIN", ">> PLEASE PRESS ENTER TO BEGIN"},
    {"Use the left and right triggers to view", "Use Q and E to view"},  // the Personnel Jacket's pages
    // earlier wordings, in a copy on the cache drive patched by an older build
    {">> PLEASE PRESS [ESC] TO BEGIN", ">> PLEASE PRESS ENTER TO BEGIN"},
    {"Use the mouse buttons to view", "Use Q and E to view"},
};

std::u16string Wide(const char* s) {
    std::u16string w;
    for (; *s; s++) w += char16_t(uint8_t(*s));
    return w;
}

// each from in the UTF-16 text: replaced by to, the string it is in then filled out with spaces at its end
int Apply(std::vector<uint8_t>& data, const Swap& swap) {
    std::u16string from = Wide(swap.from), to = Wide(swap.to);
    if (to.size() > from.size()) return 0;
    size_t bytes = from.size() * 2;
    int done = 0;
    for (size_t at = 0; at + bytes <= data.size(); at += 2) {
        if (std::memcmp(&data[at], from.data(), bytes) != 0) continue;
        // the text after the match up to the string's end moves up, spaces fill the gap left at its end
        size_t end = at + bytes;
        while (end + 1 < data.size() && (data[end] || data[end + 1])) end += 2;
        size_t shift = (from.size() - to.size()) * 2;
        std::memmove(&data[at + to.size() * 2], &data[at + bytes], end - (at + bytes));
        std::memcpy(&data[at], to.data(), to.size() * 2);
        for (size_t p = end - shift; p < end; p += 2) {
            data[p] = ' ';
            data[p + 1] = 0;
        }
        done++;
        at = end;
    }
    return done;
}

}  // namespace

bool PatchGameText(const char* path, std::vector<uint8_t>& data) {
    std::string name = path;
    for (char& c : name) c = char(std::toupper(uint8_t(c)));
    if (name != "UI\\XBOX\\STRINTX.PKG") return false;
    int done = 0;
    for (const Swap& s : kEnglish) {
        int n = Apply(data, s);
        if (!n) {  // already changed (the cache drive's copy, patched in place before) or not there
            std::u16string to = Wide(s.to);
            bool patched = false;
            for (size_t at = 0; !patched && at + to.size() * 2 <= data.size(); at += 2)
                patched = std::memcmp(&data[at], to.data(), to.size() * 2) == 0;
            if (!patched) LOGR("text: \"%s\" not found", s.from);
        }
        done += n;
    }
    return done > 0;
}
