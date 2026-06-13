#pragma once
// Fixed-capacity scrollable line buffer for TUI screens.
// Collect content into the viewport with AddLine(), then call Render() + HandleKey()
// in a loop to provide scrollable display with Up/Down/PageUp/PageDown/Home/End.
//
// For interactive screens that rebuild every frame:
//   ClearContent()     — resets line count without touching scroll position
//   ScrollToLine()     — adjusts scroll so a given line stays visible (cursor-following)
//
// For highlighted rows (e.g. selected items): use the AddLine(text, fg, bg) overload.
// Render() uses DrawTextBg for those lines so the background fill is correct.

#include "UefiTypes.h"
#include "ColorTheme.h"

class ScrollViewport {
public:
    static constexpr int MAX_LINES = 256;
    static constexpr int MAX_WIDTH = 108;

    // Reset everything including scroll position (use at start of a new screen).
    void Clear();

    // Reset only the line buffer; preserves scroll position.
    // Use when rebuilding content every frame for interactive lists.
    void ClearContent();

    // Append a line with explicit foreground colour (no background fill).
    void AddLine(const char* text, Color fg);

    // Append a line with both foreground and background colours.
    // Render() will call DrawTextBg for this line.
    void AddLine(const char* text, Color fg, Color bg);

    // Append a blank line using the default text colour.
    void AddLine();

    // Append a separator row of '-' characters (Separator colour).
    void AddSeparator();

    // Returns the total number of lines stored.
    int TotalLines() const { return mCount; }

    // Draw the visible window starting at `startRow` on screen.
    // Draws exactly `viewRows` rows (clears trailing empty rows too).
    void Render(int startRow, int viewRows) const;

    // Process a key event. Returns true if the viewport consumed the key
    // (i.e. the key was a scroll command).
    bool HandleKey(EFI_INPUT_KEY key, int viewRows);

    // Adjust scroll so that `line` (a line index) is visible inside the
    // `viewRows`-tall window. Used for cursor-following in interactive lists.
    void ScrollToLine(int line, int viewRows);

    // Current scroll position (index of the topmost visible line).
    int ScrollPos() const { return mScrollPos; }

private:
    struct Line {
        char  text[MAX_WIDTH];
        Color fg;
        Color bg;
        bool  hasBg;   // if true Render() uses DrawTextBg, else DrawText
    };

    Line mLines[MAX_LINES];
    int  mCount     = 0;
    int  mScrollPos = 0;

    void ClampScroll(int viewRows);
};
