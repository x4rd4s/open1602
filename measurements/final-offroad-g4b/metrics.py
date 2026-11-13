#!/usr/bin/env python3
# usage: metrics.py <measure-out-dir> : writes ARM/PHASE/GAME.metrics.json for every run
import re,json,sys,os,collections,glob
def per_player(txt):
    P=collections.defaultdict(dict); m=None; L=open(txt).read().split('\n')
    for l in L:
        a=re.match(r'^\s+(\d+)\s+[\d.]+\s',l)
        if a: m=int(a.group(1)); continue
        y=re.match(r'^\s+player (\d+): (\d+) cities, (\d+) inhabitants, (-?\d+) gold',l)
        if y and m is not None: P[int(y.group(1))][m]=int(y.group(4))
    return P
def run(base):
    gold=per_player(base+'.txt'); food=collections.defaultdict(dict); first={}; found=[]; net60={}
    for l in open(base+'.err',errors='replace'):
        m=re.match(r'\[ai (\d+) (\d+)s\] (.*)',l)
        if not m: continue
        p=int(m.group(1)); t=int(m.group(2))/60; s=m.group(3)
        x=re.match(r'DIAG gate food city (\S+): made (\d+) eaten (\d+) \(1/32 t over (\d+) min\)',s)
        if x and int(x.group(4))>0:
            food[(p,x.group(1))][round(t)]=(int(x.group(2))-int(x.group(3)))/32/int(x.group(4)); continue
        x=re.match(r'found a city on island (\d+) at .*: built',s)
        if x:
            if p not in first: first[p]=x.group(1)
            else: found.append((p,t,x.group(1)))
            continue
        x=re.match(r'DIAG g4m city (\d+)/(\d+) at 60 min after founding: net (-?\d+)',s)
        if x: net60[(p,x.group(1),x.group(2))]=int(x.group(3))
    gap_sum=0.0; gaps=0
    for k,S in food.items():
        ts=sorted(S)
        for t in ts:
            w=[S[u] for u in ts if t<=u<=t+60]
            if S[t]<0 and len(w)>=10 and sum(1 for v in w if v<0)>=0.8*len(w):
                t60=[u for u in ts if abs(u-(t+60))<=5]
                if t60:
                    gaps+=1; gap_sum+=S[min(t60,key=lambda u:abs(u-(t+60)))]
                break
    low=0
    for p,t,isl in found:
        ts=[u for u in sorted(gold.get(p,{})) if u<=t]
        if ts and gold[p][ts[-1]]<3000: low+=1
    homes={(p,i) for p,i in first.items()}
    nets=[v for (p,i,c),v in net60.items() if (p,i) not in homes]
    return dict(food_balance_gap60=round(gap_sum,3),gap_cities=gaps,low_gold_foundings=low,foundings=len(found),
                colony_net_60=(round(sum(nets)/len(nets),1) if nets else "n/a"))
out=sys.argv[1]
for txt in glob.glob(os.path.join(out,'*','*','*.txt')):
    base=txt[:-4]
    if not os.path.exists(base+'.err'): continue
    json.dump(run(base),open(base+'.metrics.json','w'),indent=1)
print('metrics written')
