#!/usr/bin/env python3
"""Guest loads/stores without `volatile` in the generated xmow_pch.h (lote A of the codegen speed work).

Copied from LoS1-nx tools/pch_no_volatile.py (itself from masseffect-nx, measured there at +15-20 % fps; LoS: 14.0 ->
20.6 fps together with tools/loop_barriers.py). The generated REX_LOAD_U*/REX_STORE_U* read guest memory through
volatile pointers, which stops the compiler from keeping values in registers or pairing and scheduling accesses.
The MMIO macros (REX_MM_*) keep volatile. Defining REX_GUEST_VOLATILE as `volatile` (e.g. -DREX_GUEST_VOLATILE=volatile)
restores the generated behaviour. Idempotent.

REQUIRED together with tools/loop_barriers.py: a guest loop that polls memory with no call inside would otherwise
have its load hoisted out of the loop by GCC -O3 (a branch to itself on the console).

xmow changes: the newline style of the file is kept (the codegen writes CRLF on Windows); the generated pch has no
D-form macros (REX_LOAD_D_U*), the prefixes stay for a later codegen.

  python -I tools/pch_no_volatile.py generated/default/xmow_pch.h [--check]
"""
import sys

MARK = 'REX_GUEST_VOLATILE'
PREFIXES = ('#define REX_LOAD_U', '#define REX_STORE_U', '#define REX_LOAD_D_U', '#define REX_STORE_D_U')


def patch(path, check):
    raw = open(path, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in raw else '\n'
    text = raw.replace('\r\n', '\n')
    if MARK in text:
        print(f'{path}: already without volatile')
        return True
    if check:
        print(f'{path}: still volatile')
        return False
    lines = text.split('\n')
    changed = 0
    for i, line in enumerate(lines):
        if line.startswith(PREFIXES):
            new = line.replace('(volatile u', '(REX_GUEST_VOLATILE u')
            changed += new != line
            lines[i] = new
    # lote C: o pch do gerador do fork do LoS1-nx traz tambem as 8 formas D (REX_LOAD_D_U*/REX_STORE_D_U*, ramo sem
    # janela fisica, o do Switch): 16 macros.
    want = 16 if '#define REX_LOAD_D_U8(r, d)' in text else 8
    if changed != want or '#define REX_LOAD_U8(x)' not in text:
        sys.exit(f'{path}: expected {want} "(volatile u" REX_LOAD/REX_STORE macros, found {changed}; the generated pch changed')
    text = '\n'.join(lines).replace('#define REX_LOAD_U8(x)', '''// xmow lote A: guest loads/stores without volatile (tools/pch_no_volatile.py, from LoS1-nx); define
// REX_GUEST_VOLATILE as volatile to restore the generated behaviour. MMIO (REX_MM_*) stays volatile.
#ifndef REX_GUEST_VOLATILE
#define REX_GUEST_VOLATILE
#endif
#define REX_LOAD_U8(x)''', 1)
    open(path, 'w', encoding='utf-8', newline='').write(text.replace('\n', nl))
    print(f'{path}: {changed} macros without volatile')
    return True


def main():
    check = '--check' in sys.argv
    paths = [a for a in sys.argv[1:] if not a.startswith('--')]
    if not paths:
        sys.exit(__doc__)
    ok = all([patch(p, check) for p in paths])
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
