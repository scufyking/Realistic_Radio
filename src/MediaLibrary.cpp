// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "MediaLibrary.h"
#include "Logger.h"
#include <shlwapi.h>
#include <algorithm>
#include <cstdint>
#include <cwctype>

namespace {
const wchar_t* kExtensions[] = {L".mp3", L".wav", L".flac", L".m4a", L".aac", L".wma"};
constexpr size_t kMaxTracks = 5000;

bool HasAudioExtension(const std::wstring& name) {
    size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = name.substr(dot);
    for (auto& c : ext) c = (wchar_t)towlower(c);
    for (auto* e : kExtensions) if (ext == e) return true;
    return false;
}

std::vector<uint8_t> ReadBytes(const std::wstring& path, uint64_t offset, size_t count) {
    std::vector<uint8_t> out;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return out;
    LARGE_INTEGER pos; pos.QuadPart = (LONGLONG)offset;
    if (SetFilePointerEx(f, pos, nullptr, FILE_BEGIN)) {
        out.resize(count);
        DWORD got = 0;
        if (!ReadFile(f, out.data(), (DWORD)count, &got, nullptr)) got = 0;
        out.resize(got);
    }
    CloseHandle(f);
    return out;
}
uint64_t FileSize(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d)) return 0;
    return ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}

std::wstring Latin1(const uint8_t* p, size_t n) {   // ID3 "ISO-8859-1" is in practice Windows-1252
    std::wstring w;
    if (!n) return w;
    int len = MultiByteToWideChar(1252, 0, (const char*)p, (int)n, nullptr, 0);
    w.resize(len);
    MultiByteToWideChar(1252, 0, (const char*)p, (int)n, &w[0], len);
    return w;
}
std::wstring Utf16(const uint8_t* p, size_t n, bool bigEndian) {
    std::wstring w;
    for (size_t i = 0; i + 1 < n; i += 2) w.push_back(bigEndian ? (wchar_t)((p[i] << 8) | p[i + 1]) : (wchar_t)(p[i] | (p[i + 1] << 8)));
    return w;
}
std::wstring Trim(std::wstring s) {
    while (!s.empty() && (s.back() == 0 || iswspace(s.back()))) s.pop_back();
    size_t a = 0; while (a < s.size() && iswspace(s[a])) ++a;
    return s.substr(a);
}
// ID3v2 text frame body -> first value
std::wstring DecodeText(const uint8_t* p, size_t n) {
    if (n < 1) return L"";
    uint8_t enc = p[0]; ++p; --n;
    std::wstring w;
    switch (enc) {
    case 0: w = Latin1(p, n); break;
    case 1:   // UTF-16 with BOM
        if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) w = Utf16(p + 2, n - 2, true);
        else if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) w = Utf16(p + 2, n - 2, false);
        else w = Utf16(p, n, false);
        break;
    case 2: w = Utf16(p, n, true); break;
    case 3: w = Utf8ToWide(std::string((const char*)p, n)); break;
    default: return L"";
    }
    size_t z = w.find(L'\0');   // ID3v2.4 separates several values with a null character
    if (z != std::wstring::npos) w.resize(z);
    if (!w.empty() && w[0] == 0xFEFF) w.erase(0, 1);
    return Trim(w);
}
uint32_t SyncSafe(const uint8_t* p) { return ((p[0] & 0x7f) << 21) | ((p[1] & 0x7f) << 14) | ((p[2] & 0x7f) << 7) | (p[3] & 0x7f); }
uint32_t Be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

struct Tags { std::wstring title, artist, album; std::vector<uint8_t> picture; std::string mime; };

