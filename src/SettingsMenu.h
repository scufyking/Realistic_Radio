// Realistic Radio
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include "Config.h"
#include "OverlayImage.h"
#include <functional>

class InputManager;

// one row of a settings page
struct MenuDef {
    MenuItemKind kind = MenuItemKind::Toggle;
    const char *de = "", *en = "", *hde = "", *hen = "";
    std::function<double(const Config&)> get;
    std::function<void(Config&, double)> set;
    double mn = 0, mx = 1, step = 1;
    std::function<std::wstring(double, bool)> fmt;
    std::vector<std::pair<const char*, const char*>> labels;   // choice
    bool restart = false;
    Action action = Action::Count;                              // key row
    std::wstring folder; int folderTracks = -1;                 // media type row of a music folder
    int command = 0;                                            // button row: SettingsMenu::Command
    const char *btnDe = "", *btnEn = "";
};

// In-game settings menu (default Ctrl+0) with mouse and keyboard: pages for radio, reception, navigation screen,
// pop-up overlay, USB/CD and keys. Changes take effect immediately (RadioManager applies Edited() whenever
// TakeChanged() is true); "Save" and closing the menu write them to realistic-radio.json.
class SettingsMenu {
public:
    enum class Result { Open, Closed };
    enum Command { None = 0, OpenMusicFolder, ClearLogos, OpenLogoFolder };
    struct Folder { std::wstring name; int tracks; };   // music folder = one USB / CD (name empty = main folder)
    void Open(const Config& cfg);
    bool IsOpen() const { return open_; }
    Result Tick(InputManager& input);       // ~20-60x per second while open
    void Close();                           // without saving (plugin shutdown)
    const Config& Edited() const { return edit_; }
    bool TakeChanged() { bool c = changed_; changed_ = false; return c; }     // values changed -> apply them
    bool TakeSaveRequest() { bool s = saveReq_; saveReq_ = false; return s; } // -> write the file, then MarkSaved()
    void MarkSaved();
    int TakeCommand() { int c = cmd_; cmd_ = None; return c; }   // button pressed -> RadioManager does it
    void SetFolders(const std::vector<Folder>& f);                 // music folders, each gets a media type row
    void SetCacheInfo(uint64_t bytes, int files);
    void SetStatus(const std::wstring& s) { status_ = s; dirty_ = true; }
    std::shared_ptr<OverlayImage> TakeImage();   // new picture when something on the menu changed, else nullptr
    int RenderHeight() const { return renderH_; }

    using Def = MenuDef;
private:
    MenuView View() const;
    std::wstring T(const char* de, const char* en) const;
    std::wstring KeyText(const KeyBinding& b) const;
    void Adjust(int row, int dir);          // keyboard / wheel / arrow buttons
    void Activate(int row);                 // Enter / click
    void SetSlider(int row, float frac);
    void BindKey(int row, const KeyBinding& b);
    void BindButton(int row, int button);
    void ClearBinding(int row);
    void Defaults();
    void Revert();
    void Edited_();                          // after every change
    void UpdateHover();
    int RowCount() const;
    bool KeysPage() const;
    bool IsKeyRow(int row) const;           // key binding row (Keys page, or a key row on another page)
    Action RowAction(int row) const;

    bool open_ = false, changed_ = false, saveReq_ = false, dirty_ = false, mouse_ = false;
    Config edit_, saved_;
    std::string savedJson_;
    int page_ = 0, sel_ = 0, hover_ = -1, hoverCtl_ = -1, capture_ = 0, drag_ = -1;
    std::wstring status_;
    std::vector<Def> defs_;                  // rows of the current page (static + generated)
    std::vector<Folder> folders_;
    uint64_t cacheBytes_ = 0; int cacheFiles_ = 0;
    int cmd_ = None, scroll_ = 0;
    void BuildDefs();
    void EnsureVisible();
    int VisibleRows() const;
    int renderH_ = 1080;
};
