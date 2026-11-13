// Persistent Bodies (Settings::persistent_bodies, the overlay's General): how many dead soldiers stay.
// A dying soldier's AGP_Pawn.DeadCleanup (0xAB480) hands its pawn to the game's
// AGP_GameSinglePlayer.AddPendingDestroyPawn (0x2B7410, through Level.Game's vtable +0x13C), which keeps the bodies in
// PendingDestroyAGPPawnsList (+0x60C data, +0x610 count) and, while the count is over MaxPendingDestroyAGPPawns
// (+0x618), destroys the oldest one that qualifies (its vtable +0x1E0) before adding the new one. On the console the
// limit leaves one body: each death took the last one away, and the ~30 s removals came the same way (probe,
// 2026-11-13: every body's DestroyActor came from 0x2B74C5). The limit is raised for that call to keep the setting's
// number, newest first; the game's own value is put back after, so Off is the console's behaviour.
#include <algorithm>
#include <cstdint>

#include "app/app.h"
#include "core/log.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace {
constexpr uint32_t kPendingCount = 0x610;  // PendingDestroyAGPPawnsList's count
constexpr uint32_t kPendingMax = 0x618;    // MaxPendingDestroyAGPPawns
}  // namespace

// AGP_GameSinglePlayer.AddPendingDestroyPawn, its compiled script: ecx a frame whose +4 is the game object
X86_EXTERN_FUNC(__imp__sub_002B7410);
X86_FUNC(sub_002B7410) {
    uint32_t keep = app::Get().persistent_bodies;  // bodies kept, 0 the game's own
    uint32_t game = X86_MEM32(ctx.ecx + 4);
    if (!keep || game < 0x10000 || !mem::IsCommitted(game + kPendingMax + 3)) {
        __imp__sub_002B7410(ctx, base);
        return;
    }
    int32_t own = int32_t(X86_MEM32(game + kPendingMax));
    static int32_t logged = -1;
    if (own != logged) {
        logged = own;
        LOGR("persistent bodies: the game keeps %d past the newest, now %u bodies (%u queued)", own,
             keep == app::kBodiesAll ? 0xFFFFFFFFu : keep, X86_MEM32(game + kPendingCount));
    }
    // the trim runs while the count is over the limit, then the new body is added: keep - 1 before it
    int32_t limit = keep == app::kBodiesAll ? 0x7FFFFFFF : int32_t(keep) - 1;
    X86_MEM32(game + kPendingMax) = uint32_t(std::max(own, limit));
    __imp__sub_002B7410(ctx, base);
    X86_MEM32(game + kPendingMax) = uint32_t(own);
}
