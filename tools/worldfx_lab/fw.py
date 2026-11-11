import sys
from lab import *
L=Lab(960,540,recompile='-n' not in sys.argv)
sc=island_world()
ims=[]
for t in [1.2,1.9,2.5,3.4,6.9,7.6]:
    cam=Cam((0,66.6,-8),yaw=0,pitch=-22,fov=42)
    m=Match(); m.event((0,64,0),6,t,col=(0.2,0.55,1.0),seed=777.0)
    ims.append(L.render(cam,sc,m,time=10+t,phase=2.0))
sheet(ims,3,"fw.jpg",0.67)
