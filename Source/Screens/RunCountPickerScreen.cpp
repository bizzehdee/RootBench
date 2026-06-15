#include "Screens/RunCountPickerScreen.h"
#include "Tui.h"
#include "Renderer.h"
#include "ColorTheme.h"
#include "BenchmarkRegistry.h"
#include "IBenchmark.h"
#include "RunConfig.h"
#include "Freestanding.h"

namespace {
// Format "<label><N> min [(HhMMm)]" into line, with cursor marker.
void FormatMinutes(char* line, bool hi, const char* label, int minutes) {
    int p = 0;
    line[p++] = hi ? '>' : ' '; line[p++] = ' ';
    for (const char* s = label; *s; ++s) line[p++] = *s;
    const char* ns = UintToStr(static_cast<UINT64>(minutes));
    for (int j = 0; ns[j]; ++j) line[p++] = ns[j];
    for (const char* s = " min"; *s; ++s) line[p++] = *s;
    if (minutes >= 60) {
        line[p++] = ' '; line[p++] = '(';
        const char* hs = UintToStr(static_cast<UINT64>(minutes / 60));
        for (int j = 0; hs[j]; ++j) line[p++] = hs[j];
        line[p++] = 'h';
        int mm = minutes % 60;
        line[p++] = (char)('0' + mm / 10);
        line[p++] = (char)('0' + mm % 10);
        line[p++] = 'm'; line[p++] = ')';
    }
    line[p] = '\0';
}
}  // namespace

RunCountPickerScreen::Field RunCountPickerScreen::FieldAt(int cursor) const {
    if (cursor == 0) return F_Runs;
    int i = 0;
    if (mHasCoreCycle) { ++i; if (cursor == i) return F_Scope; }
    if (mHasRegular)   { ++i; if (cursor == i) return F_TestDuration; }
    if (mHasStress)    { ++i; if (cursor == i) return F_StressDuration; }
    return F_None;
}

void RunCountPickerScreen::OnEnter(Tui& tui) {
    Tui::PendingRun& pr = tui.Pending();
    mRuns              = 1;
    mCoreCycleAllCores = true;
    mCursor            = 0;
    mTestMin           = static_cast<int>(RunConfig::GetTestMinutes());
    mStressMin         = static_cast<int>(RunConfig::GetStressMinutes());

    mHasCoreCycle = false;
    for (UINTN i = 0; i < pr.count; ++i)
        if (pr.modes[i] == RunMode::CoreCycle) { mHasCoreCycle = true; break; }

    // Classify the selection into stress vs regular time-boxed tests.
    if (pr.category) {
        bool isStress = (StrCmp(pr.category, "Stress") == 0);
        mHasStress  = isStress;
        mHasRegular = !isStress;
    } else {
        mHasStress = mHasRegular = false;
        IBenchmark** all = BenchmarkRegistry::GetAll();
        for (UINTN i = 0; i < pr.count; ++i) {
            if (StrCmp(all[pr.indices[i]]->GetCategory(), "Stress") == 0) mHasStress = true;
            else                                                          mHasRegular = true;
        }
    }

    mPickerRows = 1 + (mHasCoreCycle ? 1 : 0) + (mHasRegular ? 1 : 0) + (mHasStress ? 1 : 0);

    // Footer: "Running <count> benchmark(s)".
    int p = 0;
    for (const char* s = "Running "; *s; ++s) mFooter[p++] = *s;
    const char* ns = UintToStr(pr.count);
    for (int j = 0; ns[j] && p < 44; ++j) mFooter[p++] = ns[j];
    for (const char* s = " benchmark(s)"; *s && p < 47; ++s) mFooter[p++] = *s;
    mFooter[p] = '\0';
}

