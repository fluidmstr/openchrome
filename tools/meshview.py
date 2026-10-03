import sys,numpy as np; sys.path.insert(0,'tools'); from rpack import Pack
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
def decode(vb,stride):
    a=np.frombuffer(vb,np.uint8)[:len(vb)//stride*stride].reshape(-1,stride)
    if stride==20: return np.ascontiguousarray(a[:,:6]).view('<f2').astype(np.float32).reshape(-1,3)
    return np.ascontiguousarray(a[:,:12]).view('<f4').reshape(-1,3)
def render(pos,ib,out):
    idx=np.frombuffer(ib[:len(ib)//6*6],'<u2').reshape(-1,3).astype(int)
    idx=idx[(idx<len(pos)).all(1)]
    fig,ax=plt.subplots(1,3,figsize=(12,4))
    for a,(i,j) in zip(ax,[(0,1),(0,2),(2,1)]):
        for t in idx[:3000]:
            p=pos[t][:,[i,j]]; a.fill(p[:,0],p[:,1],fill=False,lw=.3,color='k')
        a.set_aspect('equal'); a.axis('off')
    fig.savefig(out,dpi=70); plt.close(fig)
if __name__=='__main__':
    pk=Pack("F:/SteamLibrary/steamapps/common/Dying Light/DW/Data/common_meshes_PC.rpack")
    for nm,st in [('0x2_lod2',20),('ads_d',32),('anim_fuse_boxes_lever',40),('ot_barrier_c',None)]:
        r=[r for r in pk.resources if r.name==nm and r.flags==0x1100005][0]; d=pk.by_role(r)
        if st is None: continue
        pos=decode(d[0xf0],st); print(nm,pos.min(0),pos.max(0)); render(pos,d[0xf1],f'out/mesh_{nm}.png')
