import struct, sys, re
def fnv(b):
    h=0xcbf29ce484222325
    for x in b: h=((h^x)*0x100000001b3)&0xFFFFFFFFFFFFFFFF
    return h
def sig(b, tags):
    if b[:4]!=b'DXBC': return '?'
    parts=struct.unpack_from('<I',b,28)[0]
    for i in range(parts):
        off=struct.unpack_from('<I',b,32+i*4)[0]
        cc=b[off:off+4].decode('latin1'); size=struct.unpack_from('<I',b,off+4)[0]
        if cc not in tags: continue
        p=off+8; cnt,first=struct.unpack_from('<II',b,p)
        v1=cc.endswith('1'); stride=32 if v1 else 24; na=4 if v1 else 0
        out=[]
        for e in range(cnt):
            el=p+first+e*stride
            no,idx=struct.unpack_from('<II',b,el+na); reg=struct.unpack_from('<I',b,el+na+16)[0]; m=b[el+na+20]
            name=b[p+no:b.index(b'\0',p+no)].decode()
            out.append(f"{name}{idx}.{''.join('xyzw'[k] for k in range(4) if m>>k&1)}@{reg}")
        return ' '.join(out)
    return '-'
def strings(d):
    res=[];i=0;n=len(d)
    rx=re.compile(rb'[A-Za-z_][A-Za-z0-9_.]*$')
    while i+4<n:
        L=struct.unpack_from('<I',d,i)[0]
        if 2<=L<=80 and i+4+L<=n and rx.match(d[i+4:i+4+L]):
            res.append((i,d[i+4:i+4+L].decode())); i+=4+L; continue
        i+=1
    return res
f=sys.argv[1]; d=open(f,'rb').read()
strs=strings(d); si=0; ctx=[]
o=0
while True:
    o=d.find(b'DXBC',o)
    if o<0: break
    size=struct.unpack_from('<I',d,o+24)[0]
    while si<len(strs) and strs[si][0]<o: ctx.append(strs[si][1]); si+=1
    b=d[o:o+size]
    plat=next((s for s in reversed(ctx) if s.startswith('Direct3D_')),'?')
    stage=next((s for s in reversed(ctx) if s in('Vertex','Fragment','Compute')),'?')
    if plat=='Direct3D_SM65':
        tail=ctx[-40:]
        print(f"{fnv(b):016x} {stage} {plat} | out: {sig(b,('OSG1','OSGN'))} | in: {sig(b,('ISG1','ISGN'))} | ctx: {' '.join(tail[-24:])}")
    o+=max(size,4)
