#pragma once
#include <string>
#include <stdexcept>

namespace helper {
inline std::wstring quoteWindowsArgument(const std::wstring& arg) {
    std::wstring out = L"\""; size_t backslashes = 0;
    for (auto c : arg) {
        if (!c) throw std::runtime_error("NUL in argument");
        if (c == L'\\') { ++backslashes; continue; }
        if (c == L'\"') { out.append(backslashes * 2 + 1, L'\\'); out += c; }
        else { out.append(backslashes, L'\\'); out += c; }
        backslashes = 0;
    }
    out.append(backslashes * 2, L'\\'); out += L'\"'; return out;
}
}
