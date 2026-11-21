// Realistic Radio Bus+ (add-on)
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "SiiFile.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <stdexcept>
extern "C" {
#include "../jobs/aes/aes.h"
#include "../jobs/miniz/miniz_tinfl.h"
}

// ---- "ScsC": 4 magic, 32 HMAC, 16 IV, 4 inflated size, then AES-256-CBC of a zlib stream
bool SiiDecode(std::string& b, std::string& error) {
    if (b.size() < 4 || b.compare(0, 4, "ScsC") != 0) return true;
    if (b.size() < 56 + 16) { error = "encrypted file too short"; return false; }
    static const uint8_t key[32] = {0x2a, 0x5f, 0xcb, 0x17, 0x91, 0xd2, 0x2f, 0xb6, 0x02, 0x45, 0xb3, 0xd8, 0x36, 0x9e, 0xd0, 0xb2,
                                    0xc2, 0x73, 0x71, 0x56, 0x3f, 0xbf, 0x1f, 0x3c, 0x9e, 0xdf, 0x6b, 0x11, 0x82, 0x5a, 0x5d, 0x0a};
    uint8_t iv[16]; memcpy(iv, b.data() + 36, 16);
    uint32_t size = 0; memcpy(&size, b.data() + 52, 4);
    std::string enc = b.substr(56);
    enc.resize(enc.size() / 16 * 16);
    AES_ctx ctx; AES_init_ctx_iv(&ctx, key, iv);
    AES_CBC_decrypt_buffer(&ctx, (uint8_t*)&enc[0], enc.size());
    std::string out(size ? size : enc.size() * 8, '\0');
    size_t got = tinfl_decompress_mem_to_mem(&out[0], out.size(), enc.data(), enc.size(), TINFL_FLAG_PARSE_ZLIB_HEADER);
    if (got == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) { error = "could not decrypt / unpack the save"; return false; }
    out.resize(got);
    b.swap(out);
    return true;
}

namespace {
const char kChars[] = "\0" "0123456789abcdefghijklmnopqrstuvwxyz_";
std::string Token(uint64_t v) { std::string r; while (v) { r += kChars[v % 38]; v /= 38; } return r; }
bool TokenValue(const std::string& t, uint64_t& v) {
    if (t.empty() || t.size() > 12) return false;
    v = 0;
    for (size_t i = t.size(); i-- > 0;) {
        const char* p = (const char*)memchr(kChars + 1, t[i], 37);
        if (!p) return false;
        v = v * 38 + (uint64_t)(p - kChars);
    }
    return true;
}
std::string Hex(uint64_t v) { char b[32]; snprintf(b, sizeof b, "%llx", (unsigned long long)v); return b; }
std::string Nameless(uint64_t v) {
    std::string r = "_nameless";
    bool started = false;
    for (int s = 48; s >= 0; s -= 16) {
        unsigned g = (unsigned)((v >> s) & 0xFFFF);
        if (!g && !started && s) continue;
        started = true; r += "." + Hex(g);
    }
    return r;
}
std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r"), z = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, z - a + 1);
}
std::string Num(double d) { char b[64]; snprintf(b, sizeof b, "%.9g", d); return b; }
// text values: "quoted" -> unquoted; &3f800000 -> float
std::string TextValue(std::string v) {
    v = Trim(v);
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
        std::string r;
        for (size_t i = 1; i + 1 < v.size(); ++i) { if (v[i] == '\\' && i + 2 < v.size()) ++i; r += v[i]; }
        return r;
    }
    if (v.size() == 9 && v[0] == '&') { uint32_t u = (uint32_t)strtoul(v.c_str() + 1, nullptr, 16); float f; memcpy(&f, &u, 4); return Num(f); }
    return v;
}
std::string Quote(const std::string& v) { std::string r = "\""; for (char c : v) { if (c == '"' || c == '\\') r += '\\'; r += c; } return r + "\""; }
int SimpleSize(uint32_t t) {
    switch (t) {
    case 0x03: return 8; case 0x05: return 4; case 0x07: return 8; case 0x09: return 12; case 0x11: return 12; case 0x17: return 16;
    case 0x19: return 32;
    case 0x25: return 4; case 0x27: return 4; case 0x29: return 2; case 0x2B: return 2; case 0x2F: return 4; case 0x31: return 8;
    case 0x33: return 8; case 0x35: return 1; case 0x37: return 4;
    default: return 0;
    }
}
}

