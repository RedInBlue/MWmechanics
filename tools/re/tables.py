import sys, struct; sys.path.insert(0,'.')
from pe import *
T={
 'TwoDegreeTable':[0.0,1.2,2.3,3.0,3.0,2.8],
 'FourDegreeTable':[0.0,1.7,3.2,4.3,5.1,5.2],
 'TwelveDegreeTable':[0.0,1.9,3.8,5.2,6.3,7.1],
 'SteeringRangeData':[40.0,20.0,10.0,5.5,4.5,3.25,2.9,2.9,2.9,2.9],
 'SteeringSpeedData':[1.0,1.0,1.0,0.56,0.5,0.35,0.3,0.3,0.3,0.3],
 'SteeringWheelRangeData':[45.0,15.0,11.0,8.0,7.0,7.0,7.0,7.0,7.0,7.0],
 'DriftRearFrictionData':[1.1,0.95,0.87,0.77,0.67,0.6,0.51,0.43,0.37,0.34],
 'SteeringInputRangeData':[1.0,1.0,1.1,1.2,1.25,1.35],
}
for path in sys.argv[1:]:
    d,b,s=load(path); print('==',path)
    for k,v in T.items():
        bs=struct.pack('<%df'%len(v),*v)
        hits=[]; i=d.find(bs)
        while i!=-1: hits.append(hex(off2va(s,i))); i=d.find(bs,i+1)
        # also partial: first 3 values
        part=[]; bs3=struct.pack('<3f',*v[:3]); i=d.find(bs3)
        while i!=-1: part.append(hex(off2va(s,i))); i=d.find(bs3,i+1)
        print(f'  {k:24s} exact={hits} prefix3={part[:6]}')
