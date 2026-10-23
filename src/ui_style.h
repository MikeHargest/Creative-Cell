#pragma once

#include <imgui.h>

// The look: monochrome — near-black surfaces in a few steps, grey text,
// white only for what's selected or pressed. Sizes are in pixels at 100 %
// and scaled by the screen's scale (ui::s()).
namespace ui {

namespace col {
constexpr ImU32 bg0 = IM_COL32(10, 10, 10, 255);     // window, grid area
constexpr ImU32 bg1 = IM_COL32(15, 15, 15, 255);     // side panels, bars
constexpr ImU32 bg2 = IM_COL32(22, 22, 22, 255);     // tiles, fields, cards
constexpr ImU32 bg3 = IM_COL32(30, 30, 30, 255);     // hover
constexpr ImU32 bg4 = IM_COL32(40, 40, 40, 255);     // pressed, strong border
constexpr ImU32 line = IM_COL32(32, 32, 32, 255);    // separators, outlines
constexpr ImU32 text = IM_COL32(232, 232, 232, 255);
constexpr ImU32 text2 = IM_COL32(176, 176, 176, 255);  // secondary
constexpr ImU32 text3 = IM_COL32(124, 124, 124, 255);  // hints, disabled
constexpr ImU32 accent = IM_COL32(250, 250, 250, 255);  // selection
constexpr ImU32 warn = IM_COL32(200, 200, 200, 255);    // (still grey: a sign, not a color)
}  // namespace col

struct Fonts {
  ImFont* sans = nullptr;
  ImFont* mono = nullptr;
};
extern Fonts fonts;

// Screen scale (1 at 100 %).
float s();
void setScale(float scale);
inline float px(float v) { return v * s(); }

void loadFonts();
void applyStyle();

// Sizes of the fonts in use (scaled).
float textSize();   // body
float smallSize();  // labels, meta
float monoSize();   // numbers

enum class Icon {
  Search, Grid, List, Folder, FolderPlus, Collection, Plus, Gear, User, Store, Clock, Box, Warning, Trash,
  External, Reveal, Close, ChevronLeft, ChevronRight, ChevronDown, ChevronUp, Frame, Refresh, Import, Copy, Dots, Layers, Check, PanelLeft, PanelRight, Expand, Collapse
};
void drawIcon(ImDrawList* dl, Icon icon, ImVec2 center, float size, ImU32 color);

// A square button with an icon; `on` keeps it lit.
bool iconButton(const char* id, Icon icon, float size, bool on = false, const char* tip = nullptr,
                bool enabled = true);
// A small toggle with a label, for rows of options ("ALL FBX OBJ").
bool chip(const char* label, bool on, float height = 0.0f);
// A button: `primary` — white with dark text (one per screen: the main action).
bool button(const char* label, bool primary = false, float width = 0.0f, Icon icon = Icon::Check, bool withIcon = false);
// Grey capitals over a section; `right` — an icon button at its end (returns
// true when clicked).
bool sectionHeader(const char* label, Icon right = Icon::Plus, bool withButton = false, const char* tip = nullptr);
// The same with a chevron: a click on the row folds / unfolds `open`.
bool foldHeader(const char* label, bool& open, Icon right = Icon::Plus, bool withButton = false,
                const char* tip = nullptr);
// Text cut with "…" to fit `width`.
void textClipped(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 color, const char* text, float width);
// Monospace text in the current window.
void mono(const char* fmt, ...);
void tooltip(const char* text);

// A fixed area of the window (no title, not movable), filled with `bg`.
void beginPanel(const char* id, ImVec2 pos, ImVec2 size, ImU32 bg, ImVec2 padding = ImVec2(0, 0),
                ImGuiWindowFlags extra = 0);
void endPanel();

}  // namespace ui