const SiiField* SiiUnit::F(const std::string& name) const { for (auto& f : fields) if (f.name == name) return &f; return nullptr; }
std::string SiiUnit::S(const std::string& name) const { const SiiField* f = F(name); return f && !f->v.empty() ? f->v[0] : std::string(); }
uint32_t SiiUnit::U(const std::string& name, uint32_t def) const {
    const SiiField* f = F(name);
    if (!f || f->v.empty() || f->v[0].empty()) return def;
    return (uint32_t)strtoull(f->v[0].c_str(), nullptr, 10);
}
double SiiUnit::D(const std::string& name, double def) const {
    const SiiField* f = F(name);
    if (!f || f->v.empty() || f->v[0].empty()) return def;
    return strtod(f->v[0].c_str(), nullptr);
}
std::vector<std::string> SiiUnit::A(const std::string& name) const {
    const SiiField* f = F(name);
    if (!f) return {};
    if (f->type == 0 && !f->array && f->v.size() == 1 && f->v[0] == "0") return {};   // text save: "key: 0" is an empty array
    return f->v;
}

const SiiUnit* SiiFile::Unit(const std::string& id) const { auto it = index_.find(id); return it == index_.end() ? nullptr : &units_[it->second]; }
std::vector<const SiiUnit*> SiiFile::OfClass(const std::string& cls) const {
    std::vector<const SiiUnit*> r;
    for (auto& u : units_) if (u.cls == cls) r.push_back(&u);
    return r;
}

bool SiiFile::Load(std::string bytes, std::string& error) {
    data_.swap(bytes);
    units_.clear(); index_.clear(); defs_.clear(); edits_.clear();
    if (!SiiDecode(data_, error)) return false;
    bool ok;
    if (data_.compare(0, 4, "BSII") == 0) { binary_ = true; ok = LoadBinary(error); }
    else if (data_.compare(0, 8, "SiiNunit") == 0) { binary_ = false; ok = LoadText(error); }
    else { error = "unknown save format"; return false; }
    if (!ok) return false;
    for (size_t i = 0; i < units_.size(); ++i) index_[units_[i].id] = i;
    return true;
}

// ---- text: "class : name {" ... " key: value" / " key[i]: value" / " key[]: value" ... "}"
bool SiiFile::LoadText(std::string& error) {
    const std::string& d = data_;
    size_t pos = 0;
    SiiUnit u;
    bool in = false;
    while (pos < d.size()) {
        size_t eol = d.find('\n', pos);
        if (eol == std::string::npos) eol = d.size();
        const size_t lineBegin = pos;
        std::string t = Trim(d.substr(pos, eol - pos));
        pos = eol + 1;
        if (!in) {
            size_t colon = t.find(" : ");
            if (colon != std::string::npos && !t.empty() && t.back() == '{') {
                u = SiiUnit();
                u.cls = Trim(t.substr(0, colon));
                u.id = Trim(t.substr(colon + 3, t.size() - colon - 4));
                u.begin = lineBegin; u.valuesBegin = std::min(pos, d.size());
                in = true;
            }
            continue;
        }
        if (t == "}") {
            u.end = std::min(pos, d.size());
            units_.push_back(std::move(u));
            in = false;
            continue;
        }
        size_t c = t.find(':');
        if (c == std::string::npos) continue;
        std::string key = Trim(t.substr(0, c)), val = t.substr(c + 1);
        size_t br = key.find('[');
        std::string base = br == std::string::npos ? key : key.substr(0, br);
        SiiField* f = u.fields.empty() || u.fields.back().name != base ? nullptr : &u.fields.back();
        if (!f) { u.fields.push_back(SiiField()); f = &u.fields.back(); f->name = base; f->begin = lineBegin; }
        f->end = std::min(pos, d.size());
        if (br != std::string::npos) { f->array = true; f->v.push_back(TextValue(val)); }
        else {
            // "key: 3" followed by key[0..2] is an array; a plain value otherwise (decided by the next lines)
            f->v.assign(1, TextValue(val));
            size_t next = d.find_first_not_of(" \t", pos);
            if (next != std::string::npos && d.compare(next, base.size() + 1, base + "[") == 0) { f->array = true; f->v.clear(); }
        }
    }
    (void)error;
    if (units_.empty()) { error = "no units in the text save"; return false; }
    return true;
}

