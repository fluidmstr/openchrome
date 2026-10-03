import sys,struct,numpy as np; sys.path.insert(0,'tools'); from rpack import Pack
def find_counts(m,I):
    w=np.frombuffer(m[:len(m)//4*4],'<u4')
    for k in range(len(w)-8):
        N=w[k+2]
        if w[k+1]==0 and 1<=N<=200 and w[k+3]==0 and k+4+N<=len(w) and int(w[k+4:k+4+N].sum())==I and w[k]>0:
            return int(w[k]),int(N),w[k+4:k+4+N].tolist()
def load(pk,r):
    d=pk.by_role(r); vb,ib,m=d[0xf0],d[0xf1],d[0x10]
    I=len(ib)//2; f=find_counts(m,I)
    return d,f,I
if __name__=='__main__':
    pk=Pack("F:/SteamLibrary/steamapps/common/Dying Light/DW/Data/common_meshes_PC.rpack")
    import collections; c=collections.Counter(); nf=0; ex={}
    for r in pk.resources:
        if r.flags!=0x1100005 or 3 not in r.chunks: continue
        d,f,I=load(pk,r)
        if not f: nf+=1; continue
        V,N,cnt=f; vb=len(d[0xf0]); s=vb/V
        c[(round(s,2))]+=1; ex.setdefault(round(s,2),r.name)
    print('nofind',nf); print(c.most_common(12)); print({k:ex[k] for k,_ in c.most_common(8)})
