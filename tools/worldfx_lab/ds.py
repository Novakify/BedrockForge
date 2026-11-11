# death scene sheet: python3 ds.py <scene> [slot] [times...]
import sys
from lab import *
sn=int(sys.argv[1]); slot=int(sys.argv[2]) if len(sys.argv)>2 else 0
times=[float(x) for x in sys.argv[3:] if x!='-n'] or [0.2,0.5,0.8,1.1,1.5,1.9,2.3,2.8,3.3,3.9]
import os
CAMP=eval(os.environ.get('CAMP','(3.2,65.9,-4.4)'))
L=Lab(480,300,recompile='-n' not in sys.argv)
sc=flat_world()
ims=[]
for t in times:
    cam=Cam.look(CAMP,(0.5,65.0,0.5),fov=58)
    m=Match(); m.event((0,64,0),3,t,scene=sn+16*(slot+1),col=(0.2,0.55,1.0))
    ims.append(L.render(cam,sc,m,time=10+t))
sheet(ims,5,f'ds{sn}.jpg')
