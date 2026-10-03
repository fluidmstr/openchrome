"""Quick 3-view wireframe PNG of an OBJ: objview.py in.obj out.png"""
import sys, numpy as np
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
v, f = [], []
for l in open(sys.argv[1]):
    if l.startswith('v '): v.append([float(x) for x in l.split()[1:4]])
    elif l.startswith('f '): f.append([int(x.split('/')[0]) - 1 for x in l.split()[1:4]])
v, f = np.array(v), np.array(f)
fig, ax = plt.subplots(1, 3, figsize=(15, 5))
for a, (i, j) in zip(ax, [(0, 1), (0, 2), (2, 1)]):
    for t in f[:6000]:
        p = v[t][:, [i, j]]; a.fill(p[:, 0], p[:, 1], fill=False, lw=.2, color='k')
    a.set_aspect('equal'); a.axis('off')
fig.savefig(sys.argv[2], dpi=60)