// Reads ID3v2.2/2.3/2.4 (title, artist, album, optionally the front cover). maxBytes limits how much of the tag is read.
bool ParseId3v2(const std::wstring& path, size_t maxBytes, bool wantPicture, Tags& t) {
    auto hdr = ReadBytes(path, 0, 10);
    if (hdr.size() < 10 || hdr[0] != 'I' || hdr[1] != 'D' || hdr[2] != '3') return false;
    const int ver = hdr[3];
    if (ver < 2 || ver > 4) return false;
    const uint8_t flags = hdr[5];
    size_t size = SyncSafe(&hdr[6]);
    auto tag = ReadBytes(path, 10, std::min(size, maxBytes));
    if (flags & 0x80 && ver < 4) {   // tag-wide unsynchronisation (ID3v2.2/2.3): remove the inserted 0x00 after 0xFF
        std::vector<uint8_t> u; u.reserve(tag.size());
        for (size_t i = 0; i < tag.size(); ++i) { u.push_back(tag[i]); if (tag[i] == 0xFF && i + 1 < tag.size() && tag[i + 1] == 0) ++i; }
        tag.swap(u);
    }
    size_t pos = 0;
    if ((flags & 0x40) && ver >= 3 && tag.size() >= 4) pos = ver == 3 ? 4 + Be32(&tag[0]) : SyncSafe(&tag[0]);   // extended header
    const size_t hlen = ver == 2 ? 6 : 10;
    while (pos + hlen <= tag.size()) {
        std::string id; size_t fsize;
        if (ver == 2) { id.assign((const char*)&tag[pos], 3); fsize = (tag[pos + 3] << 16) | (tag[pos + 4] << 8) | tag[pos + 5]; }
        else { id.assign((const char*)&tag[pos], 4); fsize = ver == 4 ? SyncSafe(&tag[pos + 4]) : Be32(&tag[pos + 4]); }
        if (id[0] == 0) break;   // padding
        const uint8_t* body = tag.data() + pos + hlen;
        pos += hlen;
        if (fsize > tag.size() - pos) break;   // truncated (beyond maxBytes) or broken
        uint16_t fflags = ver >= 3 ? (uint16_t)((tag[pos - 2] << 8) | tag[pos - 1]) : 0;
        bool compressedOrEncrypted = ver == 3 ? (fflags & 0x00C0) != 0 : ver == 4 ? (fflags & 0x000C) != 0 : false;
        if (!compressedOrEncrypted) {
            if (id == "TIT2" || id == "TT2") t.title = DecodeText(body, fsize);
            else if (id == "TPE1" || id == "TP1") t.artist = DecodeText(body, fsize);
            else if (id == "TALB" || id == "TAL") t.album = DecodeText(body, fsize);
            else if (wantPicture && (id == "APIC" || id == "PIC") && fsize > 4) {
                const uint8_t* p = body; const uint8_t* end = body + fsize;
                uint8_t enc = *p++;
                std::string mime;
                if (id == "PIC") { mime = std::string((const char*)p, 3); p += 3; }
                else { while (p < end && *p) mime.push_back((char)*p++); ++p; }
                uint8_t type = p < end ? *p++ : 0;
                if (enc == 1 || enc == 2) { while (p + 1 < end && (p[0] || p[1])) p += 2; p += 2; }   // description
                else { while (p < end && *p) ++p; ++p; }
                if (p < end && (t.picture.empty() || type == 3)) {   // prefer the front cover
                    t.picture.assign(p, end);
                    t.mime = mime;
                }
            }
        }
        pos += fsize;
    }
    return true;
}
void ParseId3v1(const std::wstring& path, Tags& t) {
    uint64_t sz = FileSize(path);
    if (sz < 128) return;
    auto b = ReadBytes(path, sz - 128, 128);
    if (b.size() != 128 || b[0] != 'T' || b[1] != 'A' || b[2] != 'G') return;
    auto field = [&](size_t off) { size_t n = 0; while (n < 30 && b[off + n]) ++n; return Trim(Latin1(&b[off], n)); };
    if (t.title.empty()) t.title = field(3);
    if (t.artist.empty()) t.artist = field(33);
    if (t.album.empty()) t.album = field(63);
}

// "01 - Artist - Title.mp3" -> artist + title (for files without tags)
void FromFileName(const std::wstring& name, std::wstring& title, std::wstring& artist) {
    std::wstring n = name;
    size_t dot = n.find_last_of(L'.'); if (dot != std::wstring::npos) n.resize(dot);
    for (auto& c : n) if (c == L'_') c = L' ';
    size_t i = 0;
    while (i < n.size() && iswdigit(n[i])) ++i;
    if (i > 0 && i <= 3 && i < n.size() && (n[i] == L' ' || n[i] == L'.' || n[i] == L'-')) {   // leading track number
        size_t j = i; while (j < n.size() && (n[j] == L' ' || n[j] == L'.' || n[j] == L'-')) ++j;
        if (j < n.size()) n = n.substr(j);
    }
    size_t sep = n.find(L" - ");
    if (sep != std::wstring::npos && sep > 0 && sep + 3 < n.size()) { artist = Trim(n.substr(0, sep)); title = Trim(n.substr(sep + 3)); }
    else title = Trim(n);
}

void Collect(const std::wstring& dir, const std::wstring& rel, int depth, std::vector<MediaTrack>& out) {
    if (depth > 8 || out.size() >= kMaxTracks) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> dirs, files;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L".." || n[0] == L'.' || (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) dirs.push_back(n);
        else if (HasAudioExtension(n)) files.push_back(n);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    auto logical = [](const std::wstring& a, const std::wstring& b) { return StrCmpLogicalW(a.c_str(), b.c_str()) < 0; };
    std::sort(dirs.begin(), dirs.end(), logical);
    std::sort(files.begin(), files.end(), logical);
    for (auto& f : files) {   // files of a folder first, then its sub folders (album folders stay together)
        if (out.size() >= kMaxTracks) return;
        MediaTrack t; t.path = dir + f; t.rel = rel + f;
        Tags tags;
        ParseId3v2(t.path, 256 * 1024, false, tags);
        ParseId3v1(t.path, tags);   // only fills what ID3v2 did not have
        t.title = tags.title; t.artist = tags.artist; t.album = tags.album;
        if (t.title.empty()) { std::wstring a; FromFileName(f, t.title, a); if (t.artist.empty()) t.artist = a; }
        if (t.album.empty() && !rel.empty()) { std::wstring r = rel.substr(0, rel.size() - 1); size_t s = r.find_last_of(L'\\'); t.album = s == std::wstring::npos ? r : r.substr(s + 1); }
        out.push_back(std::move(t));
    }
    for (auto& d : dirs) Collect(dir + d + L"\\", rel + d + L"\\", depth + 1, out);
}

