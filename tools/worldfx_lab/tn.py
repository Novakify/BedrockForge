import sys
from lab import *
L=Lab(640,360)
sc=island_world()
ims=[]
def mk(T, elim):
    m=Match()
    m.team((0.5,64,3.5),(0.1,0.35,1.0)); m.team((40.5,64,0.5),(1.0,0.1,0.1),elim=elim, broke=20.0 if elim else -1,vis=False)
    m.team((-40.5,64,0.5),(0.1,0.9,0.2),elim=elim, broke=20.0 if elim else -1,vis=False); m.team((0.5,64,40.5),(1.0,0.85,0.1),vis=False)
    m.a[m.m2.start,3]=T; return m
cam=Cam((6,70,-22),yaw=8,pitch=4,fov=80)
ims.append(L.render(cam,sc,mk(0,0),time=12.0,phase=1.0))
# find a moment with a strike
import math
def h1(x):
    v=math.sin(x*91.3458)*47453.5453; return v-math.floor(v)
T=0.67; period=8.0+(2.6-8.0)*T
best=None
for cyc in range(3,60):
    when=0.3+(period-1.3)*h1(cyc*1.7); az=h1(cyc*3.1)*6.2832
    d=(math.cos(az),math.sin(az))
    if d[1]>0.85: best=cyc*period+when+0.03; break
ims.append(L.render(cam,sc,mk(T,1.0),time=best or 12.0,phase=1.6))
ims.append(L.render(cam,sc,mk(T,1.0),time=(best or 12)+1.7,phase=1.6))
sheet(ims,3,'tn.jpg')
