// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
// Makes a forward-only network stream look seekable for decoder initialisation: the first `cap` bytes are kept,
// so the decoder may jump back (dr_mp3 reads 10 bytes to look for an ID3 tag and then seeks back to 0).
#include <vector>
#include <functional>
#include <cstring>
#include <cstdint>
#include <algorithm>

class NetReader {
public:
    using Source = std::function<size_t(void*, size_t)>;   // returns 0 at end/error
    explicit NetReader(Source src, size_t cap = 512 * 1024) : src_(std::move(src)), cap_(cap) {}
    size_t Read(void* out, size_t n) {
        size_t total = 0;
        while (total < n) {
            if (pos_ < net_) {   // replay from retained history
                if (pos_ >= hist_.size()) break;   // outside the retained window (not expected)
                size_t k = std::min(n - total, (size_t)(hist_.size() - pos_));
                memcpy((char*)out + total, hist_.data() + pos_, k);
                pos_ += k; total += k; continue;
            }
            size_t got = src_((char*)out + total, n - total);
            if (!got) break;
            if (retaining_) {
                if (hist_.size() + got <= cap_) hist_.insert(hist_.end(), (char*)out + total, (char*)out + total + got);
                else retaining_ = false;
            }
            net_ += got; pos_ += got; total += got;
        }
        return total;
    }
    // origin: 0 = start, 1 = current, 2 = end (unsupported)
    bool Seek(int64_t off, int origin) {
        int64_t target;
        if (origin == 0) target = off; else if (origin == 1) target = (int64_t)pos_ + off; else return false;
        if (target < 0) return false;
        if ((uint64_t)target <= net_) {
            if ((uint64_t)target < net_ && (uint64_t)target > hist_.size()) return false;
            pos_ = (uint64_t)target; return true;
        }
        pos_ = net_;
        char tmp[4096];
        while (pos_ < (uint64_t)target) {
            size_t want = (size_t)std::min<uint64_t>(sizeof(tmp), (uint64_t)target - pos_);
            if (Read(tmp, want) == 0) return false;
        }
        return true;
    }
    uint64_t Tell() const { return pos_; }
private:
    Source src_;
    size_t cap_;
    std::vector<char> hist_;
    uint64_t pos_ = 0, net_ = 0;
    bool retaining_ = true;
};
