import re,sys,collections
rep,img,outreal,outtoml=sys.argv[1:5]
d=open(img,'rb').read(); gaps=set()
for l in open(rep):
    m=re.match(r'\s+0x([0-9A-F]{8})\s+(\d+) bytes',l)
    if m: gaps.add((int(m.group(1),16),int(m.group(2))))
real=[]; cat=collections.Counter()
for a,s in sorted(gaps):
    w=[d[a-0x82000000+i:a-0x82000000+i+4].hex() for i in range(0,s,4)]
    if all(x in('00000000','60000000') for x in w): cat['padding']+=1; continue
    real.append((a,s,w)); cat['code-or-data']+=1
open(outreal,'w').write("\n".join("%08X %d %s"%(a,s," ".join(w[:6])) for a,s,w in real)+"\n")
open(outtoml,'w').write("".join('"0x%08X" = { }  # %d bytes\n'%(a,s) for a,s,w in real))
print(dict(cat),"unique >4B:",sum(1 for r in real if r[1]>4))
