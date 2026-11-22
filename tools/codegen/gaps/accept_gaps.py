"""Mimics the acceptance loop of gap_fixpoint.py (copy). usage: accept_gaps.py PASSDIR  -> PASSDIR/accepted.txt"""
import os,re,sys,subprocess
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import gap_classify as gc
d=sys.argv[1]; cfg=sys.argv[2]
subprocess.run([sys.executable,"-I",os.path.join(os.path.dirname(os.path.abspath(__file__)),"gap_classify.py"),"--tier2",d+"/generated",d+"/selected.txt",d+"/codegen.log",cfg,d+"/t2.txt"])
have=set(re.findall(r'(0x[0-9A-Fa-f]+)\s*=',open(cfg).read()))
acc=[]; rej=0
for f in ("acc.txt","t2.txt"):
    for l in open(d+"/"+f):
        a,s,rest=l.split(" ",2)
        ins=[i.strip() for i in rest.lstrip("| ").strip().split(" ; ")]
        ops=[i.split()[0] for i in ins]
        if a in have or any(re.search(r"\br1\b",i) for i in ins) or any(o in("mtlr","mflr","ld","std","stwu","lfd","stfd") for o in ops) or gc.reads_undefined_at_entry(ins): rej+=1; continue
        acc.append(l); have.add(a)
open(d+"/accepted.txt","w").writelines(acc); print(len(acc),"accepted",rej,"rejected as fragments")
