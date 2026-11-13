// Skip intros: the boot plays the four movies of the table at 0x5EDA70 (ESRB.xmv with the title and rating screens,
// UbisoftLogo, SLLogo, DemoMode) before the press start screen, each by a call of 0x300470. That routine plays a movie
// while 0x2F4540 says it goes on, then runs its finish handler, as a skip by button press does. With the setting on,
// the first four end at once, and only while one of those calls runs: other movies (the Roles films, the Extras
// screen's, the menu's demo when idle) step 0x2F4540 from elsewhere and were ended too while only the count was
// asked (user, 2026-11-13).
#include "app/app.h"
#include "core/log.h"
#include "xdk/hle.h"

namespace {
constexpr int kBootMovies = 4;
int g_played = 0;           // movies 0x300470 started since boot
bool g_boot_movie = false;  // one of the boot movies is playing
}  // namespace

// plays one movie (stdcall, the movie)
X86_EXTERN_FUNC(__imp__sub_00300470);
X86_FUNC(sub_00300470) {
    g_played++;
    bool skip = app::Get().skip_intros && g_played <= kBootMovies;
    if (skip) LOGR("intro: skipped boot movie %d", g_played);
    g_boot_movie = skip;
    __imp__sub_00300470(ctx, base);
    g_boot_movie = false;
}

// the movie's next frame (thiscall, no arguments): nonzero while it plays on
X86_EXTERN_FUNC(__imp__sub_002F4540);
X86_FUNC(sub_002F4540) {
    if (g_boot_movie) {
        ctx.eax = 0;
        ctx.esp += 4;
        return;
    }
    __imp__sub_002F4540(ctx, base);
}
