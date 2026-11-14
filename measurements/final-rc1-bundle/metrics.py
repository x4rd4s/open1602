#!/usr/bin/env python3
# usage: metrics.py <measure-out-dir> : writes ARM/PHASE/GAME.metrics.json for every run
#
# From the AI log of a build with the gate's logging patch (both arms the same, it only logs):
#   DIAG gate farm B at X,Y island I city C: reach K   once per farm a computer placed, at its first
#                                                        10-minute sample; K = 1 if the carts of one of
#                                                        its city's markets or Kontors reach it by road
#   DIAG gate food city I/C: made M eaten E (1/32 t over N min), inhabitants P   every 5 minutes
# unreachable_farms: the farms with reach 0 (lower is better).
# food_balance_gap60: as in the offroad gate (final-offroad-g4b/metrics.py); reported only.
import re, json, sys, os, collections, glob

FARM = re.compile(r'DIAG gate farm (\d+) at (\d+),(\d+) island (\d+) city (\d+): reach (\d)')
FOOD = re.compile(r'DIAG gate food city (\S+): made (\d+) eaten (\d+) \(1/32 t over (\d+) min\)')


def run(base):
    food = collections.defaultdict(dict)
    farms = 0
    unreachable = 0
    for l in open(base + '.err', errors='replace'):
        m = re.match(r'\[ai (\d+) (\d+)s\] (.*)', l)
        if not m:
            continue
        p = int(m.group(1)); t = int(m.group(2)) / 60; s = m.group(3)
        x = FARM.match(s)
        if x:
            farms += 1
            if x.group(6) == '0':
                unreachable += 1
            continue
        x = FOOD.match(s)
        if x and int(x.group(4)) > 0:
            food[(p, x.group(1))][round(t)] = (int(x.group(2)) - int(x.group(3))) / 32 / int(x.group(4))
    # The first lasting food gap of each home city (a 5-minute balance below 0 with at least 80 % of
    # the next hour's samples, at least 10, below 0 too), and its balance an hour later.
    gap_sum = 0.0; gaps = 0
    for k, S in food.items():
        ts = sorted(S)
        for t in ts:
            w = [S[u] for u in ts if t <= u <= t + 60]
            if S[t] < 0 and len(w) >= 10 and sum(1 for v in w if v < 0) >= 0.8 * len(w):
                t60 = [u for u in ts if abs(u - (t + 60)) <= 5]
                if t60:
                    gaps += 1; gap_sum += S[min(t60, key=lambda u: abs(u - (t + 60)))]
                break
    return dict(unreachable_farms=unreachable, farms=farms, food_balance_gap60=round(gap_sum, 3), gap_cities=gaps)


out = sys.argv[1]
for txt in glob.glob(os.path.join(out, '*', '*', '*.txt')):
    base = txt[:-4]
    if not os.path.exists(base + '.err'):
        continue
    json.dump(run(base), open(base + '.metrics.json', 'w'), indent=1)
print('metrics written')
