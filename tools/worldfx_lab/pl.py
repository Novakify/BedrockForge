# placed / broken block sheet
import sys
from lab import *
L=Lab(480,300,recompile='-n' not in sys.argv)
boxes=[(0,64,0,1,65,1,(0.8,0.15,0.15)),(1,64,0,2,65,1,(0.8,0.15,0.15)),(2,64,0,3,65,1,(0.8,0.15,0.15))]
ims=[]
for t in [0.03,0.10,0.18,0.26,0.34,0.40,0.44,0.55,0.8,1.2]:
    cam=Cam.look((4.2,66.6,-3.0),(3.3,64.6,0.5),fov=58)
    m=Match()
    sc=flat_world(y=30,extra=boxes+([(3,64,0,4,65,1,(0.8,0.15,0.15))] if t>0.02 else []))
    m.event((3,64,0),2,t,col=(0.8,0.15,0.15),seed=1234.0)
    m.event((0,64,0),1,t*2,col=(0.8,0.15,0.15),seed=99.0,floorDy=-1)
    ims.append(L.render(cam,sc,m,time=10+t))
sheet(ims,5,'pl.jpg')
