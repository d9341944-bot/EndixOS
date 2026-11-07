import sys,struct,hashlib,re
img=open(sys.argv[1],'rb').read()
boot=bytes((i*3+1)&255 for i in range(512)); core=bytes((i*5+2)&255 for i in range(3000))
assert img[:440]==boot[:440] and img[0x5C:0x64]==b'\x01'+b'\0'*7 or img[:0x5C]==boot[:0x5C], "boot code"
print("boot code ok, sig",img[510:512].hex())
assert img[446+4]==0x0C and img[446]==0x80, "partition entry"
c=img[512:512+6*512]
assert c[:500]==core[:500] and c[512:3000]==core[512:3000], "core.img body"
bl=c[500:512]; print("blocklist start",struct.unpack('<Q',bl[:8])[0],"len",struct.unpack('<H',bl[8:10])[0],"seg",hex(struct.unpack('<H',bl[10:12])[0]))
assert struct.unpack('<Q',bl[:8])[0]==2 and struct.unpack('<H',bl[8:10])[0]==5 and struct.unpack('<H',bl[10:12])[0]==0x820
print("bootloader layout ok")