// ---- binary: structure definitions (id 0) and records (id = structure)
bool SiiFile::LoadBinary(std::string& error) {
    const std::string& d = data_;
    const uint8_t* b = (const uint8_t*)d.data();
    const size_t n = d.size();
    size_t p = 4;
    auto need = [&](size_t k) { if (p + k > n) throw std::runtime_error("unexpected end of file"); };
    auto u32 = [&]() { need(4); uint32_t v; memcpy(&v, b + p, 4); p += 4; return v; };
    auto u64 = [&]() { need(8); uint64_t v; memcpy(&v, b + p, 8); p += 8; return v; };
    auto str = [&]() { uint32_t k = u32(); need(k); std::string s((const char*)b + p, k); p += k; return s; };
    auto id = [&]() -> std::string {
        need(1); uint8_t parts = b[p++];
        if (parts == 0xFF) return Nameless(u64());
        if (parts == 0) return "null";
        std::string r;
        for (int i = 0; i < parts; ++i) { if (i) r += '.'; r += Token(u64()); }
        return r;
    };
    auto f32 = [&]() { need(4); float f; memcpy(&f, b + p, 4); p += 4; return Num(f); };
    // one value of a simple type as text
    std::function<std::string(uint32_t, const std::map<uint32_t, std::string>&)> one = [&](uint32_t t, const std::map<uint32_t, std::string>& en) -> std::string {
        switch (t) {
        case 0x05: return f32();
        case 0x07: { std::string a = f32(); return "(" + a + ", " + f32() + ")"; }
        case 0x09: { std::string a = f32(), bb = f32(); return "(" + a + ", " + bb + ", " + f32() + ")"; }
        case 0x11: { need(12); int32_t v[3]; memcpy(v, b + p, 12); p += 12; return "(" + std::to_string(v[0]) + ", " + std::to_string(v[1]) + ", " + std::to_string(v[2]) + ")"; }
        case 0x17: { std::string a = f32(), bb = f32(), c = f32(); return "(" + a + "; " + bb + ", " + c + ", " + f32() + ")"; }
        case 0x19: need(32); p += 32; return "(placement)";
        case 0x25: { need(4); int32_t v; memcpy(&v, b + p, 4); p += 4; return std::to_string(v); }
        case 0x27: case 0x2F: return std::to_string(u32());
        case 0x29: { need(2); int16_t v; memcpy(&v, b + p, 2); p += 2; return std::to_string(v); }
        case 0x2B: { need(2); uint16_t v; memcpy(&v, b + p, 2); p += 2; return std::to_string(v); }
        case 0x03: case 0x31: { int64_t v = (int64_t)u64(); return std::to_string(v); }
        case 0x33: return std::to_string(u64());
        case 0x35: { need(1); return std::to_string(b[p++]); }
        case 0x37: { uint32_t v = u32(); auto it = en.find(v); return it == en.end() ? std::to_string(v) : it->second; }
        default: break;
        }
        throw std::runtime_error("unknown value type 0x" + Hex(t) + " at byte " + std::to_string(p));
    };
    try {
        uint32_t version = u32();
        if (version < 1 || version > 3) { error = "BSII version " + std::to_string(version) + " is not supported"; return false; }
        while (p < n) {
            const size_t recBegin = p;
            uint32_t t = u32();
            if (t == 0) {
                need(1); uint8_t valid = b[p++];
                if (!valid) break;
                uint32_t sid = u32();
                Def def; def.name = str();
                for (;;) {
                    uint32_t vt = u32();
                    if (!vt) break;
                    std::string fn = str();
                    std::map<uint32_t, std::string> en;
                    if (vt == 0x37) { uint32_t k = u32(); for (uint32_t i = 0; i < k; ++i) { uint32_t key = u32(); en[key] = str(); } }
                    def.fields.push_back({vt, fn});
                    def.enums.push_back(en);
                }
                defs_[sid] = def;
                continue;
            }
            auto it = defs_.find(t);
            if (it == defs_.end()) throw std::runtime_error("record of unknown structure " + std::to_string(t));
            const Def& def = it->second;
            SiiUnit u;
            u.cls = def.name; u.sid = t; u.begin = recBegin;
            u.id = id();
            u.valuesBegin = p;
            for (size_t fi = 0; fi < def.fields.size(); ++fi) {
                const uint32_t vt = def.fields[fi].first;
                SiiField f; f.name = def.fields[fi].second; f.type = vt; f.begin = p;
                const auto& en = def.enums[fi];
                switch (vt) {
                case 0x01: f.v.push_back(str()); break;
                case 0x02: { f.array = true; uint32_t k = u32(); for (uint32_t i = 0; i < k; ++i) f.v.push_back(str()); break; }
                case 0x39: case 0x3B: case 0x3D: f.v.push_back(id()); break;
                case 0x3A: case 0x3C: case 0x3E: { f.array = true; uint32_t k = u32(); for (uint32_t i = 0; i < k; ++i) f.v.push_back(id()); break; }
                default:
                    if (SimpleSize(vt)) { f.v.push_back(one(vt, en)); break; }
                    if (vt % 2 == 0 && SimpleSize(vt - 1)) { f.array = true; uint32_t k = u32(); for (uint32_t i = 0; i < k; ++i) f.v.push_back(one(vt - 1, en)); break; }
                    throw std::runtime_error("unknown value type 0x" + Hex(vt) + " at byte " + std::to_string(p));
                }
                f.end = p;
                u.fields.push_back(std::move(f));
            }
            u.end = p;
            units_.push_back(std::move(u));
        }
    } catch (const std::exception& e) {
        error = std::string("binary save: ") + e.what();
        return false;
    }
    return true;
}

