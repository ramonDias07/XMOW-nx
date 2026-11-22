import os,sys
n=sys.argv[1]; src=sys.argv[2]; d=os.getcwd().replace(chr(92),'/')
sel=[]
for l in open('gaps/real%s.txt'%src):
    a,s,*w=l.split(); a=int(a,16); s=int(s)
    if 0x83262CFC<=a<0x83264000: continue   # import thunk area
    if s<=1024 and (s>4 or w[0]=='4e800020'): sel.append((a,s))
print(len(sel))
p='tmp/pass%s/'%n; os.makedirs(p,exist_ok=True)
open(p+'selected.txt','w').write("".join("0x%08X %d\n"%x for x in sel))
open(p+'scratch.toml','w').write("[functions]\n"+"".join('0x%08X = { }\n'%a for a,s in sel))
open(p+'manifest.toml','w').write('''[project]
sdk_version = "0.10.0"
name = "xmow"

[entrypoint]
file_path = "%s/../../XMOW/game_root/default.xex"
out_directory_path = "generated"
includes = ["%s/xmow_config.toml", "%s/%sscratch.toml"]
'''%(d,d,d,p))
