// Scrollable line-buffer TUI widget.

#include "ScrollViewport.h"
#include "Renderer.h"
#include "ColorTheme.h"
#include "Freestanding.h"

void ScrollViewport::Clear() {
    mCount     = 0;
    mScrollPos = 0;
}

void ScrollViewport::ClearContent() {
    mCount = 0;
    // mScrollPos intentionally preserved — caller adjusts with ScrollToLine()
}

void ScrollViewport::AddLine(const char* text, Color fg) {
    if (mCount >= MAX_LINES) return;
    Line& l = mLines[mCount++];
    l.fg    = fg;
    l.bg    = {};
    l.hasBg = false;

    int p = 0;
    if (text) {
        while (text[p] && p < MAX_WIDTH - 1) {
            l.text[p] = text[p];
            ++p;
        }
    }
    l.text[p] = '\0';
}

void ScrollViewport::AddLine(const char* text, Color fg, Color bg) {
    if (mCount >= MAX_LINES) return;
    Line& l = mLines[mCount++];
    l.fg    = fg;
    l.bg    = bg;
    l.hasBg = true;

    int p = 0;
    if (text) {
        while (text[p] && p < MAX_WIDTH - 1) {
            l.text[p] = text[p];
            ++p;
        }
    }
    l.text[p] = '\0';
}

void ScrollViewport::AddLine() {
    if (mCount >= MAX_LINES) return;
    Line& l    = mLines[mCount++];
    l.text[0]  = '\0';
    l.fg       = Theme::Current().Text;
    l.bg       = {};
    l.hasBg    = false;
}

void ScrollViewport::AddSeparator() {
    if (mCount >= MAX_LINES) return;
    Line& l   = mLines[mCount++];
    l.fg      = Theme::Current().Separator;
    l.bg      = {};
    l.hasBg   = false;
    int cols  = static_cast<int>(Renderer::Columns());
    int limit = cols < MAX_WIDTH - 1 ? cols : MAX_WIDTH - 1;
    for (int i = 0; i < limit; ++i) l.text[i] = '-';
    l.text[limit] = '\0';
}

void ScrollViewport::Render(int startRow, int viewRows) const {
    int rows = static_cast<int>(Renderer::Rows());
    for (int i = 0; i < viewRows; ++i) {
        int screenRow = startRow + i;
        if (screenRow >= rows) break;
        int lineIdx = mScrollPos + i;
        if (lineIdx < mCount) {
            const Line& l = mLines[lineIdx];
            if (l.hasBg)
                Renderer::DrawTextBg(0, screenRow, l.text, l.fg, l.bg);
            else
                Renderer::DrawText(0, screenRow, l.text, l.fg);
        } else {
            // Clear trailing rows so stale content from the previous frame
            // does not bleed through on partial updates.
            Renderer::DrawText(0, screenRow, "", Theme::Current().Text);
        }
    }
}

bool ScrollViewport::HandleKey(EFI_INPUT_KEY key, int viewRows) {
    if (key.ScanCode == SCAN_UP) {
        if (mScrollPos > 0) --mScrollPos;
        return true;
    }
    if (key.ScanCode == SCAN_DOWN) {
        ClampScroll(viewRows);
        if (mScrollPos + viewRows < mCount) ++mScrollPos;
        return true;
    }
    if (key.ScanCode == SCAN_PAGE_UP) {
        mScrollPos -= viewRows;
        if (mScrollPos < 0) mScrollPos = 0;
        return true;
    }
    if (key.ScanCode == SCAN_PAGE_DOWN) {
        mScrollPos += viewRows;
        ClampScroll(viewRows);
        return true;
    }
    if (key.ScanCode == SCAN_HOME) {
        mScrollPos = 0;
        return true;
    }
    if (key.ScanCode == SCAN_END) {
        mScrollPos = mCount - viewRows;
        if (mScrollPos < 0) mScrollPos = 0;
        return true;
    }
    return false;
}

void ScrollViewport::ScrollToLine(int line, int viewRows) {
    // Scroll up so the line appears at or below the top
    if (line < mScrollPos)
        mScrollPos = line;
    // Scroll down so the line appears at or above the bottom
    else if (line >= mScrollPos + viewRows)
        mScrollPos = line - viewRows + 1;
    ClampScroll(viewRows);
}

void ScrollViewport::ClampScroll(int viewRows) {
    int maxScroll = mCount - viewRows;
    if (maxScroll < 0) maxScroll = 0;
    if (mScrollPos > maxScroll) mScrollPos = maxScroll;
}
