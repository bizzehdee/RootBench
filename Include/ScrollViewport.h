#pragma once
// Fixed-capacity scrollable line buffer for TUI screens.
// Collect content into the viewport with AddLine(), then call Render() + HandleKey()
// in a loop to provide scrollable display with Up/Down/PageUp/PageDown/Home/End.

#include "UefiTypes.h"
#include "ColorTheme.h"

class ScrollViewport {
public:
    static constexpr int MAX_LINES = 220;
    static constexpr int MAX_WIDTH = 108;

    void Clear();

    // Append a line with explicit colour.
    void AddLine(const char* text, Color color);

    // Append a blank line using the default text colour.
    void AddLine();

    // Append a separator row of '-' characters (TextDim colour).
    void AddSeparator();

    // Returns the total number of lines stored.
    int TotalLines() const { return mCount; }

    // Draw the visible window starting at `startRow` on screen.
    // Draws exactly `viewRows` rows (or fewer if the buffer is smaller).
    void Render(int startRow, int viewRows) const;

    // Process a key event. Returns true if the viewport consumed the key
    // (i.e. the key was a scroll command).
    bool HandleKey(EFI_INPUT_KEY key, int viewRows);

    // Current scroll position (first visible line index).
    int ScrollPos() const { return mScrollPos; }

private:
    struct Line {
        char  text[MAX_WIDTH];
        Color color;
    };

    Line mLines[MAX_LINES];
    int  mCount     = 0;
    int  mScrollPos = 0;

    void ClampScroll(int viewRows);
};
