// Realistic Radio Jobs+ (add-on)
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "SaveGame.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <sstream>
extern "C" {
#include "aes/aes.h"
#include "miniz/miniz_tinfl.h"
}

// ---- "ScsC": 4 magic, 32 HMAC, 16 IV, 4 inflated size, then AES-256-CBC of a zlib stream
bool DecodeSii(std::string& b, std::string& error) {
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
std::string Hex(uint64_t v) { char b[32]; snprintf(b, sizeof b, "%llx", (unsigned long long)v); return b; }
// text form of a nameless id: _nameless.1bb.331b.7760 (16-bit groups, leading zero groups left out)
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
std::string Unquote(std::string v) { v = Trim(v); if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2); return v; }
std::string Quote(const std::string& v) { std::string r = "\""; for (char c : v) { if (c == '"' || c == '\\') r += '\\'; r += c; } return r + "\""; }
}

const SaveCompany* SaveGame::Company(const std::string& n) const { auto it = companyIndex_.find(n); return it == companyIndex_.end() ? nullptr : &companies[it->second]; }
const SaveOffer* SaveGame::Offer(const std::string& id) const { auto it = offerIndex_.find(id); return it == offerIndex_.end() ? nullptr : &offers[it->second]; }

bool SaveGame::Load(const std::string& fileBytes, std::string& error) {
    data_ = fileBytes;
    companies.clear(); offers.clear(); companyIndex_.clear(); offerIndex_.clear(); records_.clear(); blocks_.clear(); replaced_ = false;
    if (!DecodeSii(data_, error)) return false;
    bool ok;
    if (data_.compare(0, 4, "BSII") == 0) { binary_ = true; ok = LoadBinary(error); }
    else if (data_.compare(0, 8, "SiiNunit") == 0) { binary_ = false; ok = LoadText(error); }
    else { error = "unknown save format"; return false; }
    if (!ok) return false;
    for (auto& c : companies) for (auto& id : c.offers) { auto it = offerIndex_.find(id); if (it != offerIndex_.end()) offers[it->second].company = c.name; }
    if (offers.empty() || companies.empty()) { error = "no companies or job offers in the save"; return false; }
    return true;
}

// ---- text: "class : name {" ... " key: value" ... "}"
bool SaveGame::LoadText(std::string& error) {
    const std::string& d = data_;
    size_t pos = 0;
    std::string cls, name;
    SaveCompany comp; SaveOffer off; size_t blockBegin = 0;
    bool inUnit = false;
    while (pos < d.size()) {
        size_t eol = d.find('\n', pos);
        if (eol == std::string::npos) eol = d.size();
        std::string line = d.substr(pos, eol - pos);
        size_t lineBegin = pos;
        pos = eol + 1;
        std::string t = Trim(line);
        if (!inUnit) {
            size_t colon = t.find(" : ");
            if (colon != std::string::npos && !t.empty() && t.back() == '{') {
                cls = t.substr(0, colon); name = Trim(t.substr(colon + 3, t.size() - colon - 4));
                inUnit = true; blockBegin = lineBegin;
                comp = SaveCompany(); off = SaveOffer();
                if (cls == "company") { const std::string p = "company.volatile."; comp.name = name.rfind(p, 0) == 0 ? name.substr(p.size()) : name; }
                if (cls == "job_offer_data") off.id = name;
            }
            continue;
        }
        if (t == "}") {
            inUnit = false;
            if (cls == "company") { companyIndex_[comp.name] = companies.size(); companies.push_back(comp); }
            else if (cls == "job_offer_data") { blocks_[off.id] = Block{blockBegin, std::min(pos, d.size())}; offerIndex_[off.id] = offers.size(); offers.push_back(off); }
            continue;
        }
        size_t c = t.find(':');
        if (c == std::string::npos) continue;
        std::string key = t.substr(0, c), val = Trim(t.substr(c + 1));
        if (cls == "economy" && key == "game_time") gameTime = (unsigned)strtoul(val.c_str(), nullptr, 10);
        else if (cls == "company" && key.rfind("job_offer[", 0) == 0) comp.offers.push_back(val);
        else if (cls == "job_offer_data") {
            if (key == "target") off.target = Unquote(val);
            else if (key == "cargo") off.cargo = val;
            else if (key == "shortest_distance_km") off.distance = (unsigned)strtoul(val.c_str(), nullptr, 10);
            else if (key == "ferry_time") off.ferryTime = (unsigned)strtoul(val.c_str(), nullptr, 10);
            else if (key == "ferry_price") off.ferryPrice = (unsigned)strtoul(val.c_str(), nullptr, 10);
            else if (key == "expiration_time") off.expiration = (unsigned)strtoul(val.c_str(), nullptr, 10);
        }
    }
    (void)error;
    return true;
}