void SiiFile::Set(const std::string& unitId, const std::string& field, const std::vector<std::string>& values, bool quoted) {
    Edit e; e.v = values; e.quoted = quoted; e.array = true;
    edits_[unitId][field] = e;
}
void SiiFile::Set(const std::string& unitId, const std::string& field, const std::string& value, bool quoted) {
    Edit e; e.v.assign(1, value); e.quoted = quoted; e.array = false;
    edits_[unitId][field] = e;
}

std::string SiiFile::Encode(uint32_t t, const std::map<uint32_t, std::string>& enums, const Edit& e, std::string& error) const {
    std::string r;
    auto put32 = [&](uint32_t v) { r.append((const char*)&v, 4); };
    auto putStr = [&](const std::string& s) { put32((uint32_t)s.size()); r += s; };
    auto putId = [&](const std::string& s) -> bool {
        if (s.empty() || s == "null") { r += (char)0; return true; }
        if (s.rfind("_nameless.", 0) == 0) {
            uint64_t v = 0;
            size_t pos = 10;
            while (pos <= s.size()) {
                size_t dot = s.find('.', pos);
                std::string g = s.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos);
                v = (v << 16) | (strtoull(g.c_str(), nullptr, 16) & 0xFFFF);
                if (dot == std::string::npos) break;
                pos = dot + 1;
            }
            r += (char)0xFF; r.append((const char*)&v, 8);
            return true;
        }
        std::vector<uint64_t> parts;
        size_t pos = 0;
        for (;;) {
            size_t dot = s.find('.', pos);
            uint64_t v;
            if (!TokenValue(s.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos), v)) return false;
            parts.push_back(v);
            if (dot == std::string::npos) break;
            pos = dot + 1;
        }
        if (parts.size() > 254) return false;
        r += (char)parts.size();
        for (uint64_t v : parts) r.append((const char*)&v, 8);
        return true;
    };
    auto putOne = [&](uint32_t et, const std::string& s) -> bool {
        switch (et) {
        case 0x25: { int32_t v = (int32_t)strtol(s.c_str(), nullptr, 10); r.append((const char*)&v, 4); return true; }
        case 0x27: case 0x2F: { uint32_t v = (uint32_t)strtoul(s.c_str(), nullptr, 10); r.append((const char*)&v, 4); return true; }
        case 0x29: { int16_t v = (int16_t)strtol(s.c_str(), nullptr, 10); r.append((const char*)&v, 2); return true; }
        case 0x2B: { uint16_t v = (uint16_t)strtoul(s.c_str(), nullptr, 10); r.append((const char*)&v, 2); return true; }
        case 0x35: { r += (char)(uint8_t)strtoul(s.c_str(), nullptr, 10); return true; }
        case 0x05: { float f = strtof(s.c_str(), nullptr); r.append((const char*)&f, 4); return true; }
        case 0x37: {
            for (auto& kv : enums) if (kv.second == s) { put32(kv.first); return true; }
            char* endp = nullptr; unsigned long v = strtoul(s.c_str(), &endp, 10);
            if (endp && *endp == 0 && !s.empty()) { put32((uint32_t)v); return true; }
            return false;
        }
        default: return false;
        }
    };
    switch (t) {
    case 0x01: putStr(e.v.empty() ? std::string() : e.v[0]); return r;
    case 0x02: put32((uint32_t)e.v.size()); for (auto& s : e.v) putStr(s); return r;
    case 0x39: case 0x3B: case 0x3D: if (!putId(e.v.empty() ? std::string() : e.v[0])) error = "bad id"; return r;
    case 0x3A: case 0x3C: case 0x3E: put32((uint32_t)e.v.size()); for (auto& s : e.v) if (!putId(s)) { error = "bad id " + s; break; } return r;
    default: break;
    }
    if (SimpleSize(t)) { if (!putOne(t, e.v.empty() ? std::string("0") : e.v[0])) error = "cannot write value type 0x" + Hex(t); return r; }
    if (t % 2 == 0 && SimpleSize(t - 1)) {
        put32((uint32_t)e.v.size());
        for (auto& s : e.v) if (!putOne(t - 1, s)) { error = "cannot write value type 0x" + Hex(t); break; }
        return r;
    }
    error = "cannot write value type 0x" + Hex(t);
    return r;
}

