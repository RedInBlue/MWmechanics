import csv, sys, math, statistics as st
def f(r,k):
    try: return float(r[k])
    except: return float('nan')
def pct(v,p):
    v=sorted(x for x in v if x==x); return v[min(len(v)-1,int(len(v)*p))] if v else float('nan')
def med(v):
    v=[x for x in v if x==x]; return st.median(v) if v else float('nan')
for path in sys.argv[1:]:
    rows=list(csv.DictReader(open(path,newline='')))
    g=rows[0]['game']; T=sum(f(r,'dt') for r in rows)
    print(f'\n===== {g}: {len(rows)} ticks {T:.1f}s; drift states {sorted({r["drift_state"] for r in rows})}, burnout states {sorted({r["burnout_state"] for r in rows})}')
    # steering rate
    rates=[]
    for a,b in zip(rows,rows[1:]):
        dt=f(b,'dt'); 
        if dt>0: rates.append(abs(f(b,'steer_deg')-f(a,'steer_deg'))/dt)
    print(f' steering wheel-angle rate deg/s p50 {pct(rates,.5):.1f} p95 {pct(rates,.95):.1f} max {max(rates):.1f}')
    print(' mph    n  steer_max  steerdeg@full  yaw_gain  slipF  slipR  tracF<1% tracR<1% tboostF tboostR yawctl  muF95 muR95 carslip95')
    for lo,hi in ((10,20),(20,30),(30,40),(40,50),(50,60)):
        b=[r for r in rows if lo<=f(r,'speed_mph')<hi and int(r['gear'])>1]
        if len(b)<30: continue
        full=[abs(f(r,'steer_deg')) for r in b if abs(f(r,'steer'))>0.95]
        gains=[]
        for r in b:
            sd=f(r,'steer_deg'); v=f(r,'speed_mph')/2.23694
            if abs(sd)>2 and v>3: gains.append(f(r,'yaw_rate')/(v*math.tan(math.radians(sd))))
        corner=[r for r in b if abs(f(r,'steer'))>0.3]
        sf=med([abs(f(r,'fl_slip_deg'))+abs(f(r,'fr_slip_deg')) for r in corner])/2
        sr=med([abs(f(r,'rl_slip_deg'))+abs(f(r,'rr_slip_deg')) for r in corner])/2
        tf=100*sum(1 for r in corner for t in('fl','fr') if f(r,t+'_traction')<0.999)/max(1,2*len(corner))
        tr=100*sum(1 for r in corner for t in('rl','rr') if f(r,t+'_traction')<0.999)/max(1,2*len(corner))
        def mu(r,a,c):
            L=f(r,a+'_load')+f(r,c+'_load'); return (abs(f(r,a+'_lat'))+abs(f(r,c+'_lat')))/L if L>100 else float('nan')
        print(f'{lo:>3}-{hi:<3}{len(b):>5} {med([f(r,"steer_max_deg") for r in b]):>8.2f}  {med(full):>11.2f}  {med(gains):>8.3f} {sf:>6.2f} {sr:>6.2f} {tf:>7.1f} {tr:>7.1f} '
              f'{med([f(r,"fl_tractboost") for r in corner]):>7.3f} {med([f(r,"rl_tractboost") for r in corner]):>7.3f} {med([f(r,"yaw_control") for r in b]):>6.3f} '
              f'{pct([mu(r,"fl","fr") for r in corner],.95):>5.2f} {pct([mu(r,"rl","rr") for r in corner],.95):>5.2f} {pct([abs(f(r,"car_slip_deg")) for r in corner],.95):>8.2f}')
