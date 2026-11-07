import sys,struct
img=open(sys.argv[1],'rb').read()
mbr=img[:512]; assert mbr[510:512]==b'\x55\xaa'
t,start,size=mbr[450],*struct.unpack('<II',mbr[454:462]); print("part type %02x start %d size %d"%(t,start,size))
p=img[start*512:]
bps,spc,rsvd,nf=struct.unpack('<HBHB',p[11:17]); tot=struct.unpack('<I',p[32:36])[0]; fsz=struct.unpack('<I',p[36:40])[0]; root=struct.unpack('<I',p[44:48])[0]
assert bps==512 and nf==2 and p[82:90]==b'FAT32   ' and p[510:512]==b'\x55\xaa'
assert p[:512]==p[6*512:7*512], "backup boot differs"
fat1=p[rsvd*512:(rsvd+fsz)*512]; fat2=p[(rsvd+fsz)*512:(rsvd+2*fsz)*512]; assert fat1==fat2, "FAT copies differ"
data=rsvd+2*fsz; ncl=(tot-data)//spc
F=lambda c: struct.unpack('<I',fat1[c*4:c*4+4])[0]&0x0FFFFFFF
assert F(0)==0x0FFFFFF8 and F(1)==0x0FFFFFFF
def chain(c):
    out=[]
    while 2<=c<0x0FFFFFF7:
        assert c not in out, "loop"; out.append(c); c=F(c)
    assert c>=0x0FFFFFF8, "bad chain end %x"%c
    return out
used=set()
def rdchain(c): return b''.join(p[(data+(x-2)*spc)*512:(data+(x-1)*spc)*512] for x in chain(c))
files={}
def walk(c,path):
    d=rdchain(c)
    for o in range(0,len(d),32):
        e=d[o:o+32]
        if e[0]==0: break
        if e[0]==0xE5 or e[11]==0x0F or e[0]==ord('.'): continue
        n=e[:8].decode().strip(); x=e[8:11].decode().strip(); nm=n+('.'+x if x else '')
        cl=(struct.unpack('<H',e[20:22])[0]<<16)|struct.unpack('<H',e[26:28])[0]; sz=struct.unpack('<I',e[28:32])[0]
        if e[11]&0x10: walk(cl,path+'/'+nm)
        else:
            ch=chain(cl) if sz else []
            assert len(ch)*spc*512>=sz and (len(ch)-1)*spc*512<sz or sz==0, "size/chain mismatch "+nm
            for x in ch: assert x not in used, "cross-linked"; used.add(x)
            files[path+'/'+nm]=(sz,(rdchain(cl)[:sz] if sz else b''))
walk(root,'')
print(len(files),"files; clusters used by files",len(used))
big=files['/BOOT/ENDIXOS.ELF'][1]; exp=bytes(((i*7+(i>>8))&255) for i in range(1500000)); assert big==exp,"big file corrupt"
assert files['/HOME/DANIL/USERS.DAT'][1]==b'replaced longer text'
assert files['/BOOT/GRUB/GRUB.CFG'][1].startswith(b'set timeout=3')
for i in range(100): assert files['/HOME/F%d.TXT'%i][1]==('/home/f%d.txt'%i).encode()
# any allocated cluster in FAT not reachable? (leak check)
alloc={c for c in range(2,ncl+2) if F(c)!=0}
dirs=set(); 
def dd(c):
    dirs.update(chain(c))
    d=rdchain(c)
    for o in range(0,len(d),32):
        e=d[o:o+32]
        if e[0]==0: break
        if e[0]==0xE5 or e[11]==0x0F or e[0]==ord('.'): continue
        if e[11]&0x10: dd((struct.unpack('<H',e[20:22])[0]<<16)|struct.unpack('<H',e[26:28])[0])
dd(root)
leak=alloc-used-dirs; print("leaked clusters:",len(leak)); assert not leak
print("ALL OK")
