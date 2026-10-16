// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include "Common.h"

// Own music for the USB/CD mode: every audio file in realistic-radio\usb\ (including sub folders).
struct MediaTrack {
    std::wstring path;      // full path
    std::wstring rel;       // path relative to the usb folder (used to remember the last track)
    std::wstring title, artist, album;
};

class MediaLibrary {
public:
    // Creates the folder (with a short read-me) if it does not exist, then lists all supported files,
    // sorted like Windows Explorer sorts names (folders first by name, "2" before "10").
    void Scan(const std::wstring& folder);
    const std::vector<MediaTrack>& Tracks() const { return tracks_; }
    const std::wstring& Folder() const { return folder_; }
    int IndexOf(const std::wstring& rel) const;
    // Cover picture for a track: embedded ID3 picture (written once to cacheDir) or cover/folder/front.jpg|png
    // next to the file. Returns an image file path or an empty string.
    std::wstring CoverFor(const MediaTrack& t, const std::wstring& cacheDir) const;
private:
    std::wstring folder_;
    std::vector<MediaTrack> tracks_;
};
