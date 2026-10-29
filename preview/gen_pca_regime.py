import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), ".."))
# Self-consistent synthetic PCA regime fit for offline screenshots: computes a
# REAL PCA (numpy eigh on the standardized covariance) and REAL per-regime
# empirical mean/covariance in PCA space from the SAME projection -- so the
# loading-vector directions and the bubble positions are guaranteed
# consistent (this is exactly the bug the prototype had: hand-placed bubbles
# with decorative, unrelated arrow directions).
import json, numpy as np
rng = np.random.default_rng(7)

FEATURES = ["Net GEX (z)", "Gamma Flip Distance %", "Speed (z)", "Charm/session-hour (z)",
            "Color (z)", "Vanna VEX (z)", "Net Dealer Hedge Delta / DEX (z)",
            "Call OI Ratio Momentum", "Put OI Ratio Momentum", "Implied Z-Score (ATM)",
            "Implied Z-Score (25d put)", "VRP (z)", "RV-vs-IV Z-Score (TOD)",
            "25d Risk Reversal (z)", "25d Risk Reversal Momentum", "RND Skewness (z)", "RND Kurtosis (z)"]
SHORT = ["GEX", "Flip Dist", "Speed", "Charm(CEX)", "Color", "Vanna(VEX)", "DEX",
         "Call OI Mom", "Put OI Mom", "IV Z (ATM)", "IV Z (25dP)", "VRP_z", "RV/IV Z",
         "IV Skew", "Skew Mom", "RND Skew", "RND Kurt"]
IDX = {n: i for i, n in enumerate(FEATURES)}
D = len(FEATURES)

REGIMES = ["Pinning / Low-Volatility", "High-Volatility Risk", "Trending / Directional-Bias",
           "Mean-Reversion", "Transitional / Breakout"]
COLORS = ["#9aa0aa", "#e0a339", "#2dd4bf", "#9a8bf0", "#e0587c"]

def synth_day(archetype):
    f = rng.normal(0, 0.45, D)
    gex, flip, speed, rviv, ivz = IDX["Net GEX (z)"], IDX["Gamma Flip Distance %"], IDX["Speed (z)"], IDX["RV-vs-IV Z-Score (TOD)"], IDX["Implied Z-Score (ATM)"]
    if archetype == 0:   # Pinning/Low-Vol: GEX+/Flip+ (above the flip), Speed low, RV/IV low
        f[gex] += 2.2; f[flip] += 1.9; f[speed] -= 1.1; f[rviv] -= 1.0
    elif archetype == 1:  # High-Vol Risk
        f[ivz] += 2.4; f[rviv] += 2.1
    elif archetype == 2:  # Trending: GEX-/Flip- (BELOW the flip -- this is the sign the prototype got backwards)
        f[gex] -= 2.3; f[flip] -= 2.0; f[speed] += 1.9
    elif archetype == 3:  # Mean-Reversion: mild positive GEX, low speed
        f[gex] += 0.6; f[speed] -= 0.7
    elif archetype == 4:  # Transitional/Breakout: flip near zero, speed spiking
        f[flip] += rng.normal(0, 0.25); f[speed] += 1.8
    return f

N_PER = 46
raw = np.array([synth_day(a) for a in range(5) for _ in range(N_PER)])
labels = np.array([a for a in range(5) for _ in range(N_PER)])

mean = raw.mean(axis=0); std = raw.std(axis=0, ddof=1); std[std < 1e-9] = 1e-9
Z = (raw - mean) / std
cov = np.cov(Z, rowvar=False)
eigvals, eigvecs = np.linalg.eigh(cov)          # ascending
order = np.argsort(eigvals)[::-1]
eigvals, eigvecs = eigvals[order], eigvecs[:, order]
loadings = eigvecs[:, :3]                        # D x 3, REAL fitted directions
total_var = eigvals.sum()
ev3 = eigvals[:3]

proj = Z @ loadings                               # N x 3, REAL projected points -- same transform as the arrows

