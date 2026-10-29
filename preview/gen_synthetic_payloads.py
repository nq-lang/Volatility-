# Synthetic bridge-shaped payloads for offline screenshots. The TERM bucket now carries
# genuine multi-expiry grids (as production data does), so surfaces are real surfaces.
import json, random, math
random.seed(42)
SPOT = 6000.0
BUCKETS = ["0DTE", "1DTE", "2DTE", "term"]
SINGLE_DTE = {"0DTE": 0, "1DTE": 1, "2DTE": 2}
TERM_DTES = [3, 7, 14, 21, 30, 45, 60]
STRIKES = [SPOT + k * 5 for k in range(-24, 25)]

def profile(k, d):
    call = 1.0 * math.exp(-((k - (SPOT + 55)) ** 2) / (2 * (28 + d * 0.8) ** 2)) \
         + 0.55 * math.exp(-((k - (SPOT + 15)) ** 2) / (2 * (16 + d * 0.5) ** 2)) \
         + 0.35 * math.exp(-((k - (SPOT + 95)) ** 2) / (2 * (18 + d * 0.5) ** 2))
    put = 1.05 * math.exp(-((k - (SPOT - 50)) ** 2) / (2 * (30 + d * 0.8) ** 2)) \
        + 0.6 * math.exp(-((k - (SPOT - 15)) ** 2) / (2 * (14 + d * 0.5) ** 2)) \
        + 0.3 * math.exp(-((k - (SPOT - 100)) ** 2) / (2 * (20 + d * 0.5) ** 2))
    decay = math.exp(-d / 38.0) + 0.12
    return (call - put) * decay

def model_value(model, k, d):
    p = profile(k, d); n = random.uniform(-0.03, 0.03)
    x = (k - SPOT) / 60.0
    return {
        "gex": p * 1.1e9 + n * 1e8,
        "dex": (p * 0.6 + 0.25 * math.tanh(-x)) * 4e7 + n * 1e7,
        "vanna": (p * math.cos(x * 1.6) - 0.2 * math.sin(x * 2.2)) * 3.2e7 + n * 5e6,
        "charm": (-abs(p) * 0.8 + 0.25 * p) * 2.5e6 * (1.4 if d < 8 else 0.8) + n * 4e5,
        "speed": (p - profile(k + 5, d)) * 4e5 + n * 5e4,
        "color": (-p * 0.5 + 0.1 * math.sin(x * 3)) * 6e4 + n * 1e4,
        "theta": abs(p) * 9e5 + 2e5 * math.exp(-((k - SPOT) ** 2) / (2 * 40 ** 2)) + n * 1e5,
    }[model]

def exposure(model, bucket):
    dtes = TERM_DTES if bucket == "term" else [SINGLE_DTE[bucket]]
    pts = [{"strike": k, "dte": d, "value": model_value(model, k, d)} for d in dtes for k in STRIKES]
    return {"model": model, "bucket": bucket, "points": pts}

def surf(model, channel, x_axis, bucket, xs, fn):
    dtes = TERM_DTES if bucket == "term" else [SINGLE_DTE[bucket]]
    pts = [{"x": x, "dte": d, "y": fn(x, d)} for d in dtes for x in xs]
    return {"model": model, "_channel": channel, "x_axis": x_axis, "bucket": bucket, "points": pts}

def scalar(model, value, n=0, extra=None, channel=None):
    d = {"model": model, "value": value, "n": n}
    if extra: d.update(extra)
    if channel: d["_channel"] = channel
    return d

payloads = []
for m in ["gex", "dex", "vanna", "charm", "speed", "color", "theta"]:
    for b in BUCKETS: payloads.append(exposure(m, b))

vrp_strikes = [SPOT + k * 10 for k in range(-15, 16)]
def vrp_fn(x, d):
    dist = abs(x - SPOT); dimple = -0.035 * math.exp(-dist / 45.0) * math.exp(-d / 20.0)
    return round(0.012 + 0.00012 * dist / (d + 3) + 0.006 * math.sin(d / 9.0) + dimple + random.uniform(-0.003, 0.003), 5)
