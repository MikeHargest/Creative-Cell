#include "ui_style.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include "util.h"

namespace ui {

Fonts fonts;

namespace {
float g_scale = 1.0f;

ImVec4 v4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

std::string fontPath(const char* name) {
  std::error_code ec;
  const std::filesystem::path installed = exeDir() / "assets" / "fonts" / name;
  if (std::filesystem::exists(installed, ec)) return pathU8(installed);
  return std::string(CC_ASSETS_DIR "/fonts/") + name;
}
}  // namespace

float s() { return g_scale; }
void setScale(float scale) { g_scale = scale > 0.5f ? scale : 1.0f; }

float textSize() { return px(14.0f); }
float smallSize() { return px(12.0f); }
float monoSize() { return px(12.5f); }

void loadFonts() {
  ImGuiIO& io = ImGui::GetIO();
  ImFontConfig cfg;
  cfg.OversampleH = 2;
  fonts.sans = io.Fonts->AddFontFromFileTTF(fontPath("Inter.ttf").c_str(), textSize(), &cfg);
  if (!fonts.sans) fonts.sans = io.Fonts->AddFontDefault();
  fonts.mono = io.Fonts->AddFontFromFileTTF(fontPath("JetBrainsMono-Regular.ttf").c_str(), monoSize(), &cfg);
  if (!fonts.mono) fonts.mono = fonts.sans;
  io.FontDefault = fonts.sans;
}

void applyStyle() {
  ImGuiStyle& st = ImGui::GetStyle();
  st = ImGuiStyle();
  st.FontSizeBase = textSize();
  st.WindowRounding = 0.0f;
  st.ChildRounding = px(6);
  st.FrameRounding = px(5);
  st.PopupRounding = px(6);
  st.GrabRounding = px(4);
  st.ScrollbarRounding = px(4);
  st.TabRounding = px(4);
  st.WindowBorderSize = 0.0f;
  st.PopupBorderSize = 1.0f;
  st.FrameBorderSize = 0.0f;
  st.WindowPadding = ImVec2(px(12), px(12));
  st.FramePadding = ImVec2(px(8), px(5));
  st.ItemSpacing = ImVec2(px(8), px(6));
  st.ItemInnerSpacing = ImVec2(px(6), px(4));
  st.IndentSpacing = px(14);
  st.ScrollbarSize = px(10);
  st.GrabMinSize = px(10);
  st.SelectableTextAlign = ImVec2(0.0f, 0.5f);

  ImVec4* c = st.Colors;
  c[ImGuiCol_Text] = v4(col::text);
  c[ImGuiCol_TextDisabled] = v4(col::text3);
  c[ImGuiCol_WindowBg] = v4(col::bg1);
  c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_PopupBg] = v4(IM_COL32(18, 18, 18, 250));
  c[ImGuiCol_Border] = v4(col::line);
  c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_FrameBg] = v4(col::bg2);
  c[ImGuiCol_FrameBgHovered] = v4(col::bg3);
  c[ImGuiCol_FrameBgActive] = v4(col::bg4);
  c[ImGuiCol_TitleBg] = v4(col::bg1);
  c[ImGuiCol_TitleBgActive] = v4(col::bg1);
  c[ImGuiCol_TitleBgCollapsed] = v4(col::bg1);
  c[ImGuiCol_MenuBarBg] = v4(col::bg1);
  c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ScrollbarGrab] = v4(IM_COL32(48, 48, 48, 255));
  c[ImGuiCol_ScrollbarGrabHovered] = v4(IM_COL32(70, 70, 70, 255));
  c[ImGuiCol_ScrollbarGrabActive] = v4(IM_COL32(90, 90, 90, 255));
  c[ImGuiCol_CheckMark] = v4(col::accent);
  c[ImGuiCol_SliderGrab] = v4(IM_COL32(120, 120, 120, 255));
  c[ImGuiCol_SliderGrabActive] = v4(col::accent);
  c[ImGuiCol_Button] = v4(col::bg2);
  c[ImGuiCol_ButtonHovered] = v4(col::bg3);
  c[ImGuiCol_ButtonActive] = v4(col::bg4);
  c[ImGuiCol_Header] = v4(col::bg3);
  c[ImGuiCol_HeaderHovered] = v4(IM_COL32(26, 26, 26, 255));
  c[ImGuiCol_HeaderActive] = v4(col::bg4);
  c[ImGuiCol_Separator] = v4(col::line);
  c[ImGuiCol_SeparatorHovered] = v4(IM_COL32(90, 90, 90, 255));
  c[ImGuiCol_SeparatorActive] = v4(col::accent);
  c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ResizeGripHovered] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ResizeGripActive] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_TableHeaderBg] = v4(col::bg1);
  c[ImGuiCol_TableBorderStrong] = v4(col::line);
  c[ImGuiCol_TableBorderLight] = v4(col::line);
  c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_TableRowBgAlt] = v4(IM_COL32(255, 255, 255, 6));
  c[ImGuiCol_TextSelectedBg] = v4(IM_COL32(255, 255, 255, 50));
  c[ImGuiCol_DragDropTarget] = v4(col::accent);
  c[ImGuiCol_NavCursor] = v4(IM_COL32(255, 255, 255, 120));
  c[ImGuiCol_ModalWindowDimBg] = v4(IM_COL32(0, 0, 0, 150));
}

