#include "host/runtime/unicode.h"

namespace ntmac::runtime {

std::u16string utf8_to_utf16(std::string_view s) {
    std::u16string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        auto c = static_cast<unsigned char>(s[i]);
        uint32_t cp;
        size_t n;
        if (c < 0x80)              { cp = c;        n = 1; }
        else if ((c >> 5) == 0x6)  { cp = c & 0x1F; n = 2; }
        else if ((c >> 4) == 0xE)  { cp = c & 0x0F; n = 3; }
        else if ((c >> 3) == 0x1E) { cp = c & 0x07; n = 4; }
        else                       { cp = 0xFFFD;   n = 1; }

        bool valid = i + n <= s.size();
        for (size_t k = 1; valid && k < n; k++) {
            auto cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) valid = false;
            else cp = (cp << 6) | (cc & 0x3F);
        }
        if (!valid) {
            cp = 0xFFFD;
            n = 1;
        }
        i += n;

        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 | (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 | (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }
    return out;
}

std::string utf16_to_utf8(std::u16string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        uint32_t cp = s[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            i++;
        } else if (cp >= 0xD800 && cp < 0xE000) {
            cp = 0xFFFD;
        }

        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

std::u16string_view utf16_view(const uint16_t* s) {
    if (!s) return {};
    size_t n = 0;
    while (s[n]) n++;
    return {reinterpret_cast<const char16_t*>(s), n};
}

std::u16string to_windows_path(const std::filesystem::path& path) {
    auto s = utf8_to_utf16(path.string());
    for (auto& c : s) {
        if (c == u'/') c = u'\\';
    }
    return path.is_absolute() ? u"Z:" + s : s;
}

std::filesystem::path from_windows_path(std::u16string_view path) {
    std::u16string s(path);
    for (auto& c : s) {
        if (c == u'\\') c = u'/';
    }

    if (s.size() >= 2 && s[1] == u':') {
        if (s[0] != u'Z' && s[0] != u'z') return {};
        s.erase(0, 2);
        if (s.empty()) s = u"/";
    } else if (s.starts_with(u"//")) {
        return {}; // UNC
    }
    return utf16_to_utf8(s);
}

}
