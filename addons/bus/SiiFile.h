// Realistic Radio Bus+ (add-on)
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// A save of ETS2 / ATS (game.sii) as a list of units with their values - for reading and for changing single values.
// Formats: encrypted ("ScsC": AES-256-CBC + zlib), binary ("BSII", version 1-3) and text ("SiiNunit"). A changed file is
// written unencrypted in the format it came in (the game reads all of them). Values are kept as text: numbers in
// decimal, ids like "bus_stop.city.berlin" or "_nameless.20d.3828.7ac8", enums by name, strings without quotes.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct SiiField {
    std::string name;
    uint32_t type = 0;                 // binary value type (0 in text saves)
    bool array = false;
    std::vector<std::string> v;        // the value(s) as text
    size_t begin = 0, end = 0;         // binary: bytes of the value; text: the lines of the field
};

struct SiiUnit {
    std::string cls, id;
    uint32_t sid = 0;                  // binary: structure id
    size_t begin = 0, valuesBegin = 0, end = 0;   // binary: record; text: from the header line to after "}"
    std::vector<SiiField> fields;
    const SiiField* F(const std::string& name) const;
    std::string S(const std::string& name) const;                  // first value or ""
    uint32_t U(const std::string& name, uint32_t def = 0) const;    // first value as number
    double D(const std::string& name, double def = 0) const;
    std::vector<std::string> A(const std::string& name) const;      // all values
};

class SiiFile {
public:
    bool Load(std::string bytes, std::string& error);   // the bytes of game.sii (any format)
    bool Binary() const { return binary_; }
    const std::string& Bytes() const { return data_; }  // after Commit: the changed file

    const SiiUnit* Unit(const std::string& id) const;
    std::vector<const SiiUnit*> OfClass(const std::string& cls) const;
    const std::vector<SiiUnit>& Units() const { return units_; }

    // Changes a value (applied by Commit). quoted: a string (only matters for text saves).
    // An array is given as a vector, a single value as a string.
    void Set(const std::string& unitId, const std::string& field, const std::vector<std::string>& values, bool quoted = false);
    void Set(const std::string& unitId, const std::string& field, const std::string& value, bool quoted = false);
    bool Commit(std::string& error);   // rebuilds the file; the file must be loaded again to read the new values

private:
    struct Def { std::string name; std::vector<std::pair<uint32_t, std::string>> fields; std::vector<std::map<uint32_t, std::string>> enums; };
    struct Edit { std::vector<std::string> v; bool quoted = false, array = true; };
    bool LoadBinary(std::string& error);
    bool LoadText(std::string& error);
    std::string Encode(uint32_t type, const std::map<uint32_t, std::string>& enums, const Edit& e, std::string& error) const;
    std::string data_;
    bool binary_ = false;
    std::vector<SiiUnit> units_;
    std::map<std::string, size_t> index_;
    std::map<uint32_t, Def> defs_;
    std::map<std::string, std::map<std::string, Edit>> edits_;
};

// "ScsC" -> decrypted and inflated; other data unchanged. false = broken
bool SiiDecode(std::string& bytes, std::string& error);