// --- icons: drawn as lines in a unit box, so they stay sharp at any size ---
void drawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float size, ImU32 color) {
  const float h = size * 0.5f;
  const float t = std::max(1.0f, size / 12.0f);  // stroke
  auto P = [&](float x, float y) { return ImVec2(c.x + x * h, c.y + y * h); };  // -1..1 box
  auto L = [&](float x0, float y0, float x1, float y1) { dl->AddLine(P(x0, y0), P(x1, y1), color, t); };
  auto R = [&](float x0, float y0, float x1, float y1, float r = 0.0f) {
    dl->AddRect(P(x0, y0), P(x1, y1), color, r * h, t);
  };
  switch (icon) {
    case Icon::Search:
      dl->AddCircle(P(-0.15f, -0.15f), h * 0.55f, color, 0, t);
      L(0.25f, 0.25f, 0.8f, 0.8f);
      break;
    case Icon::Grid:
      R(-0.8f, -0.8f, -0.1f, -0.1f, 0.1f);
      R(0.1f, -0.8f, 0.8f, -0.1f, 0.1f);
      R(-0.8f, 0.1f, -0.1f, 0.8f, 0.1f);
      R(0.1f, 0.1f, 0.8f, 0.8f, 0.1f);
      break;
    case Icon::List:
      for (float y : {-0.6f, 0.0f, 0.6f}) {
        dl->AddCircleFilled(P(-0.7f, y), t * 0.9f, color);
        L(-0.35f, y, 0.8f, y);
      }
      break;
    case Icon::Folder:
    case Icon::FolderPlus: {
      const ImVec2 pts[] = {P(-0.85f, -0.6f), P(-0.25f, -0.6f), P(-0.05f, -0.4f), P(0.85f, -0.4f),
                            P(0.85f, 0.65f),  P(-0.85f, 0.65f)};
      dl->AddPolyline(pts, 6, color, t, ImDrawFlags_Closed);
      if (icon == Icon::FolderPlus) {
        L(-0.25f, 0.13f, 0.35f, 0.13f);
        L(0.05f, -0.17f, 0.05f, 0.43f);
      }
      break;
    }
    case Icon::Collection:
      R(-0.75f, -0.35f, 0.75f, 0.75f, 0.12f);
      L(-0.55f, -0.58f, 0.55f, -0.58f);
      L(-0.35f, -0.8f, 0.35f, -0.8f);
      break;
    case Icon::Plus:
      L(-0.7f, 0, 0.7f, 0);
      L(0, -0.7f, 0, 0.7f);
      break;
    case Icon::Gear: {
      dl->AddCircle(c, h * 0.3f, color, 0, t);
      const int teeth = 8;
      for (int i = 0; i < teeth; ++i) {
        const float a = i * 6.2831853f / teeth;
        const float ca = std::cos(a), sa = std::sin(a);
        L(ca * 0.55f, sa * 0.55f, ca * 0.85f, sa * 0.85f);
      }
      dl->AddCircle(c, h * 0.58f, color, 0, t);
      break;
    }
    case Icon::User:
      dl->AddCircle(P(0, -0.3f), h * 0.32f, color, 0, t);
      dl->PathArcTo(P(0, 0.85f), h * 0.7f, 3.14159f * 1.05f, 3.14159f * 1.95f);
      dl->PathStroke(color, t);
      break;
    case Icon::Store: {
      R(-0.7f, -0.05f, 0.7f, 0.8f);
      const ImVec2 roof[] = {P(-0.85f, -0.05f), P(-0.65f, -0.75f), P(0.65f, -0.75f), P(0.85f, -0.05f)};
      dl->AddPolyline(roof, 4, color, t);
      R(-0.2f, 0.3f, 0.2f, 0.8f);
      break;
    }
    case Icon::Clock:
      dl->AddCircle(c, h * 0.8f, color, 0, t);
      L(0, 0, 0, -0.5f);
      L(0, 0, 0.35f, 0.2f);
      break;
    case Icon::Box: {
      // An isometric cube: the app's "model".
      const ImVec2 top = P(0, -0.85f), l = P(-0.75f, -0.45f), r = P(0.75f, -0.45f), m = P(0, -0.05f),
                   bl = P(-0.75f, 0.45f), br = P(0.75f, 0.45f), b = P(0, 0.85f);
      const ImVec2 outline[] = {top, r, br, b, bl, l};
      dl->AddPolyline(outline, 6, color, t, ImDrawFlags_Closed);
      dl->AddLine(l, m, color, t);
      dl->AddLine(r, m, color, t);
      dl->AddLine(m, b, color, t);
      break;
    }
    case Icon::Warning: {
      const ImVec2 tri[] = {P(0, -0.8f), P(0.85f, 0.7f), P(-0.85f, 0.7f)};
      dl->AddPolyline(tri, 3, color, t, ImDrawFlags_Closed);
      L(0, -0.25f, 0, 0.2f);
      dl->AddCircleFilled(P(0, 0.45f), t * 0.8f, color);
      break;
    }
    case Icon::Trash:
      L(-0.8f, -0.55f, 0.8f, -0.55f);
      L(-0.25f, -0.8f, 0.25f, -0.8f);
      {
        const ImVec2 can[] = {P(-0.6f, -0.55f), P(-0.5f, 0.8f), P(0.5f, 0.8f), P(0.6f, -0.55f)};
        dl->AddPolyline(can, 4, color, t);
      }
      break;
    case Icon::External:
      R(-0.75f, -0.45f, 0.45f, 0.75f, 0.1f);
      L(-0.05f, 0.05f, 0.8f, -0.8f);
      L(0.25f, -0.8f, 0.8f, -0.8f);
      L(0.8f, -0.8f, 0.8f, -0.25f);
      break;
    case Icon::Reveal: {
      const ImVec2 pts[] = {P(-0.85f, -0.6f), P(-0.25f, -0.6f), P(-0.05f, -0.4f), P(0.85f, -0.4f),
                            P(0.85f, 0.65f),  P(-0.85f, 0.65f)};
      dl->AddPolyline(pts, 6, color, t, ImDrawFlags_Closed);
      L(-0.3f, 0.15f, 0.35f, 0.15f);
      L(0.1f, -0.1f, 0.35f, 0.15f);
      L(0.1f, 0.4f, 0.35f, 0.15f);
      break;
    }
    case Icon::Close:
      L(-0.6f, -0.6f, 0.6f, 0.6f);
      L(0.6f, -0.6f, -0.6f, 0.6f);
      break;
    case Icon::ChevronLeft:
      L(0.25f, -0.55f, -0.3f, 0);
      L(-0.3f, 0, 0.25f, 0.55f);
      break;
    case Icon::ChevronRight:
      L(-0.25f, -0.55f, 0.3f, 0);
      L(0.3f, 0, -0.25f, 0.55f);
      break;
    case Icon::ChevronDown:
      L(-0.55f, -0.25f, 0, 0.3f);
      L(0, 0.3f, 0.55f, -0.25f);
      break;
    case Icon::Frame:
      for (int sx : {-1, 1}) {
        for (int sy : {-1, 1}) {
          L(sx * 0.8f, sy * 0.8f, sx * 0.35f, sy * 0.8f);
          L(sx * 0.8f, sy * 0.8f, sx * 0.8f, sy * 0.35f);
        }
      }
      dl->AddCircleFilled(c, t * 1.3f, color);
      break;
    case Icon::Refresh:
      dl->PathArcTo(c, h * 0.7f, 0.5f, 5.6f);
      dl->PathStroke(color, t);
      L(0.62f, -0.55f, 0.62f, -0.05f);
      L(0.62f, -0.05f, 0.15f, -0.1f);
      break;
    case Icon::Import:
      L(0, -0.85f, 0, 0.35f);
      L(-0.4f, -0.05f, 0, 0.35f);
      L(0.4f, -0.05f, 0, 0.35f);
      L(-0.8f, 0.45f, -0.8f, 0.8f);
      L(-0.8f, 0.8f, 0.8f, 0.8f);
      L(0.8f, 0.8f, 0.8f, 0.45f);
      break;
    case Icon::Copy:
      R(-0.75f, -0.4f, 0.35f, 0.8f, 0.12f);
      L(-0.4f, -0.8f, 0.75f, -0.8f);
      L(0.75f, -0.8f, 0.75f, 0.4f);
      break;
    case Icon::Dots:
      for (float x : {-0.6f, 0.0f, 0.6f}) dl->AddCircleFilled(P(x, 0), t * 1.1f, color);
      break;
    case Icon::Layers: {
      const ImVec2 a[] = {P(0, -0.8f), P(0.85f, -0.35f), P(0, 0.1f), P(-0.85f, -0.35f)};
      dl->AddPolyline(a, 4, color, t, ImDrawFlags_Closed);
      const ImVec2 b[] = {P(-0.85f, 0.1f), P(0, 0.55f), P(0.85f, 0.1f)};
      dl->AddPolyline(b, 3, color, t);
      break;
    }
    case Icon::PanelLeft:
    case Icon::PanelRight: {
      // A window with its side panel as a filled strip.
      R(-0.85f, -0.7f, 0.85f, 0.7f, 0.15f);
      const float x0 = icon == Icon::PanelLeft ? -0.85f : 0.3f, x1 = icon == Icon::PanelLeft ? -0.3f : 0.85f;
      dl->AddRectFilled(P(x0, -0.7f), P(x1, 0.7f), color, 0.15f * h,
                        icon == Icon::PanelLeft ? ImDrawFlags_RoundCornersLeft : ImDrawFlags_RoundCornersRight);
      break;
    }
    case Icon::Expand:
    case Icon::Collapse: {
      // Four corner arrows: out to the corners (expand) or in (collapse).
      const bool out = icon == Icon::Expand;
      for (int sx : {-1, 1}) {
        for (int sy : {-1, 1}) {
          const float tip = out ? 0.8f : 0.2f, base = out ? 0.2f : 0.8f;
          L(sx * base, sy * base, sx * tip, sy * tip);
          const float c = out ? 0.8f : 0.2f, d = out ? -0.4f : 0.4f;
          L(sx * c, sy * c, sx * (c + d), sy * c);
          L(sx * c, sy * c, sx * c, sy * (c + d));
        }
      }
      break;
    }
    case Icon::Check:
      L(-0.7f, 0.0f, -0.2f, 0.5f);
      L(-0.2f, 0.5f, 0.7f, -0.5f);
      break;
  }
}

