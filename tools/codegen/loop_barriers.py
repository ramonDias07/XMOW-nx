#!/usr/bin/env python3
"""Compiler barriers at the loop headers of the generated code (lote A; REQUIRED together with tools/pch_no_volatile.py).

Copied from LoS1-nx tools/loop_barriers.py (from futari-nx tools/perf_codegen_post.py pass 3; LoS: 26,566 barriers,
find_self_loops 0 in recompiled functions). The logic is LoS's unchanged; xmow changes: the newline style of each file
is kept (the codegen writes CRLF on Windows), the counts give the reason a header got no barrier, --report FILE
writes one line per loop header, and xmow_inline_helpers.h is checked for loops (the helpers are inlined everywhere
and are not processed: today they are only the register save/restore helpers, with no loop).

With guest loads that are not volatile, GCC -O3 (the Switch build) hoists the load of a guest polling loop
(`while (device[x] != 0) {}`: a real wait on the Xbox 360, another thread or the GPU changes the word) out of the loop
and turns it into a branch to itself: the thread spins forever. tools/find_self_loops.py (LoS) counts these in an ELF.

A loop header is a label `loc_X:` that a later `goto loc_X;` of the same function jumps back to. The line after it gets
REX_LOOP_BARRIER(), a compiler barrier on the guest memory (defined in the pch): no guest load is reused across
iterations, also inside inlined callees; straight-line code keeps the plain loads.

A header gets NO barrier when the barrier has nothing to protect:
  - no-guest: the loop's lines (from its header to its last backward goto) read no guest memory and make no call at
    all (REX_SLOAD_*/REX_SSTORE_*, the guest stack of r1 that tools/patch_stack_access.py already made non-volatile,
    do not count: that is the state before this pass);
  - indirect: every way back to the header passes an indirect call (REX_CALL_INDIRECT_*: a call through a pointer
    loaded from memory, which the compiler must assume changes memory). Calls of recompiled functions, inlined
    helpers, hooks and kernel imports do NOT count. A function with a goto this script cannot read keeps a barrier
    at every header.
--all restores a barrier at every header. Idempotent: barriers no longer wanted are removed.

The barrier clobbers the 4 GB guest window (+4 KB, the PC's REX_PHYS_HOST_OFFSET page; marathon-nx / peggle2-nx), not
all memory: ctx and the host stack keep their registers. -DREX_LOOP_BARRIER_FULL gives the full "memory" form.

  python -I tools/loop_barriers.py generated/default [--check] [--all] [--report FILE]
"""
import os
import re
import sys

LABEL = re.compile(r'^(loc_[0-9A-F]{8}):\s*$')
GOTO = re.compile(r'goto (loc_[0-9A-F]{8});')
ONLY_GOTO = re.compile(r'^\tgoto (loc_[0-9A-F]{8});$')
IF_GOTO = re.compile(r'^\tif \(.*\) goto (loc_[0-9A-F]{8});$')
# Also the return of a leaf function after a leaf_locals-style pass: `{ REX_LEAF_STORE(l_r3, r3); return; }`.
# xmow lote B: and patch_local_regs' write-back before a return: `{ ctx.r3 = L_r3; return; }`.
RETURN = re.compile(r'^\t(?:\{ (?:(?:REX_LEAF_STORE\(\w+, \w+\)|ctx\.\w+ = L_\w+); )+)?return\b[^;]*;(?: \})?$')
BARRIER = '\tREX_LOOP_BARRIER();'
MARKER = '// xmow lote A: loop barrier (tools/loop_barriers.py, from LoS1-nx)'
MACRO = (MARKER + '\n#ifndef REX_LOOP_BARRIER\n'
         '#if defined(REX_LOOP_BARRIER_FULL)\n'
         '#define REX_LOOP_BARRIER() __asm__ __volatile__("" ::: "memory")\n'
         '#else  // the guest window only (marathon-nx): ctx and the host stack keep their registers\n'
         '#define REX_LOOP_BARRIER() __asm__ __volatile__("" : "+m"(*(uint8_t (*)[0x100001000ull])base))\n'
         '#endif\n#endif\n')
