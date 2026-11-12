// Realistic Radio Jobs+ (add-on)
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// A save of ETS2 / ATS (game.sii): reads the companies and their job offers and replaces one offer by an own job.
// Formats: encrypted ("ScsC": AES-256-CBC + zlib), binary ("BSII", version 1-3) and text ("SiiNunit"). The changed file
// is written unencrypted in the format it came in (the game reads all of them).
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct SaveOffer {
    std::string id;                    // unit name of the job_offer_data
    std::string target;                // "company.city"
    std::string cargo;                 // "cargo.furniture"
    unsigned distance = 0, ferryTime = 0, ferryPrice = 0, expiration = 0;
    std::string company;               // "company.city" that offers it
};
struct SaveCompany {
    std::string name;                  // "tradeaux.berlin"
    std::vector<std::string> offers;   // ids of its job_offer_data
};

class SaveGame {
public:
    bool Load(const std::string& fileBytes, std::string& error);   // the bytes of game.sii
    const std::string& Bytes() const { return data_; }             // after Replace: the changed file
    bool Binary() const { return binary_; }

    unsigned gameTime = 0;
    std::vector<SaveCompany> companies;
    std::vector<SaveOffer> offers;
    const SaveCompany* Company(const std::string& name) const;
    const SaveOffer* Offer(const std::string& id) const;

    // Replaces the offer `victimId` (one of the source company's offers) by cargo, trailer and units of the offer
    // `templateId` (an offer with the wanted cargo), going to `target`. Only one replacement per loaded file.
    bool Replace(const std::string& victimId, const std::string& templateId, const std::string& target,
                 unsigned distanceKm, unsigned ferryTime, unsigned ferryPrice, unsigned expiration, std::string& error);

private:
    struct Field { uint32_t type = 0; std::string name; size_t begin = 0, end = 0; };   // binary: bytes of one value
    struct Record { uint32_t sid = 0; size_t begin = 0, end = 0, valuesBegin = 0; std::vector<Field> fields; };   // binary unit
    struct Block { size_t begin = 0, end = 0; };   // text unit: from its first line to after "}"
    bool LoadText(std::string& error);
    bool LoadBinary(std::string& error);
    std::string data_;
    bool binary_ = false, replaced_ = false;
    std::map<std::string, size_t> companyIndex_, offerIndex_;
    std::map<std::string, Record> records_;   // binary: job_offer_data records by id
    std::map<std::string, Block> blocks_;     // text: job_offer_data blocks by id
};

// "ScsC" -> decrypted and inflated; other data unchanged. false = broken
bool DecodeSii(std::string& bytes, std::string& error);