bool iconButton(const char* id, Icon icon, float size, bool on, const char* tip, bool enabled) {
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::BeginDisabled(!enabled);
  const bool clicked = ImGui::InvisibleButton(id, ImVec2(size, size));
  ImGui::EndDisabled();
  const bool hov = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
  const bool held = ImGui::IsItemActive();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (on || (enabled && (hov || held))) {
    dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), on || held ? col::bg4 : col::bg3, px(5));
  }
  const ImU32 c = !enabled ? col::text3 : (on || hov) ? col::text : col::text2;
  drawIcon(dl, icon, ImVec2(pos.x + size * 0.5f, pos.y + size * 0.5f), size * 0.5f, c);
  if (tip && hov) tooltip(tip);
  return clicked && enabled;
}

bool chip(const char* label, bool on, float height) {
  ImFont* f = fonts.sans;
  const float fs = smallSize();
  const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, label, ImGui::FindRenderedTextEnd(label));
  const float h = height > 0.0f ? height : px(24);
  const ImVec2 size(ts.x + px(16), h);
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const bool clicked = ImGui::InvisibleButton(label, size);
  const bool hov = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 end(pos.x + size.x, pos.y + size.y);
  if (on) {
    dl->AddRectFilled(pos, end, col::bg4, px(5));
  } else if (hov) {
    dl->AddRectFilled(pos, end, col::bg3, px(5));
  }
  dl->AddText(f, fs, ImVec2(pos.x + px(8), pos.y + (h - ts.y) * 0.5f), on ? col::text : hov ? col::text : col::text2,
              label, ImGui::FindRenderedTextEnd(label));
  return clicked;
}

