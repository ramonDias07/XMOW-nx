import struct,sys,re
d=open("tmp/image.bin","rb").read()
gaps=[]
for l in open("gaps/real5.txt"):
    a,s,*_=l.split(); gaps.append((int(a,16),int(s)))
def ingap(v):
    for a,s in gaps:
        if a<=v<a+s: return (a,s)
W=lambda off: struct.unpack_from(">I",d,off)[0]
hits={}
n=len(d)//4
for i in range(n):
    w=W(i*4)
    if w>>26!=15: continue
    rt=(w>>21)&31; ra=(w>>16)&31
    if ra!=0: continue
    hi=w&0xFFFF
    if not (0x8200<=hi<=0x8330 or 0x8200<=hi+1<=0x8330): continue
    for j in range(i+1,min(i+10,n)):
        x=W(j*4); op=x>>26
        if op in(14,24) and ((x>>21)&31)>=0 and ((x>>16)&31)==rt:
            lo=x&0xFFFF
            if op==14 and lo&0x8000: lo-=0x10000
            v=((hi<<16)+lo)&0xFFFFFFFF if op==14 else ((hi<<16)|lo)
            if v&3==0 and ingap(v):
                hits.setdefault(v,[]).append((0x82000000+i*4,0x82000000+j*4))
        # stop if rt overwritten
        if op in(14,15,32,24,25) and ((x>>21)&31)==rt and op!=14 and op!=24: break
for v in sorted(hits):
    g=ingap(v)
    print("0x%08X gap=0x%08X+%d size=%d refs=%s"%(v,g[0],v-g[0],g[1],",".join("%08X"%h[0] for h in hits[v][:4])))