rnd_strikes = [SPOT + k * 10 for k in range(-25, 26)]
def rnd_fn(x, d):
    sig = 26 + d * 5.5
    return round(math.exp(-((x - SPOT - sig * 0.12) ** 2) / (2 * sig ** 2)) / (sig * math.sqrt(2 * math.pi)) * (1 + 0.15 * math.sin(x / 40 + d)), 6)
deltas = [round(0.05 + i * 0.025, 3) for i in range(19)]
def civ_fn(dl, d):   # bloomberg-like: steep short-dated skew that flattens with term
    return round(0.125 + 0.05 * (0.5 - dl) + 0.30 * math.exp(-d / 9.0) * (0.5 - dl) ** 2 + 0.018 * math.log(1 + d / 6.0) + random.uniform(-0.0015, 0.0015), 5)
def piv_fn(dl, d):
    return round(0.14 + 0.085 * (0.5 - dl) + 0.42 * math.exp(-d / 9.0) * (0.5 - dl) ** 2 + 0.020 * math.log(1 + d / 6.0) + random.uniform(-0.0015, 0.0015), 5)
for b in BUCKETS:
    payloads.append(surf("VRP", "vrp", "strike", b, vrp_strikes, vrp_fn))
    payloads.append(surf("RND", "rnd", "strike", b, rnd_strikes, rnd_fn))
    payloads.append(surf("call_delta_iv", "call_delta_iv", "delta", b, deltas, civ_fn))
    payloads.append(surf("put_delta_iv", "put_delta_iv", "delta", b, deltas, piv_fn))

payloads.append(scalar("spot", SPOT))
payloads.append(scalar("gamma_flip_distance", -18.4, 0, {"regime": "short_gamma", "flip_level": SPOT + 18.4}, channel="gamma_flip"))
payloads.append(scalar("call_oi_ratio_momentum", 0.0182, channel="call_oi_momentum"))
payloads.append(scalar("put_oi_ratio_momentum", 0.0246, channel="put_oi_momentum"))
payloads.append(scalar("iv_zscore", 1.74, 60, {"dte": 1, "delta": 0.25}))
payloads.append(scalar("rv_iv_zscore_tod", 2.31, 48, {"bucket": "09:30-09:45"}))

insight = {
    "regime": "short-gamma / trend — D_flip -18.4pts (-0.31%) and shrinking vs. 45min ago; dealer convention: long calls / short puts (default)",
    "dominant_greek": "charm (1DTE) — building into the power hour, dominant over vanna as IV has stabilized post-open",
    "key_levels": "flip level ~6018.4; put wall 5950; call wall 6050; RND mode 6009, right tail thickening past 6040",
    "layer_agreement": "Layer 1 (flip distance) + Layer 4 (put OI momentum rising far-OTM) agree short-gamma/risk-off; Layer 3 (put-delta IV) confirms with a steepening skew; broad multi-layer confluence",
    "composite_signal": "25Δ put Implied Z-Score at +1.74 (n=60) spatially aligned with concentrated negative VEX in the same far-OTM put strikes — composite vanna-reversion setup is LIVE",
    "scenario": "if price holds below 6018 flip level, expect continued short-gamma amplification toward 5990 support; if price reclaims 6018-6025, dealer hedging flips dampening and 6050 call wall becomes the near-term magnet",
    "invalidation": "a shrinking flip distance reversing to widen back above +15pts, or the put IV z-score/VEX alignment decoupling, would invalidate this read",
    "multi_day_context": "term-level GEX is still deeply negative and consistent with the intraday short-gamma read — no term/intraday conflict right now",
    "named_patterns_active": ["Charm-Driven Close Bias (watch, pre-close)", "Fresh Tail-Hedging Confluence", "Composite Vanna-Reversion Setup"],
    "key_levels_structured": [
        {"price": 6018, "direction": "short", "conviction": "highest", "type": "breakthrough", "probability": 78},
        {"price": 6050, "direction": "long", "conviction": "highest", "type": "rejection", "probability": 64},
        {"price": 5950, "direction": "short", "conviction": "normal", "type": "magnet", "probability": 41},
        {"price": 5990, "direction": "short", "conviction": "normal", "type": "pin", "probability": 35},
    ],
}
json.dump({"model_updates": payloads, "insight": insight}, open("synthetic_payloads.json", "w"))
print("wrote", len(payloads), "payloads")
