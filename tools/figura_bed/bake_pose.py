# Poses a Figura avatar (figura_avatar.json) at time T of one animation, the way BF's Figura runtime does (Model.cpp
# Part::recalculate, Anim channel signs), and saves every visible mesh's posed vertices.
import json, math, sys
import numpy as np
A=json.load(open(sys.argv[1]))
T=float(sys.argv[2]) if len(sys.argv)>2 else 5.20833
ANIM=int(sys.argv[3]) if len(sys.argv)>3 else 10
def Rx(d):
    r=math.radians(d);c,s=math.cos(r),math.sin(r);return np.array([[1,0,0,0],[0,c,-s,0],[0,s,c,0],[0,0,0,1.]])
def Ry(d):
    r=math.radians(d);c,s=math.cos(r),math.sin(r);return np.array([[c,0,s,0],[0,1,0,0],[-s,0,c,0],[0,0,0,1.]])
def Rz(d):
    r=math.radians(d);c,s=math.cos(r),math.sin(r);return np.array([[c,-s,0,0],[s,c,0,0],[0,0,1,0],[0,0,0,1.]])
def Tr(v):
    m=np.eye(4);m[:3,3]=v;return m
def sample(frames,t):
    fr=sorted(frames,key=lambda f:f['time'])
    pts=[(f['time'],np.array(f['pre'],float)) for f in fr if isinstance(f['pre'][0],(int,float))]
    if not pts: return np.zeros(3)
    if t<=pts[0][0]: return pts[0][1]
    for (t0,a),(t1,b) in zip(pts,pts[1:]):
        if t0<=t<=t1:
            u=0 if t1==t0 else (t-t0)/(t1-t0); return a+(b-a)*u
    return pts[-1][1]
out=[]
def walk(p,parent,vis,path=''):
    path=path+'/'+p['name']
    rot=np.array(p.get('rot',[0,0,0]),float); piv=np.array(p.get('piv',[0,0,0]),float)
    ar=np.zeros(3); ap=np.zeros(3)
    for a in p.get('anim',[]):
        if a.get('id')==ANIM:
            d=a['data']
            if 'rot' in d: v=sample(d['rot'],T); ar=np.array([-v[0],-v[1],v[2]])
            if 'pos' in d: v=sample(d['pos'],T); ap=np.array([-v[0],v[1],v[2]])
    r=rot+ar
    ismesh='mesh_data' in p and p['mesh_data']
    R = (Rx(r[0])@Ry(r[1])@Rz(r[2])) if ismesh else (Rz(r[2])@Ry(r[1])@Rx(r[0]))
    M=Tr(ap+piv)@R@Tr(-piv)
    W=parent@M
    v=vis and p.get('vsb',True)
    if ismesh and v:
        md=p['mesh_data'];vt=np.array(md['vtx'],float).reshape(-1,3)
        h=np.c_[vt,np.ones(len(vt))]@W.T
        out.append((p['name'],h[:,:3],path))
    for c in p.get('chld',[]): walk(c,W,v,path)
model=[c for c in A['models']['chld'] if c['name']=='model'][0]
root=[c for c in model['chld'] if c['name']=='root'][0]
walk(root,np.eye(4),True)
import pickle;pickle.dump(out,open('posed_%d_%g.pkl'%(ANIM,T),'wb'))
for n,v,_ in out: print(n,len(v),v.min(0).round(2),v.max(0).round(2))