ARROW_SCALE = 6.0
ellipsoids = []
for a in range(5):
    pts = proj[labels == a]
    center = pts.mean(axis=0)
    c3 = np.cov(pts, rowvar=False)
    w, v = np.linalg.eigh(c3)
    order2 = np.argsort(w)[::-1]
    w, v = w[order2], v[:, order2]
    axes = [[float(v[0, k]), float(v[1, k]), float(v[2, k]), float(np.sqrt(max(0, w[k])) * 1.6)] for k in range(3)]
    ellipsoids.append({"regime": a, "name": REGIMES[a], "color": COLORS[a],
                        "center": center.tolist(), "axes": axes})

points = [{"pc1": float(p[0]), "pc2": float(p[1]), "pc3": float(p[2]), "regime": int(l)} for p, l in zip(proj, labels)]

static_payload = {
    "explained_variance": {"pc1": float(ev3[0] / total_var), "pc2": float(ev3[1] / total_var), "pc3": float(ev3[2] / total_var),
                            "cumulative": float(ev3.sum() / total_var)},
    "n_days_in_fit": int(len(raw)),
    "regimes": [{"name": n, "color": c} for n, c in zip(REGIMES, COLORS)],
    "ellipsoids": ellipsoids,
    "points": points,
    "arrow_scale": ARROW_SCALE,
    "loadings": [{"name": FEATURES[i], "short": SHORT[i], "pc1": float(loadings[i, 0]),
                  "pc2": float(loadings[i, 1]), "pc3": float(loadings[i, 2])} for i in range(D)],
}

# ---- live "today" observation: a Trending-archetype day with fresh noise, so
# the white dot should land inside/near the teal (Trending) bubble, and the
# Flip Dist arrow (real fitted direction) should point AWAY from Trending
# since Trending = negative flip distance, not toward it. ----
live_raw = synth_day(2)
live_raw[IDX["Net GEX (z)"]] += rng.normal(0, 0.2)
live_std = (live_raw - mean) / std
live_pca = live_std @ loadings
contributions = [{"name": FEATURES[i], "pull": [float(live_std[i] * loadings[i, 0]),
                                                  float(live_std[i] * loadings[i, 1]),
                                                  float(live_std[i] * loadings[i, 2])]} for i in range(D)]

# session trail: interpolate from near-origin (pre-market baseline) to the live point
trail = [[float(t) for t in (live_pca * k / 8)] for k in range(9)]

probs_raw = np.array([-np.linalg.norm(proj[labels == a].mean(axis=0) - live_pca) for a in range(5)])
probs = np.exp(probs_raw - probs_raw.max()); probs /= probs.sum()
predicted = int(np.argmax(probs))

live_payload = {
    "predicted_regime": REGIMES[predicted],
    "predicted_color": COLORS[predicted],
    "probabilities": [{"name": REGIMES[a], "color": COLORS[a], "p": float(probs[a])} for a in range(5)],
    "pca_coords": {"pc1": float(live_pca[0]), "pc2": float(live_pca[1]), "pc3": float(live_pca[2])},
    "trail": trail,
    "directional_bias_bullish": False,
    "triggers": {"soft_active": True, "hard_active": False,
                 "flip_distance_pct": float(live_raw[IDX["Gamma Flip Distance %"]]), "speed_value": float(live_raw[IDX["Speed (z)"]])},
    "decay_path_deviation": 1.31,
    "mean_reversion_override_active": False,
    "feature_contributions": contributions,
    "narrative": f"{REGIMES[predicted]} read, dominated by Net GEX (z) ({live_raw[IDX['Net GEX (z)']]:+.2f}), "
                 f"Gamma Flip Distance % ({live_raw[IDX['Gamma Flip Distance %']]:+.2f}), and Speed (z) ({live_raw[IDX['Speed (z)']]:+.2f}).",
}

json.dump({"static": static_payload, "live": live_payload},
          open((VT_ROOT + "/preview/synthetic_regime_payloads.json"), "w"))
print("wrote real-fit PCA regime payloads:", "predicted =", REGIMES[predicted])
print("Flip Dist loading vector:", loadings[IDX["Gamma Flip Distance %"]].round(3))
print("Trending bubble center:  ", ellipsoids[2]["center"])
