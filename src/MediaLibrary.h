// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include "Common.h"

// Media types a music folder can be shown as (chip on the screen, picture when a track has no cover)
struct MediaTypeInfo { const char* id; const char* de; const char* en; };
const std::vector<MediaTypeInfo>& MediaTypes();          // usb, cd, sd, cassette, bluetooth, aux
std::string MediaTypeId(const std::string& nameOrId);    // "CD" / "cd" / "Kassette" -> id, unknown -> ""
std::wstring MediaTypeLabel(const std::string& id, bool english);

// Own music for the USB/CD mode: every audio file in realistic-radio\usb\ (including sub folders).
struct MediaTrack {
    std::wstring path;      // full path
    std::wstring rel;       // path relative to the usb folder (used to remember the last track)
    std::wstring title, artist, album;
};

// One USB stick / CD: a contiguous range of Tracks(). With folderPerDisc every sub folder of the usb folder is one
// disc (named after the folder) and files directly in the usb folder form an extra disc with an empty name.
struct MediaDisc {
    std::wstring name;
    int first = 0, count = 0;
};

class MediaLibrary {
public:
    // Creates the folder (with a short read-me) if it does not exist, then lists all supported files,
    // sorted like Windows Explorer sorts names ("2" before "10"): files of a folder first, then its sub folders.
    void Scan(const std::wstring& folder, bool folderPerDisc);
    const std::vector<MediaTrack>& Tracks() const { return tracks_; }
    const std::vector<MediaDisc>& Discs() const { return discs_; }
    int DiscOf(int track) const;                       // -1 = none
    int DiscByName(const std::wstring& name) const;    // -1 = none
    const std::wstring& Folder() const { return folder_; }
    int IndexOf(const std::wstring& rel) const;
    // Cover picture for a track: embedded ID3 picture (written once to cacheDir) or cover/folder/front.jpg|png
    // next to the file. Returns an image file path or an empty string.
    std::wstring CoverFor(const MediaTrack& t, const std::wstring& cacheDir) const;
private:
    std::wstring folder_;
    std::vector<MediaTrack> tracks_;
    std::vector<MediaDisc> discs_;
};
