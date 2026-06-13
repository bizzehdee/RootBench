#pragma once
// Runtime colour palette system for the TUI.
// Three built-in themes: Dark (default), Light, High-contrast dark.

#include "UefiTypes.h"

struct Color {
    UINT8 B, G, R, Reserved;

    constexpr Color() : B(0), G(0), R(0), Reserved(0) {}
    constexpr Color(UINT8 r, UINT8 g, UINT8 b)
        : B(b), G(g), R(r), Reserved(0) {}

    constexpr bool operator==(const Color& o) const {
        return R == o.R && G == o.G && B == o.B;
    }
    constexpr bool operator!=(const Color& o) const { return !(*this == o); }

    UINT32 Pack() const {
        return (static_cast<UINT32>(R) << 16) |
               (static_cast<UINT32>(G) << 8)  |
               static_cast<UINT32>(B);
    }
};

// ── Palette ───────────────────────────────────────────────────

struct Palette {
    Color Background;
    Color HeaderBorder;
    Color HeaderText;
    Color Text;
    Color TextDim;
    Color Highlight;
    Color HighlightTxt;
    Color Accent;
    Color Success;
    Color Warning;
    Color Error;
    Color Separator;
    Color CheckMark;
    Color Footer;
};

// ── Built-in palettes ─────────────────────────────────────────

namespace ThemePalettes {

constexpr Palette kDark = {
    /*Background  */ { 10,  10,  28},
    /*HeaderBorder*/ {  0, 180, 216},
    /*HeaderText  */ {255, 255, 255},
    /*Text        */ {210, 210, 210},
    /*TextDim     */ {120, 120, 130},
    /*Highlight   */ {  0,  70,  90},
    /*HighlightTxt*/ {255, 255, 255},
    /*Accent      */ {  0, 200, 220},
    /*Success     */ { 80, 200,  80},
    /*Warning     */ {220, 180,   0},
    /*Error       */ {220,  60,  60},
    /*Separator   */ { 50,  50,  70},
    /*CheckMark   */ {  0, 220, 160},
    /*Footer      */ {180, 160,  60},
};

constexpr Palette kLight = {
    /*Background  */ {245, 245, 245},
    /*HeaderBorder*/ {  0, 120, 160},
    /*HeaderText  */ { 20,  20,  60},
    /*Text        */ { 30,  30,  30},
    /*TextDim     */ {100, 100, 110},
    /*Highlight   */ {180, 220, 240},
    /*HighlightTxt*/ { 20,  20,  60},
    /*Accent      */ {  0, 130, 160},
    /*Success     */ { 20, 140,  20},
    /*Warning     */ {170, 110,   0},
    /*Error       */ {180,  30,  30},
    /*Separator   */ {190, 190, 200},
    /*CheckMark   */ {  0, 150,  90},
    /*Footer      */ {110,  90,  10},
};

constexpr Palette kHighContrastDark = {
    /*Background  */ {  0,   0,   0},
    /*HeaderBorder*/ {255, 255,   0},
    /*HeaderText  */ {255, 255, 255},
    /*Text        */ {255, 255, 255},
    /*TextDim     */ {200, 200, 200},
    /*Highlight   */ {255, 255,   0},
    /*HighlightTxt*/ {  0,   0,   0},
    /*Accent      */ {  0, 255, 255},
    /*Success     */ {  0, 255,   0},
    /*Warning     */ {255, 200,   0},
    /*Error       */ {255,   0,   0},
    /*Separator   */ {128, 128, 128},
    /*CheckMark   */ {  0, 255, 180},
    /*Footer      */ {255, 200,   0},
};

} // namespace ThemePalettes

// ── Runtime theme selection ───────────────────────────────────

enum class ThemeId { Dark = 0, Light = 1, HighContrastDark = 2 };

namespace Theme {

inline ThemeId sActiveTheme = ThemeId::Dark;

inline const Palette& Current() {
    switch (sActiveTheme) {
        case ThemeId::Light:            return ThemePalettes::kLight;
        case ThemeId::HighContrastDark: return ThemePalettes::kHighContrastDark;
        default:                        return ThemePalettes::kDark;
    }
}

inline void     Set(ThemeId id) { sActiveTheme = id; }
inline ThemeId  CurrentId()     { return sActiveTheme; }
inline const char* CurrentName() {
    switch (sActiveTheme) {
        case ThemeId::Light:            return "Light";
        case ThemeId::HighContrastDark: return "High-contrast dark";
        default:                        return "Dark";
    }
}

} // namespace Theme