uint64_t Fnv(const std::wstring& s) {
    uint64_t h = 1469598103934665603ULL;
    for (wchar_t c : s) { h ^= (uint64_t)(c & 0xffff); h *= 1099511628211ULL; }
    return h;
}
bool Exists(const std::wstring& p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
} // namespace

void MediaLibrary::Scan(const std::wstring& folder) {
    folder_ = folder;
    if (GetFileAttributesW(folder.c_str()) == INVALID_FILE_ATTRIBUTES) {
        CreateDirectoryW(folder.c_str(), nullptr);
        HANDLE f = CreateFileW((folder + L"LIES-MICH - READ-ME.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            const char* text =
                "Realistic Radio - USB / CD\r\n"
                "==========================\r\n\r\n"
                "DEUTSCH\r\n"
                "Kopiere hier deine eigenen Musikdateien hinein (auch in Unterordnern, z. B. ein Ordner pro Album).\r\n"
                "Unterstuetzt: MP3, WAV, FLAC, M4A, AAC, WMA.\r\n"
                "Im Spiel mit der Taste \"Pos1\" (Home) zwischen Radio und USB umschalten.\r\n"
                "Pfeil links/rechts = Titel zurueck/vor, Ende = Pause.\r\n"
                "Titel und Interpret kommen aus den MP3-Tags, sonst aus dem Dateinamen (\"Interpret - Titel.mp3\").\r\n"
                "Ein Cover wird angezeigt, wenn es in der MP3 steckt oder als cover.jpg / folder.jpg im Ordner liegt.\r\n\r\n"
                "ENGLISH\r\n"
                "Put your own music files here (sub folders are fine, e.g. one folder per album).\r\n"
                "Supported: MP3, WAV, FLAC, M4A, AAC, WMA.\r\n"
                "In the game press \"Home\" to switch between radio and USB.\r\n"
                "Left/Right arrow = previous/next track, End = pause.\r\n"
                "Title and artist come from the MP3 tags, otherwise from the file name (\"Artist - Title.mp3\").\r\n"
                "A cover is shown if it is embedded in the MP3 or saved as cover.jpg / folder.jpg in the folder.\r\n";
            DWORD w = 0; WriteFile(f, text, (DWORD)strlen(text), &w, nullptr); CloseHandle(f);
        }
        Log::Write("Created USB folder %s", WideToUtf8(folder).c_str());
    }
    tracks_.clear();
    ULONGLONG t0 = GetTickCount64();
    Collect(folder, L"", 0, tracks_);
    Log::Write("USB: %d file(s) in %s (%llu ms)", (int)tracks_.size(), WideToUtf8(folder).c_str(), GetTickCount64() - t0);
}

int MediaLibrary::IndexOf(const std::wstring& rel) const {
    for (size_t i = 0; i < tracks_.size(); ++i) if (_wcsicmp(tracks_[i].rel.c_str(), rel.c_str()) == 0) return (int)i;
    return -1;
}

std::wstring MediaLibrary::CoverFor(const MediaTrack& t, const std::wstring& cacheDir) const {
    std::wstring lower = t.path; for (auto& c : lower) c = (wchar_t)towlower(c);
    if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, L".mp3") == 0) {
        wchar_t name[48]; swprintf(name, 48, L"cover-%016llx", (unsigned long long)(Fnv(lower) ^ FileSize(t.path)));
        for (const wchar_t* ext : {L".jpg", L".png"}) if (Exists(cacheDir + name + ext)) return cacheDir + name + ext;
        Tags tags;
        if (ParseId3v2(t.path, 24 * 1024 * 1024, true, tags) && tags.picture.size() > 16) {
            bool png = tags.picture[0] == 0x89 && tags.picture[1] == 'P';
            std::wstring file = cacheDir + name + (png ? L".png" : L".jpg");
            HANDLE f = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f != INVALID_HANDLE_VALUE) {
                DWORD w = 0; WriteFile(f, tags.picture.data(), (DWORD)tags.picture.size(), &w, nullptr); CloseHandle(f);
                if (w == tags.picture.size()) return file;
                DeleteFileW(file.c_str());
            }
        }
    }
    std::wstring dir = t.path.substr(0, t.path.find_last_of(L'\\') + 1);
    for (const wchar_t* n : {L"cover.jpg", L"cover.png", L"folder.jpg", L"folder.png", L"front.jpg", L"front.png", L"albumart.jpg"})
        if (Exists(dir + n)) return dir + n;
    return L"";
}
