import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), ".."))
# MOCK multibook_v1 viz payload: the single_v1 mock payload + 7 synthetic cross-book columns (z, loadings, names, feature_group). Display fixture only.
import json, random, copy
random.seed(9)
v = json.load(open((VT_ROOT + '/preview/synthetic_viz_payloads.json'))); s = copy.deepcopy(v['static'])
NAMES = ["Family net GEX sign", "Family net GEX share", "Dist. to family flip %", "Flip dispersion (agreement)", "Skew divergence", "Term-structure divergence", "Weighted OI/gamma concentration"]
SHORT = ["FamGEX±", "FamGEX%", "FamFlip%", "FlipDisp", "SkewDiv", "TermDiv", "OIconc"]
s['feature_names'] += NAMES; s['feature_short'] += SHORT; s['feature_group'] = ['anchor'] * 17 + ['cross_book'] * 7; s['batch_id'] = 'NQ-multibook_v1-MOCK'
L = s['pca_meta']['loadings']; nc = len(L[0])
for _ in range(7): L.append([random.uniform(-.35, .35) for _ in range(nc)])
for p in s['points']:
    dom = p['dominant'] if isinstance(p.get('dominant'), int) else 0
    p['z'] = p['z'] + [round(random.gauss((dom - 2) * 0.5, 0.8), 3) for _ in range(7)]
live = []
for l in v['live']:
    l = copy.deepcopy(l); l['viz']['z'] = l['viz']['z'] + [round(random.gauss(0, .8), 3) for _ in range(7)]; live.append(l)
json.dump({'static': s, 'live': live}, open((VT_ROOT + '/preview/synthetic_viz_payloads_mb.json'), 'w')); print('ok', len(s['feature_short']))
