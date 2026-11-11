import sys
from lab import *
L=Lab(480,300,recompile='-n' not in sys.argv)
sc=flat_world()
ims=[]
for brk in [-1,-1,0.03,0.15,0.3,0.5,0.8,1.2,2.0,2.6]:
    front = len(ims)==1
    cam=Cam.look((0.5,65.6,-3.2) if front else (3.4,65.8,-2.6),(0.5,64.7,1.0),fov=58)
    m=Match(); m.team((0.5,64,1.5),(0.1,0.35,1.0),yaw=3.14159,broke=brk)
    m.a[m.m2.start,2]=1.0
    ims.append(L.render(cam,sc,m,time=10+max(brk,0)))
sheet(ims,5,'fb.jpg')