void RunCountPickerScreen::Draw(Tui& /*tui*/, int top, int /*bottom*/) {
    int row = top + 1;

    const char* prompt = mHasCoreCycle
        ? "How many times to run on each core?"
        : "How many times to run each benchmark?";
    Renderer::DrawText(2, row, prompt, Theme::Current().TextDim);
    row += 2;

    char line[80];

    // Runs (cursor 0)
    {
        bool hi = (FieldAt(mCursor) == F_Runs);
        int p = 0;
        line[p++] = hi ? '>' : ' '; line[p++] = ' ';
        for (const char* s = "Runs:  "; *s; ++s) line[p++] = *s;
        const char* ns = UintToStr(static_cast<UINT64>(mRuns));
        for (int j = 0; ns[j]; ++j) line[p++] = ns[j];
        line[p] = '\0';
        Renderer::DrawText(2, row, line, hi ? Theme::Current().Accent : Theme::Current().Text);
    }
    row++;

    if (mHasCoreCycle) {
        row++;
        bool hi = (FieldAt(mCursor) == F_Scope);
        int p = 0;
        line[p++] = hi ? '>' : ' '; line[p++] = ' ';
        for (const char* s = "Core scope:  "; *s; ++s) line[p++] = *s;
        const char* sv = mCoreCycleAllCores ? "[All Cores]" : "[Selected]";
        for (int j = 0; sv[j]; ++j) line[p++] = sv[j];
        line[p] = '\0';
        Renderer::DrawText(2, row, line, hi ? Theme::Current().Accent : Theme::Current().Text);
        row++;
    }

    if (mHasRegular) {
        row++;
        bool hi = (FieldAt(mCursor) == F_TestDuration);
        FormatMinutes(line, hi, "Test duration:    ", mTestMin);
        Renderer::DrawText(2, row, line, hi ? Theme::Current().Accent : Theme::Current().Text);
        row++;
    }

    if (mHasStress) {
        row++;
        bool hi = (FieldAt(mCursor) == F_StressDuration);
        FormatMinutes(line, hi, "Stress duration:  ", mStressMin);
        Renderer::DrawText(2, row, line, hi ? Theme::Current().Accent : Theme::Current().Text);
        row++;
    }

    row++;
    Renderer::DrawText(2, row,
        "[Up/Dn] Field  [Left/Right] Adjust  [Enter] Start  [Esc] Cancel",
        Theme::Current().TextDim);
    if (mHasRegular || mHasStress) {
        row++;
        Renderer::DrawText(2, row,
            "Duration: [Left/Right] +/-1 min   [PgUp/PgDn] +/-60 min   (1 min - 24 h)",
            Theme::Current().TextDim);
    }
}

NavResult RunCountPickerScreen::HandleKey(Tui& tui, EFI_INPUT_KEY key) {
    Field f = FieldAt(mCursor);

    if (key.ScanCode == SCAN_UP) {
        mCursor = (mCursor - 1 + mPickerRows) % mPickerRows;
    } else if (key.ScanCode == SCAN_DOWN) {
        mCursor = (mCursor + 1) % mPickerRows;
    } else if (key.ScanCode == SCAN_LEFT || key.ScanCode == SCAN_RIGHT) {
        bool inc = (key.ScanCode == SCAN_RIGHT);
        if (f == F_Runs) {
            if (!inc && mRuns > 1)  --mRuns;
            if (inc  && mRuns < 10) ++mRuns;
        } else if (f == F_Scope) {
            mCoreCycleAllCores = !mCoreCycleAllCores;
        } else if (f == F_TestDuration) {
            mTestMin += inc ? 1 : -1;
        } else if (f == F_StressDuration) {
            mStressMin += inc ? 1 : -1;
        }
    } else if (key.ScanCode == SCAN_PAGE_UP || key.ScanCode == SCAN_PAGE_DOWN) {
        int delta = (key.ScanCode == SCAN_PAGE_UP) ? 60 : -60;
        if      (f == F_TestDuration)   mTestMin   += delta;
        else if (f == F_StressDuration) mStressMin += delta;
    } else if (key.ScanCode == SCAN_ESC) {
        return NavBack();
    } else if (key.UnicodeChar == '\r' || key.UnicodeChar == '\n') {
        Tui::PendingRun& pr = tui.Pending();
        pr.runs              = static_cast<UINTN>(mRuns);
        pr.coreCycleAllCores = mCoreCycleAllCores;
        if (mHasRegular) RunConfig::SetTestMinutes(static_cast<UINT32>(mTestMin));
        if (mHasStress)  RunConfig::SetStressMinutes(static_cast<UINT32>(mStressMin));
        tui.RunPending();   // BenchmarkRunner owns the display during the run
        return NavReplace(pr.category ? ScreenId::CategoryResults : ScreenId::Results);
    }

    // Clamp durations to the allowed range after any adjustment.
    const int lo = (int)RunConfig::kMinMinutes, hi = (int)RunConfig::kMaxMinutes;
    if (mTestMin   < lo) mTestMin   = lo;  if (mTestMin   > hi) mTestMin   = hi;
    if (mStressMin < lo) mStressMin = lo;  if (mStressMin > hi) mStressMin = hi;

    return NavStay();
}