# A read of guest memory, or anything that could hide one: a load, a raw address, a call of any kind.
GUEST = re.compile(r'REX_LOAD|REX_RO_|REX_RAW_ADDR|REX_ADDR|REX_GUEST_CAS|REX_MM_|\bbase\b|REX_CALL_INDIRECT_|ppc_trap\(')
INDIRECT = re.compile(r'REX_CALL_INDIRECT_')
OTHER_CALL = re.compile(r'\b\w+\(ctx, base[,)]')
STACK = re.compile(r'REX_SLOAD_')


def needed_headers(body, headers):
    """Of the loop headers of one function (label -> line index in body): {header: reason}, reason in
    'barrier', 'barrier-unreadable', 'no-guest', 'indirect'; plus {header: (lines of the loop)}."""
    label_at = {}
    for i, line in enumerate(body):
        m = LABEL.match(line)
        if m:
            label_at[m.group(1)] = i
    last_back = {}
    for i, line in enumerate(body):
        for target in GOTO.findall(line):
            if target in headers and label_at[target] <= i:
                last_back[target] = i
    n = len(body)
    succ = [None] * n
    readable = True
    for i, line in enumerate(body):
        m = ONLY_GOTO.match(line)
        mi = IF_GOTO.match(line) if not m else None
        if 'goto' in line and not m and not mi and not line.lstrip().startswith('//'):
            readable = False
            break
        if m:
            succ[i] = [label_at[m.group(1)]] if m.group(1) in label_at else None
        elif mi:
            succ[i] = [i + 1, label_at[mi.group(1)]] if mi.group(1) in label_at else None
        elif RETURN.match(line):
            succ[i] = []
        else:
            succ[i] = [i + 1]
        if succ[i] is None:
            readable = False
            break
    reason, loops = {}, {}
    for header, at in headers.items():
        end = last_back.get(header, at)
        lines = [l for l in body[at + 1:end + 1] if not l.lstrip().startswith('//') and l != BARRIER]
        loops[header] = lines
        if not any(GUEST.search(l) or OTHER_CALL.search(l) for l in lines):
            reason[header] = 'no-guest'
            continue
        if not readable:
            reason[header] = 'barrier-unreadable'
            continue
        seen = set()
        stack = [at]
        found = False
        while stack and not found:
            i = stack.pop()
            if i in seen or i >= n:
                continue
            seen.add(i)
            line = body[i]
            if INDIRECT.search(line) and not OTHER_CALL.search(line) and not line.lstrip().startswith('//'):
                continue  # this path is fenced by the call
            for j in succ[i]:
                if j == at:
                    found = True
                    break
                stack.append(j)
        reason[header] = 'barrier' if found else 'indirect'
    return reason, loops


