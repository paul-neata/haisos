#pragma once

#ifdef _WIN32
#include <climits>
#include <string>
#include <windows.h>

namespace Haisos {

// Text is UTF-8 everywhere inside Haisos -- JSON, the console, Lua, the names
// of files. The Windows API takes UTF-16 instead, through its "W" functions;
// these convert between the two where Haisos meets it. (The narrow "A"
// functions, and the C runtime's narrow ones, take the ANSI code page, which
// holds few of the characters a name may have: nothing hands them a name.)

// Converts UTF-8 |text| to UTF-16. The lengths are explicit on both sides, so
// |out| holds exactly the characters of |text| and no terminating NUL -- one
// converted with a length of -1 keeps its NUL, which then sits inside whatever
// string it is pasted into. Returns false if |text| is not valid UTF-8 or too
// long to convert; an empty |text| converts to an empty |out|
// (MultiByteToWideChar itself refuses a length of 0).
inline bool Utf8ToWide(const std::string& text, std::wstring& out) {
    out.clear();
    if (text.empty()) {
        return true;
    }
    if (text.size() > static_cast<size_t>(INT_MAX)) {
        return false;
    }
    const int length = static_cast<int>(text.size());
    const int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, nullptr, 0);
    if (wideLength <= 0) {
        return false;
    }
    std::wstring wide(static_cast<size_t>(wideLength), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, &wide[0], wideLength) != wideLength) {
        return false;
    }
    out = std::move(wide);
    return true;
}

// Converts UTF-16 |text| to UTF-8. A lone surrogate, which Windows lets a file
// name hold, becomes U+FFFD: the name can then be shown, though not opened.
inline std::string WideToUtf8(const std::wstring& text) {
    if (text.empty() || text.size() > static_cast<size_t>(INT_MAX)) {
        return std::string();
    }
    const int length = static_cast<int>(text.size());
    const int narrowLength = WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
    if (narrowLength <= 0) {
        return std::string();
    }
    std::string narrow(static_cast<size_t>(narrowLength), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, text.data(), length, &narrow[0], narrowLength, nullptr, nullptr) != narrowLength) {
        return std::string();
    }
    return narrow;
}

} // namespace Haisos

#endif // _WIN32
