// Hooks for default.xbe. A strong definition replaces the recompiled function at that address, for example:
//
// #include "xdk/hle.h"
//
// X86_EXTERN_FUNC(__imp__sub_00012345);
// X86_FUNC(sub_00012345) {
//     __imp__sub_00012345(ctx, base);  // the original
// }

#include "core/log.h"
#include "xdk/hle.h"
#include "xdk/d3d8.h"

namespace dispatch {
uint32_t HostThunk(X86Func* fn);  // cpu/dispatch.cpp
}

// Coroutine dispatcher (cdecl run(start, object)): the game's scripted state code runs as stackful coroutines.
// The dispatcher saves every register and the 16 bit flags, keeps its stack pointer in [0x67C224] and its exit
// (0xEE16E) in [0x67C214], then either starts object->Run() (start != 0) or resumes the suspended one by jumping to
// [0x67C218]. A yield copies its frames to [0x67C210], stores where to resume in [0x67C218], sets esp back to
// [0x67C224] and jumps to [0x67C214]. Recompiled, that jump would run the exit as a nested call and then return into
// the dispatcher, which runs the exit again on a wrong stack; here [0x67C214] leads to a host function that throws
// back to this frame, which runs the exit once.
namespace {
struct CoroutineYield {};
constexpr uint32_t kExit = 0x67C214, kResumeAt = 0x67C218, kDispatcherEsp = 0x67C224;
constexpr uint32_t kGetRunner = 0x4AAD0;  // thiscall(object): the coroutine to run

void YieldToDispatcher(X86Context& __restrict, uint8_t*) { throw CoroutineYield{}; }
}  // namespace

X86_FUNC(sub_000EE130) {
    static const uint32_t yield = dispatch::HostThunk(YieldToDispatcher);
    // push ebp; mov ebp, esp; push ecx; push ebx; push esi; push edi; pushad; pushfw
    X86_PUSH32(ctx.ebp);
    ctx.ebp = ctx.esp;
    X86_PUSH32(ctx.ecx);
    X86_PUSH32(ctx.ebx);
    X86_PUSH32(ctx.esi);
    X86_PUSH32(ctx.edi);
    uint32_t sp = ctx.esp;
    X86_PUSH32(ctx.eax);
    X86_PUSH32(ctx.ecx);
    X86_PUSH32(ctx.edx);
    X86_PUSH32(ctx.ebx);
    X86_PUSH32(sp);
    X86_PUSH32(ctx.ebp);
    X86_PUSH32(ctx.esi);
    X86_PUSH32(ctx.edi);
    ctx.esp -= 2;
    X86_MEM16(ctx.esp) = (uint16_t)(ctx.cf | 2u | ctx.pf << 2 | ctx.zf << 6 | ctx.sf << 7 | 0x200u | ctx.df << 10 | ctx.of << 11);
    ctx.eax = 0xEE16E;
    X86_MEM32(kExit) = yield;
    X86_MEM32(kDispatcherEsp) = ctx.esp;
    const uint32_t dispatcher_esp = ctx.esp;
    try {
        if (X86_MEM32(ctx.ebp + 8)) {
            ctx.ecx = X86_MEM32(ctx.ebp + 0xC);
            X86_PUSH32(0xEE158u);
            x86_icall(ctx, base, kGetRunner);
            X86_MEM32(ctx.ebp - 4) = ctx.eax;
            ctx.ecx = ctx.eax;
            uint32_t run = X86_MEM32(X86_MEM32(ctx.eax) + 4);
            X86_PUSH32(0xEE166u);
            x86_icall(ctx, base, run);
        } else {
            x86_icall(ctx, base, X86_MEM32(kResumeAt));  // jmp [0x67C218]
        }
    } catch (const CoroutineYield&) {
    }
    // 0xEE16E: popfw; popad; pop edi; pop esi; pop ebx; mov esp, ebp; pop ebp; ret
    ctx.esp = dispatcher_esp;
    {
        uint32_t f = X86_MEM16(ctx.esp);
        ctx.esp += 2;
        ctx.cf = f & 1, ctx.pf = (f >> 2) & 1, ctx.zf = (f >> 6) & 1, ctx.sf = (f >> 7) & 1;
        ctx.df = (f >> 10) & 1, ctx.of = (f >> 11) & 1;
    }
    auto pop = [&] {
        uint32_t v = X86_MEM32(ctx.esp);
        ctx.esp += 4;
        return v;
    };
    ctx.edi = pop(), ctx.esi = pop(), ctx.ebp = pop();
    pop();  // esp
    ctx.ebx = pop(), ctx.edx = pop(), ctx.ecx = pop(), ctx.eax = pop();
    ctx.edi = pop(), ctx.esi = pop(), ctx.ebx = pop();
    ctx.esp = ctx.ebp;
    ctx.ebp = pop();
    ctx.esp += 4;  // ret
}

// IDirectSoundStream adjuster (this - 0x1C) -> 0x45C266 -> the voice's Use3DVoiceData flag (0x45B551), ret 8.
// xbe_symbols left it unnamed and the runtime has no IDirectSoundStream-side name for it; the original reads the
// library's own voice, which the runtime's streams don't have. The flag has no effect here, so it just succeeds.
// The other unnamed voice calls XACT makes are named by hand in config\game.toml.
X86_FUNC(sub_0045C836) {
    ctx.eax = 0;
    ctx.esp += 4 + 8;
}

// XOnlineGetUsers(XONLINE_USER users[8], DWORD* count), stdcall, ret 8: the accounts stored on the console.
// The runtime's XOnlineStartup is a stub, so the library's own code finds its state null and returns
// XONLINE_E_NOT_INITIALIZED (0x80150005); UIX's startup (0x34DBD3) treats that as fatal and the game then writes
// through the null UIX object. A console without accounts answers with none.
X86_FUNC(sub_003F2E56) {
    uint32_t users = X86_MEM32(ctx.esp + 4), count = X86_MEM32(ctx.esp + 8);
    for (uint32_t i = 0; users && i < 0x700; i += 4) X86_MEM32(users + i) = 0;
    if (count) X86_MEM32(count) = 0;
    LOG_FIRST(1, "aaroas: XOnlineGetUsers, no accounts");
    ctx.eax = 0;
    ctx.esp += 4 + 8;
}

// D3DDevice_UpdateOverlay(pSurface, pSrcRect, pDstRect, EnableColorKey, ColorKey), stdcall, ret 0x14.
// xbe_symbols has no 5849 signature for it. The original writes NV2A overlay registers (0x8704...) through the
// GPU register base, which is not mapped on the host, so it crashed on the first XMV movie frame.
// The movie frame (YUY2) is shown by the host renderer instead.
X86_FUNC(sub_003C37B0) {
    d3d::PresentOverlay(X86_MEM32(ctx.esp + 4));
    ctx.eax = 0;
    ctx.esp += 4 + 0x14;  // return address and 5 arguments
}