// ---- binary: structure definitions (id 0) and records (id = structure)
bool SaveGame::LoadBinary(std::string& error) {
    const std::string& d = data_;
    const uint8_t* b = (const uint8_t*)d.data();
    const size_t n = d.size();
    size_t p = 4;
    uint32_t version = 0;
    auto need = [&](size_t k) { if (p + k > n) throw std::runtime_error("unexpected end of file"); };
    auto u32 = [&]() { need(4); uint32_t v; memcpy(&v, b + p, 4); p += 4; return v; };
    auto u64 = [&]() { need(8); uint64_t v; memcpy(&v, b + p, 8); p += 8; return v; };
    auto str = [&]() { uint32_t k = u32(); need(k); std::string s((const char*)b + p, k); p += k; return s; };
    auto id = [&]() -> std::string {
        need(1); uint8_t parts = b[p++];
        if (parts == 0xFF) return Nameless(u64());
        std::string r;
        for (int i = 0; i < parts; ++i) { if (i) r += '.'; r += Token(u64()); }
        return r;
    };
    struct Def { std::string name; std::vector<std::pair<uint32_t, std::string>> fields; };
    std::map<uint32_t, Def> defs;
    // size of the simple types; arrays are the type + 1
    auto simple = [](uint32_t t) -> int {
        switch (t) {
        case 0x03: return 8; case 0x05: return 4; case 0x07: return 8; case 0x09: return 12; case 0x11: return 12; case 0x17: return 16;
        case 0x25: return 4; case 0x27: return 4; case 0x29: return 2; case 0x2B: return 2; case 0x2F: return 4; case 0x31: return 8;
        case 0x33: return 8; case 0x35: return 1; case 0x37: return 4;
        default: return 0;
        }
    };
    std::function<void(uint32_t)> skip = [&](uint32_t t) {
        switch (t) {
        case 0x01: str(); return;
        case 0x02: { uint32_t k = u32(); for (uint32_t i = 0; i < k; ++i) str(); return; }
        case 0x39: case 0x3B: case 0x3D: id(); return;
        case 0x3A: case 0x3C: case 0x3E: { uint32_t k = u32(); for (uint32_t i = 0; i < k; ++i) id(); return; }
        case 0x19: need(version >= 3 ? 32 : 32); p += 32; return;
        case 0x1A: { uint32_t k = u32(); need((size_t)k * 32); p += (size_t)k * 32; return; }
        default: break;
        }
        if (int s = simple(t)) { need(s); p += s; return; }
        if (t % 2 == 0) if (int s = simple(t - 1)) { uint32_t k = u32(); need((size_t)k * s); p += (size_t)k * s; return; }
        throw std::runtime_error("unknown value type 0x" + Hex(t) + " at byte " + std::to_string(p));
    };
    try {
        version = u32();
        if (version < 1 || version > 3) { error = "BSII version " + std::to_string(version) + " is not supported"; return false; }
        while (p < n) {
            size_t recBegin = p;
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
                    if (vt == 0x37) { uint32_t k = u32(); for (uint32_t i = 0; i < k; ++i) { u32(); str(); } }
                    def.fields.push_back({vt, fn});
                }
                defs[sid] = def;
                continue;
            }
            auto it = defs.find(t);
            if (it == defs.end()) throw std::runtime_error("record of unknown structure " + std::to_string(t));
            const Def& def = it->second;
            std::string name = id();
            Record rec; rec.sid = t; rec.begin = recBegin; rec.valuesBegin = p;
            const bool isOffer = def.name == "job_offer_data", isCompany = def.name == "company", isEconomy = def.name == "economy";
            SaveOffer off; SaveCompany comp;
            if (isOffer) off.id = name;
            if (isCompany) { const std::string pre = "company.volatile."; comp.name = name.rfind(pre, 0) == 0 ? name.substr(pre.size()) : name; }
            for (auto& f : def.fields) {
                size_t vb = p;
                if (isCompany && f.second == "job_offer" && (f.first == 0x3A || f.first == 0x3C || f.first == 0x3E)) {
                    uint32_t k = u32();
                    for (uint32_t i = 0; i < k; ++i) comp.offers.push_back(id());
                } else if (isOffer && f.second == "target" && f.first == 0x01) off.target = str();
                else if (isOffer && f.second == "cargo" && (f.first == 0x39 || f.first == 0x3B || f.first == 0x3D)) off.cargo = id();
                else if (isEconomy && f.second == "game_time" && (f.first == 0x27 || f.first == 0x25)) gameTime = u32();
                else skip(f.first);
                if (isOffer) {
                    rec.fields.push_back(Field{f.first, f.second, vb, p});
                    auto num = [&]() -> unsigned { uint32_t v = 0; memcpy(&v, b + vb, std::min<size_t>(4, p - vb)); if (p - vb == 2) v &= 0xFFFF; return v; };
                    if (f.second == "shortest_distance_km") off.distance = num();
                    else if (f.second == "ferry_time") off.ferryTime = num();
                    else if (f.second == "ferry_price") off.ferryPrice = num();
                    else if (f.second == "expiration_time") off.expiration = num();
                }
            }
            rec.end = p;
            if (isOffer) { records_[off.id] = rec; offerIndex_[off.id] = offers.size(); offers.push_back(off); }
            if (isCompany) { companyIndex_[comp.name] = companies.size(); companies.push_back(comp); }
        }
    } catch (const std::exception& e) {
        error = std::string("binary save: ") + e.what();
        return false;
    }
    return true;
}