def read(path):
    raw = open(path, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in raw else '\n'
    return raw.replace('\r\n', '\n'), nl


def write(path, text, nl):
    open(path, 'w', encoding='utf-8', newline='').write(text.replace('\n', nl))


def process(folder, check, everywhere, report):
    counts = {'barrier': 0, 'barrier-unreadable': 0, 'no-guest': 0, 'indirect': 0}
    headers_total = files = 0
    stack_only = 0
    rep = []
    for f in sorted(os.listdir(folder)):
        if not re.match(r'\w+_recomp\.\d+\.cpp$', f):
            continue
        path = os.path.join(folder, f)
        text, nl = read(path)
        lines = text.split('\n')
        plain = [l for l in lines if l != BARRIER]
        out = []
        i = 0
        while i < len(plain):
            if not plain[i].startswith('DEFINE_REX_FUNC('):
                out.append(plain[i])
                i += 1
                continue
            j = i + 1
            while j < len(plain) and plain[j] != '}':
                j += 1
            body = plain[i + 1:j]
            func = plain[i][len('DEFINE_REX_FUNC('):].split(')')[0]
            headers, seen = {}, {}
            for k, line in enumerate(body):
                m = LABEL.match(line)
                if m:
                    seen[m.group(1)] = k
                    continue
                for target in GOTO.findall(line):
                    if target in seen:
                        headers[target] = seen[target]
            reason, loops = needed_headers(body, headers)
            if everywhere:
                reason = {h: 'barrier' for h in headers}
            need = {h for h, r in reason.items() if r.startswith('barrier')}
            headers_total += len(headers)
            for h in sorted(headers):
                counts[reason[h]] += 1
                calls = any(OTHER_CALL.search(l) or INDIRECT.search(l) for l in loops[h])
                stack = any(STACK.search(l) for l in loops[h])
                if reason[h] == 'no-guest' and stack:
                    stack_only += 1
                rep.append(f'{f}\t{func}\t{h}\t{reason[h]}\tlines={len(loops[h])}\tcalls={int(calls)}\tstack={int(stack)}')
            out.append(plain[i])
            for line in body:
                out.append(line)
                m = LABEL.match(line)
                if m and m.group(1) in need:
                    out.append(BARRIER)
            i = j
        if out != lines:
            files += 1
            if not check:
                write(path, '\n'.join(out), nl)
    pch = next((os.path.join(folder, f) for f in os.listdir(folder) if f.endswith('_pch.h')), None)
    if not pch:
        sys.exit(f'loop barriers: no *_pch.h in {folder}')
    text, nl = read(pch)
    pch_changed = False
    if MARKER not in text:
        if '#define REX_LOAD_U8(x)' not in text:
            sys.exit(f'loop barriers: no REX_LOAD_U8 in {pch}: the generated pch changed')
        if 'REX_LOOP_BARRIER' in text:
            sys.exit(f'loop barriers: {pch} already defines another REX_LOOP_BARRIER')
        text = text.replace('#define REX_LOAD_U8(x)', MACRO + '#define REX_LOAD_U8(x)', 1)
        pch_changed = True
        if not check:
            write(pch, text, nl)
    helpers = os.path.join(folder, 'xmow_inline_helpers.h')
    if os.path.isfile(helpers):
        htext, _ = read(helpers)
        hloops = len([l for l in htext.split('\n') if LABEL.match(l)])
        if hloops:
            print(f'WARNING: {helpers} has {hloops} labels: inlined helpers with loops get no barrier')
    if report and not check:
        with open(report, 'w', encoding='utf-8', newline='\n') as r:
            r.write('file\tfunction\theader\treason\tlines\tcalls\tstack\n' + '\n'.join(rep) + '\n')
    with_barrier = counts['barrier'] + counts['barrier-unreadable']
    print(f'{folder}: {headers_total} loop headers, {with_barrier} with a barrier '
          f'({counts["barrier-unreadable"]} of them in functions with a goto this cannot follow); without: '
          f'{counts["no-guest"]} read no guest memory and call nothing ({stack_only} of them read the guest stack r1), '
          f'{counts["indirect"]} fenced by an indirect call on every way back; {files} files '
          f'{"to change" if check else "changed"}{", pch macro added" if pch_changed else ""}')
    return files == 0 and not pch_changed


def main():
    check = '--check' in sys.argv
    everywhere = '--all' in sys.argv
    args = sys.argv[1:]
    report = None
    if '--report' in args:
        k = args.index('--report')
        report = args[k + 1]
        del args[k:k + 2]
    folders = [a for a in args if not a.startswith('--')]
    if not folders:
        sys.exit(__doc__)
    ok = all([process(f, check, everywhere, report) for f in folders])
    sys.exit(0 if ok or not check else 1)


if __name__ == '__main__':
    main()