bool SiiFile::Commit(std::string& error) {
    if (edits_.empty()) return true;
    struct Splice { size_t begin, end; std::string with; };
    std::vector<Splice> splices;
    for (auto& ue : edits_) {
        const SiiUnit* u = Unit(ue.first);
        if (!u) { error = "unit not found: " + ue.first; return false; }
        for (auto& fe : ue.second) if (!u->F(fe.first)) { error = "field not found: " + ue.first + " / " + fe.first; return false; }
        std::string out;
        if (binary_) {
            const Def& def = defs_.at(u->sid);
            for (size_t i = 0; i < u->fields.size(); ++i) {
                const SiiField& f = u->fields[i];
                auto it = ue.second.find(f.name);
                if (it == ue.second.end()) { out.append(data_, f.begin, f.end - f.begin); continue; }
                std::string err;
                out += Encode(f.type, def.enums[i], it->second, err);
                if (!err.empty()) { error = ue.first + " / " + f.name + ": " + err; return false; }
            }
            splices.push_back({u->valuesBegin, u->end, out});
        } else {
            for (const SiiField& f : u->fields) {
                auto it = ue.second.find(f.name);
                if (it == ue.second.end()) { out.append(data_, f.begin, f.end - f.begin); continue; }
                const Edit& e = it->second;
                auto val = [&](const std::string& s) { return e.quoted ? Quote(s) : s; };
                if (e.array) {
                    out += " " + f.name + ": " + std::to_string(e.v.size()) + "\n";
                    for (size_t k = 0; k < e.v.size(); ++k) out += " " + f.name + "[" + std::to_string(k) + "]: " + val(e.v[k]) + "\n";
                } else out += " " + f.name + ": " + val(e.v.empty() ? std::string() : e.v[0]) + "\n";
            }
            // the "}" line stays: fields end before it
            size_t fieldsEnd = u->fields.empty() ? u->valuesBegin : u->fields.back().end;
            splices.push_back({u->valuesBegin, fieldsEnd, out});
        }
    }
    std::sort(splices.begin(), splices.end(), [](const Splice& a, const Splice& b) { return a.begin > b.begin; });
    for (auto& s : splices) data_.replace(s.begin, s.end - s.begin, s.with);
    edits_.clear();
    return true;
}
