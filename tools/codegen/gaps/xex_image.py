#!/usr/bin/env python3
"""Builds the guest image (base 0x82000000) from an XEX2 with basic compression (no LZX). Uses the openssl CLI for AES.
usage: xex_image.py default.xex out.bin"""
import struct, subprocess, sys, tempfile, os
RETAIL = bytes.fromhex("20B185A59D28FDC340583FBB0896BF91")
d = open(sys.argv[1], "rb").read()
assert d[:4] == b"XEX2"
pe, si, n = struct.unpack(">III", d[8:12] + d[16:20] + d[20:24])
pe = struct.unpack(">I", d[8:12])[0]; si = struct.unpack(">I", d[16:20])[0]; n = struct.unpack(">I", d[20:24])[0]
opt = {struct.unpack(">I", d[24+8*i:28+8*i])[0]: struct.unpack(">I", d[28+8*i:32+8*i])[0] for i in range(n)}
base = opt[0x10201]
off = opt[0x3ff]
size, enc, comp = struct.unpack(">IHH", d[off:off+8])
print("base %08X enc %d comp %d" % (base, enc, comp), file=sys.stderr)
assert comp == 1, "only basic compression supported"
blocks = [struct.unpack(">II", d[off+8+8*i:off+16+8*i]) for i in range((size-8)//8)]
enc_file_key = d[si+0x150:si+0x160]
def aes(data, key, dec=True):
    p = subprocess.run(["openssl", "enc", "-aes-128-cbc", "-d", "-nopad", "-K", key.hex(), "-iv", "0"*32], input=data, capture_output=True)
    assert p.returncode == 0, p.stderr
    return p.stdout
fk = aes(enc_file_key, RETAIL)
body = d[pe:]
body = body[:len(body)//16*16]
if enc:
    body = aes(body, fk)
out = bytearray(); pos = 0
for ds, zs in blocks:
    out += body[pos:pos+ds]; pos += ds; out += b"\0"*zs
open(sys.argv[2], "wb").write(out)
print("image %d bytes; first words: %s" % (len(out), out[:4].hex()), file=sys.stderr)
