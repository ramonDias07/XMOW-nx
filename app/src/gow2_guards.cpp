// XMOW (from gears2-nx) - the generic stack guard and the main-thread logger.
//
// What gears2-nx had here besides this, and is NOT in this build: the two Kismet hooks (sub_8255E4A8 and sub_8255A5C0,
// Gears of War 2 addresses: a walk of an actor's attachment array and of its +64 chain that looped on a self-referencing
// object) and the UE3 object naming through GNames at 0x83136834 (Gears' object layout and address). Hooked at the same
// numbers in Wolverine they would wrap unrelated functions. If Wolverine shows the same kind of loop, the generic guard
// below cuts it (and says where); the specific hooks can be rewritten with Wolverine's addresses then.

#include "xmow_pch.h"

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_set>

#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/xthread.h>

// The generic guard (make_nx_pch.py, point 5): any recompiled function entered with the guest stack within 32 KB of
// its limit lands here and returns at once with r3 = 0, so a runaway recursion anywhere ends in a cut instead of a
// crash. Logged once per function and call site (lr), with r3 (the object, for a method).
extern "C" void gow2_stack_cut(PPCContext& ctx, uint8_t* base, const char* function) {
  static std::atomic<uint32_t> cuts{0};
  static std::mutex mutex;
  static std::unordered_set<uint64_t> seen;
  const uint32_t n = cuts.fetch_add(1) + 1;
  const uint32_t lr = uint32_t(ctx.lr), object = ctx.r3.u32;
  bool fresh;
  {
    std::lock_guard<std::mutex> lock(mutex);
    fresh = seen.size() < 4096 && seen.insert((uint64_t(std::hash<const void*>{}(function)) << 32) ^ lr).second;
  }
  if (fresh || (n & (n - 1)) == 0) {
    REXLOG_WARN("[guard] stack cut #{} in {} (lr {:08X}, r1 {:08X}), r3 {:08X}", n, function, lr, ctx.r1.u32, object);
  }
  ctx.r3.u64 = 0;
}

// Where the main guest thread is, for the 10 s statistics (src/gow2_gpu.cpp): its last call site (lr), stack pointer
// and r3, r30 and r31. A hang shows up as the same place repeated, without a rebuild to find it.
void gow2_log_main_thread() {
  auto* kernel = rex::system::kernel_state();
  if (!kernel) return;
  for (auto& thread : kernel->object_table()->GetObjectsByType<rex::system::XThread>()) {
    if (!thread->main_thread() || !thread->thread_state()) continue;
    const PPCContext& ctx = *thread->thread_state()->context();
    REXLOG_INFO("[main] lr {:08X} r1 {:08X} r3 {:08X} r30 {:08X} r31 {:08X}", uint32_t(ctx.lr), ctx.r1.u32,
                ctx.r3.u32, ctx.r30.u32, ctx.r31.u32);
  }
}

// The check build of tools/patch_local_regs.py (PLR_CHECK=1): a call changed registers the analysis said it keeps.
// Logged once per call site (its number, patch_local_regs.check.map), at most 256 sites, with each
// register that changed (before -> after). A poisoned value a callee read shows up as a fault at 0x5Exxxxxx instead:
// the low 24 bits are the site's number too.
extern "C" void gow2_regcheck_fail(const char* function, const char* callee, int site, const char* regs,
                                   const uint64_t* before, const PPCContext* ctx) {
  static std::mutex mutex;
  static std::unordered_set<int> seen;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (seen.size() >= 256 || !seen.insert(site).second) return;
  }
  std::string changed;
  const char* p = regs;
  for (int k = 0; *p; ++k) {
    const char kind = *p++;
    int n = 0;
    while (*p >= '0' && *p <= '9') n = n * 10 + (*p++ - '0');
    while (*p == ' ') ++p;
    uint64_t now = 0;
    if (kind == 'r') {
      static const PPCRegister PPCContext::*const kR[32] = {
          &PPCContext::r0,  &PPCContext::r1,  &PPCContext::r2,  &PPCContext::r3,  &PPCContext::r4,  &PPCContext::r5,
          &PPCContext::r6,  &PPCContext::r7,  &PPCContext::r8,  &PPCContext::r9,  &PPCContext::r10, &PPCContext::r11,
          &PPCContext::r12, &PPCContext::r13, &PPCContext::r14, &PPCContext::r15, &PPCContext::r16, &PPCContext::r17,
          &PPCContext::r18, &PPCContext::r19, &PPCContext::r20, &PPCContext::r21, &PPCContext::r22, &PPCContext::r23,
          &PPCContext::r24, &PPCContext::r25, &PPCContext::r26, &PPCContext::r27, &PPCContext::r28, &PPCContext::r29,
          &PPCContext::r30, &PPCContext::r31};
      now = (ctx->*kR[n & 31]).u64;
    } else {
      static const PPCRegister PPCContext::*const kF[32] = {
          &PPCContext::f0,  &PPCContext::f1,  &PPCContext::f2,  &PPCContext::f3,  &PPCContext::f4,  &PPCContext::f5,
          &PPCContext::f6,  &PPCContext::f7,  &PPCContext::f8,  &PPCContext::f9,  &PPCContext::f10, &PPCContext::f11,
          &PPCContext::f12, &PPCContext::f13, &PPCContext::f14, &PPCContext::f15, &PPCContext::f16, &PPCContext::f17,
          &PPCContext::f18, &PPCContext::f19, &PPCContext::f20, &PPCContext::f21, &PPCContext::f22, &PPCContext::f23,
          &PPCContext::f24, &PPCContext::f25, &PPCContext::f26, &PPCContext::f27, &PPCContext::f28, &PPCContext::f29,
          &PPCContext::f30, &PPCContext::f31};
      now = (ctx->*kF[n & 31]).u64;
    }
    if (now != before[k]) changed += fmt::format(" {}{} {:X}->{:X}", kind, n, before[k], now);
  }
  REXLOG_WARN("[regcheck] site {:06X} ({}): the call to {} changed{}", site, function, callee, changed);
}
