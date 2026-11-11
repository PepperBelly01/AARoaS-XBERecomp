// Skip intros: the boot plays the four movies of the table at 0x5EDA70 (ESRB.xmv with the title and rating screens,
// UbisoftLogo, SLLogo, DemoMode) before the press start screen, each by a call of 0x300470. That routine plays a movie
// while 0x2F4540 says it goes on, then runs its finish handler, as a skip by button press does. With the setting on,
// the first four end at once; later movies (the menu's demo when idle, the briefings) play.
#include "app/app.h"
#include "core/log.h"
#include "xdk/hle.h"

namespace {
constexpr int kBootMovies = 4;
int g_played = 0;  // movies started since boot
}  // namespace

// plays one movie (stdcall, the movie)
X86_EXTERN_FUNC(__imp__sub_00300470);
X86_FUNC(sub_00300470) {
    g_played++;
    if (app::Get().skip_intros && g_played <= kBootMovies) LOGR("intro: skipped boot movie %d", g_played);
    __imp__sub_00300470(ctx, base);
}

// the movie's next frame (thiscall, no arguments): nonzero while it plays on
X86_EXTERN_FUNC(__imp__sub_002F4540);
X86_FUNC(sub_002F4540) {
    if (app::Get().skip_intros && g_played <= kBootMovies) {
        ctx.eax = 0;
        ctx.esp += 4;
        return;
    }
    __imp__sub_002F4540(ctx, base);
}
