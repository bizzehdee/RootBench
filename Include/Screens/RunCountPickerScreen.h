#pragma once
#include "Screens/Screen.h"

// Run options before launch:
//   - Runs:            how many times to run each benchmark (always)
//   - Core scope:      all vs selected cores (when any selection is core-cycle)
//   - Test duration:   minutes for regular time-boxed perf tests (when present)
//   - Stress duration: minutes for stress soak tests (when present)
// Durations are configurable 1 minute - 24 hours.
class RunCountPickerScreen : public Screen {
public:
    const char* Title()  const override { return "Set Run Count"; }
    const char* Footer() const override { return mFooter; }

    void      OnEnter(Tui& tui) override;
    void      Draw(Tui& tui, int top, int bottom) override;
    NavResult HandleKey(Tui& tui, EFI_INPUT_KEY key) override;

private:
    enum Field { F_Runs, F_Scope, F_TestDuration, F_StressDuration, F_None };
    Field FieldAt(int cursor) const;

    int  mRuns              = 1;
    bool mCoreCycleAllCores = true;
    bool mHasCoreCycle      = false;
    bool mHasRegular        = false;   // any non-stress time-boxed test selected
    bool mHasStress         = false;
    int  mTestMin           = 3;
    int  mStressMin         = 30;
    int  mCursor            = 0;
    int  mPickerRows        = 1;
    char mFooter[48]        = {};
};