bool SaveGame::Replace(const std::string& victimId, const std::string& templateId, const std::string& target,
                       unsigned distanceKm, unsigned ferryTime, unsigned ferryPrice, unsigned expiration, std::string& error) {
    if (replaced_) { error = "already changed - load the save again"; return false; }
    if (!binary_) {
        auto v = blocks_.find(victimId), t = blocks_.find(templateId);
        if (v == blocks_.end() || t == blocks_.end()) { error = "job offer not found"; return false; }
        // fields of the template's block by key (arrays: all lines of "key" and "key[i]")
        auto lines = [&](const Block& bl) {
            std::vector<std::pair<std::string, std::string>> r;   // base key, line
            std::istringstream in(data_.substr(bl.begin, bl.end - bl.begin));
            std::string l;
            while (std::getline(in, l)) {
                std::string t2 = Trim(l);
                size_t c = t2.find(':');
                if (t2.empty() || t2 == "}" || t2.back() == '{' || c == std::string::npos) continue;
                std::string key = t2.substr(0, c);
                size_t br = key.find('[');
                r.push_back({br == std::string::npos ? key : key.substr(0, br), l});
            }
            return r;
        };
        auto vl = lines(v->second), tl = lines(t->second);
        auto fromTemplate = [](const std::string& k) {
            return k == "cargo" || k == "company_truck" || k == "trailer_variant" || k == "trailer_definition" || k == "units_count" || k == "fill_ratio";
        };
        std::string head = data_.substr(v->second.begin, data_.find('\n', v->second.begin) - v->second.begin + 1);
        std::string out = head;
        std::vector<std::string> keys;   // the victim's keys in their order, once each
        for (auto& kv : vl) if (std::find(keys.begin(), keys.end(), kv.first) == keys.end()) keys.push_back(kv.first);
        for (const std::string& k : keys) {
            if (k == "target") out += " target: " + Quote(target) + "\n";
            else if (k == "expiration_time") out += " expiration_time: " + std::to_string(expiration) + "\n";
            else if (k == "urgency") out += " urgency: 0\n";
            else if (k == "shortest_distance_km") out += " shortest_distance_km: " + std::to_string(distanceKm) + "\n";
            else if (k == "ferry_time") out += " ferry_time: " + std::to_string(ferryTime) + "\n";
            else if (k == "ferry_price") out += " ferry_price: " + std::to_string(ferryPrice) + "\n";
            else { const auto& src = fromTemplate(k) ? tl : vl; for (auto& l2 : src) if (l2.first == k) out += l2.second + "\n"; }
        }
        out += "}\n";
        // the "}" line of the victim block ends with "\n" (blocks end after it)
        data_ = data_.substr(0, v->second.begin) + out + data_.substr(v->second.end);
        replaced_ = true;
        return true;
    }
    auto v = records_.find(victimId), t = records_.find(templateId);
    if (v == records_.end() || t == records_.end() || v->second.sid != t->second.sid) { error = "job offer not found"; return false; }
    const Record& vr = v->second;
    const Record& tr = t->second;
    std::string rec = data_.substr(vr.begin, vr.valuesBegin - vr.begin);   // structure id + unit id
    auto put = [&](uint32_t type, unsigned value) {
        if (type == 0x2B || type == 0x29) { uint16_t x = (uint16_t)std::min(value, 65535u); rec.append((const char*)&x, 2); }
        else { uint32_t x = value; rec.append((const char*)&x, 4); }
    };
    for (size_t i = 0; i < vr.fields.size(); ++i) {
        const Field& f = vr.fields[i];
        const Field& tf = tr.fields[i];
        const std::string& k = f.name;
        if (k == "target" && f.type == 0x01) { uint32_t len = (uint32_t)target.size(); rec.append((const char*)&len, 4); rec += target; }
        else if (k == "expiration_time") put(f.type, expiration);
        else if (k == "urgency") put(f.type, 0);
        else if (k == "shortest_distance_km") put(f.type, distanceKm);
        else if (k == "ferry_time") put(f.type, ferryTime);
        else if (k == "ferry_price") put(f.type, ferryPrice);
        else if (k == "cargo" || k == "company_truck" || k == "trailer_variant" || k == "trailer_definition" || k == "units_count" || k == "fill_ratio")
            rec += data_.substr(tf.begin, tf.end - tf.begin);
        else rec += data_.substr(f.begin, f.end - f.begin);
    }
    data_ = data_.substr(0, vr.begin) + rec + data_.substr(vr.end);
    replaced_ = true;
    return true;
}
