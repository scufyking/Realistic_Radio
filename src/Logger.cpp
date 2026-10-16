// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "Common.h"
#include "Logger.h"
#include <cstdio>
#include <cstdarg>

namespace {
std::mutex g_mtx;
FILE* g_file = nullptr;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
std::wstring PluginDataDir() {
    HMODULE mod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)&PluginDataDir, &mod);
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(mod, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    std::wstring p(buf, n);
    size_t slash = p.find_last_of(L"\\/");
    p = (slash == std::wstring::npos) ? L"" : p.substr(0, slash + 1);
    return p + L"realistic-radio\\";
}

namespace Log {
void Init(const std::wstring& path) {
    std::lock_guard<std::mutex> l(g_mtx);
    if (!g_file) g_file = _wfopen(path.c_str(), L"w");
}
void Write(const char* fmt, ...) {
    char msg[2048];
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    SYSTEMTIME st; GetLocalTime(&st);
    std::lock_guard<std::mutex> l(g_mtx);
    if (g_file) {
        fprintf(g_file, "[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
        fflush(g_file);
    }
    OutputDebugStringA(("[realistic-radio] " + std::string(msg) + "\n").c_str());
}
void Shutdown() {
    std::lock_guard<std::mutex> l(g_mtx);
    if (g_file) { fclose(g_file); g_file = nullptr; }
}
}