bool button(const char* label, bool primary, float width, Icon icon, bool withIcon) {
  ImFont* f = fonts.sans;
  const float fs = textSize();
  const char* end = ImGui::FindRenderedTextEnd(label);
  const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, label, end);
  const float iconW = withIcon ? px(20) : 0.0f;
  const float h = px(30);
  const ImVec2 size(width > 0.0f ? width : ts.x + iconW + px(24), h);
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const bool clicked = ImGui::InvisibleButton(label, size);
  const bool hov = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 e(pos.x + size.x, pos.y + size.y);
  ImU32 bg, fg;
  if (primary) {
    bg = held ? IM_COL32(200, 200, 200, 255) : hov ? IM_COL32(255, 255, 255, 255) : IM_COL32(236, 236, 236, 255);
    fg = IM_COL32(12, 12, 12, 255);
  } else {
    bg = held ? col::bg4 : hov ? col::bg3 : col::bg2;
    fg = col::text;
  }
  dl->AddRectFilled(pos, e, bg, px(5));
  if (!primary) dl->AddRect(pos, e, col::line, px(5));
  const float contentW = ts.x + iconW;
  float x = pos.x + (size.x - contentW) * 0.5f;
  if (withIcon) {
    drawIcon(dl, icon, ImVec2(x + px(7), pos.y + h * 0.5f), px(13), fg);
    x += iconW;
  }
  dl->AddText(f, fs, ImVec2(x, pos.y + (h - ts.y) * 0.5f), fg, label, end);
  return clicked;
}

