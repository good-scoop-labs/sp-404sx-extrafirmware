"""SP-404SX / SP-404A system update container (sp404sx_up.bin).
Layout: u32be L | u32be unpacked_size | LZSS stream ... | zero pad | u16be sum(file[4:L-2]) | 48-byte tag
LZSS: Okumura (N=4096, F=18, THRESHOLD=2, ring init 0x00 at N-F, flag bits LSB-first, 1=literal).
Verified by the updater after flashing (byte sum, 16 bit)."""
import struct,sys
N,F,TH=4096,18,2
def unpack(f):
    L,size=struct.unpack('>II',f[:8])
    buf=bytearray(N); r=N-F; out=bytearray(); pos=8; end=L-2
    while len(out)<size and pos<end:
        flags=f[pos]; pos+=1
        for b in range(8):
            if len(out)>=size: break
            if flags>>b&1:
                c=f[pos]; pos+=1; out.append(c); buf[r]=c; r=(r+1)&(N-1)
            else:
                i=f[pos]|((f[pos+1]&0xf0)<<4); j=(f[pos+1]&15)+TH; pos+=2
                for k in range(j+1):
                    c=buf[(i+k)&(N-1)]; out.append(c); buf[r]=c; r=(r+1)&(N-1)
    return bytes(out[:size]), f[L:]
def compress(d):
    # greedy LZSS with hash chains; emits stream decodable by unpack()
    out=bytearray(); n=len(d)
    # virtual buffer: ring starts zero-filled; we only match within real history (simpler, valid)
    heads={}; prev=[-1]*n
    i=0; flagpos=None; bit=8
    def key(p): return d[p:p+3]
    while i<n:
        if bit==8: flagpos=len(out); out.append(0); bit=0
        best=0; bpos=0
        if i+3<=n:
            p=heads.get(key(i),-1); tries=0
            while p>=0 and i-p<=N-F and tries<64:
                l=0
                while l<F and i+l<n and d[p+l]==d[i+l]: l+=1
                if l>best: best,bpos=l,p
                if best==F: break
                p=prev[p]; tries+=1
        if best>TH:
            ring=(N-F+bpos)&(N-1)
            out+=bytes([ring&0xff,((ring>>4)&0xf0)|(best-TH-1)])
            step=best
        else:
            out[flagpos]|=1<<bit; out.append(d[i]); step=1
        for q in range(i,i+step):
            if q+3<=n: k=key(q); prev[q]=heads.get(k,-1); heads[k]=q
        i+=step; bit+=1
    return bytes(out)
def pack(img,tag):
    body=compress(img)
    L=8+len(body)+2
    L=(L+15)&~15  # 16-byte alignment, like the official file
    f=bytearray(struct.pack('>II',L,len(img))+body)
    f+=bytes(L-2-len(f))
    f+=struct.pack('>H',sum(f[4:])&0xffff)
    return bytes(f)+tag
if __name__=='__main__':
    f=open(sys.argv[1],'rb').read()
    img,tag=unpack(f)
    print('unpacked',hex(len(img)),'tag',tag.rstrip(b'\0'))
    g=pack(img,tag); img2,tag2=unpack(g)
    L=struct.unpack('>I',g[:4])[0]
    print('repacked',len(g),'roundtrip ok' if img2==img and tag2==tag else 'MISMATCH','sum ok' if sum(g[4:L-2])&0xffff==struct.unpack('>H',g[L-2:L])[0] else 'BAD SUM')

# ---- minimal-change repack: keep Roland's own token stream, only rewrite tokens touching changed bytes ----
def tokens(f):
    """Decode the original stream into tokens: ('L', byte) or ('R', ring_pos, length), plus outputs."""
    L,size=struct.unpack('>II',f[:8]); pos=8; out=0; toks=[]
    while out<size:
        flags=f[pos]; pos+=1
        for b in range(8):
            if out>=size: break
            if flags>>b&1:
                toks.append(('L',f[pos],out)); pos+=1; out+=1
            else:
                i=f[pos]|((f[pos+1]&0xf0)<<4); j=(f[pos+1]&15)+TH+1; pos+=2
                toks.append(('R',i,j,out)); out+=j
    return toks
def splice(orig_file, new_img):
    """Re-serialize the original token list, replacing only tokens whose output covers a changed byte,
    or which copy from a changed byte, with literals. Output positions (and so every other reference) stay valid."""
    old_img,tag=unpack(orig_file)
    assert len(old_img)==len(new_img)
    changed=[i for i in range(len(new_img)) if old_img[i]!=new_img[i]]
    toks=tokens(orig_file)
    def src_positions(t):
        _,i,j,out=t
        # ring position i holds output byte p where (N-F+p)&(N-1)==i, most recent p < out
        p=out-((( (N-F+out) - i) - 1) & (N-1)) - 1
        return [p+k for k in range(j)]
    cs=set(changed); new=[]; rewritten=0
    for t in toks:
        if t[0]=='L':
            new.append(('L',new_img[t[2]],t[2])); continue
        _,i,j,out=t
        srcs=src_positions(t)
        if any(out+k in cs for k in range(j)) or any(s in cs for s in srcs if s>=0):
            for k in range(j): new.append(('L',new_img[out+k],out+k))
            rewritten+=1
        else: new.append(t)
    body=bytearray()
    for g in range(0,len(new),8):
        grp=new[g:g+8]; flags=0; payload=bytearray()
        for b,t in enumerate(grp):
            if t[0]=='L': flags|=1<<b; payload.append(t[1])
            else:
                _,i,j,_o=t; payload+=bytes([i&0xff,((i>>4)&0xf0)|(j-TH-1)])
        body.append(flags); body+=payload
    L=8+len(body)+2; L=(L+15)&~15
    f=bytearray(struct.pack('>II',L,len(new_img))+body); f+=bytes(L-2-len(f))
    f+=struct.pack('>H',sum(f[4:])&0xffff)
    return bytes(f)+tag, rewritten
