import csv, sys, statistics as st
def f(r,k):
    try: return float(r[k])
    except: return float('nan')
for path in sys.argv[1:]:
    rows=list(csv.DictReader(open(path,newline='')))
    g=rows[0]['game']; T=sum(f(r,'dt') for r in rows)
    print(f'\n===== {g}: {len(rows)} ticks, {T:.1f}s, rate {len(rows)/T:.0f} Hz')
    print(' const: yaw_speed', rows[-1]['yaw_speed'], 'pitch-/+', rows[-1]['pitch_neg_scale'], rows[-1]['pitch_pos_scale'], 'roll', rows[-1]['roll_scale'])
    print(' flags seen', sorted({r['flags'] for r in rows}), 'gears', sorted({r['gear'] for r in rows}), 'styles', sorted({r['driver_style'] for r in rows}))
    # find launch: first tick after which gas>0.9 and speed<3 mph and flags==0
    starts=[i for i in range(1,len(rows)) if f(rows[i],'gas')>0.9 and f(rows[i],'speed_mph')<3 and int(rows[i]['flags'])&1==0 and (int(rows[i-1]['flags'])&1==1 or f(rows[i-1],'gas')<0.9)]
    print(' launch candidates (tick idx):', starts[:5])
    i0=starts[0] if starts else 0
    t0=float(rows[i0]['t'])
    print('   t     mph   gear gas  flg  bo_st bo_tr  FL wheel/road  slip  trac tboost   FR slip trac tboost  RL slip  steer')
    step=max(1,int(len(rows)/T*0.1))
    for r in rows[max(0,i0-step*5):]:
        t=float(r['t'])-t0
        if t>7: break
        if (rows.index(r)-i0)%step: continue
        print(f"{t:6.2f} {f(r,'speed_mph'):6.1f} {r['gear']:>3} {f(r,'gas'):4.2f} {r['flags']:>3} {r['burnout_state']:>5} {f(r,'burnout_traction'):5.2f} "
              f"{f(r,'fl_wheel_mps'):6.1f}/{f(r,'fl_road_mps'):5.1f} {f(r,'fl_slip_mps'):5.1f} {f(r,'fl_traction'):4.2f} {f(r,'fl_tractboost'):5.2f}  "
              f"{f(r,'fr_slip_mps'):5.1f} {f(r,'fr_traction'):4.2f} {f(r,'fr_tractboost'):5.2f}  {f(r,'rl_slip_mps'):5.1f} {f(r,'steer'):5.2f}")