bool sectionHeader(const char* label, Icon right, bool withButton, const char* tip) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float w = ImGui::GetContentRegionAvail().x;
  const float h = px(26);
  ImGui::Dummy(ImVec2(w - (withButton ? px(22) : 0.0f), h));
  const float fs = px(11);
  dl->AddText(fonts.sans, fs, ImVec2(pos.x + px(4), pos.y + (h - fs) * 0.5f), col::text3, label);
  bool clicked = false;
  if (withButton) {
    ImGui::SameLine(0, 0);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + w - px(22), pos.y + (h - px(22)) * 0.5f));
    ImGui::PushID(label);
    clicked = iconButton("##hdr", right, px(22), false, tip);
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + h));
    ImGui::Dummy(ImVec2(0, 0));
  }
  return clicked;
}

void textClipped(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 color, const char* text, float width) {
  const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
  if (ts.x <= width) {
    dl->AddText(font, size, pos, color, text);
    return;
  }
  const char* ell = "\xE2\x80\xA6";  // …
  const float ellW = font->CalcTextSizeA(size, FLT_MAX, 0.0f, ell).x;
  const char* end = text + std::strlen(text);
  // Shortest cut that fits (UTF-8 safe: only cut at lead bytes).
  const char* cut = text;
  for (const char* p = text; p < end;) {
    const char* next = p + 1;
    while (next < end && (static_cast<unsigned char>(*next) & 0xC0) == 0x80) ++next;
    if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, text, next).x + ellW > width) break;
    cut = next;
    p = next;
  }
  dl->AddText(font, size, pos, color, text, cut);
  const float cw = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text, cut).x;
  dl->AddText(font, size, ImVec2(pos.x + cw, pos.y), color, ell);
}

void mono(const char* fmt, ...) {
  char buf[512];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);
  ImGui::PushFont(fonts.mono, monoSize());
  ImGui::TextUnformatted(buf);
  ImGui::PopFont();
}

void tooltip(const char* text) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(8), px(5)));
  ImGui::SetTooltip("%s", text);
  ImGui::PopStyleVar();
}

void beginPanel(const char* id, ImVec2 pos, ImVec2 size, ImU32 bg, ImVec2 padding, ImGuiWindowFlags extra) {
  ImGui::SetNextWindowPos(pos);
  ImGui::SetNextWindowSize(size);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, bg);
  ImGui::Begin(id, nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing | extra);
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
}

void endPanel() { ImGui::End(); }

}  // namespace ui
