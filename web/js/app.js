// app.js — Section 1 renders Greek-exposure surfaces (row 1, each with a 2D/3D
// toggle) then distribution/risk-pricing surfaces (row 2). Section 2 (top of
// page) shows ONE live-updating structural-read panel (not an accumulating
// feed — an earlier build mistakenly stacked cards; fixed here). The right
// sidebar is a dense, colored dealer-flow readout, aggregated client-side from
// whatever channels have published so far, modeled on the supplied reference
// design. Rows with no data yet show an explicit awaiting state rather than
// fabricated numbers.

const GREEK_SURFACE_MODELS = [
    { channel: 'gex',   title: 'GEX',            xLabel: 'strike', zLabel: 'gamma exposure', colorMode: 'signed', surfaceStyle: 'signed',
      zeroCrossLabel: 'Gamma Flip', wallLabels: ['Call Wall', 'Put Wall'],
      subtitle: 'Σ(OI×Γ) by strike — regime filter: green = long-gamma/pin, red = short-gamma/trend' },
    { channel: 'dex',   title: 'DEX',            xLabel: 'strike', zLabel: 'delta exposure', colorMode: 'signed', surfaceStyle: 'signed',
      subtitle: 'dealer hedge delta by strike — directional tilt layered on the GEX regime' },
    { channel: 'vanna', title: 'Vanna Exposure', xLabel: 'strike', zLabel: 'vanna exposure', colorMode: 'signed', surfaceStyle: 'signed',
      subtitle: 'IV-change hedging flow — the mechanism behind "Vanna Crush" melt-ups/air-pockets' },
    { channel: 'charm', title: 'Charm Exposure', xLabel: 'strike', zLabel: 'charm exposure', colorMode: 'signed', surfaceStyle: 'signed',
      subtitle: 'pure time-decay hedging flow — dominant on 0-1DTE, builds into the close' },
    { channel: 'speed', title: 'Speed',          xLabel: 'strike', zLabel: 'speed', colorMode: 'signed', surfaceStyle: 'signed',
      subtitle: '∂Γ/∂spot — how fast GEX itself reshapes near a wall (fragility, not direction)' },
    { channel: 'color', title: 'Color',          xLabel: 'strike', zLabel: 'color', colorMode: 'signed', surfaceStyle: 'signed',
      subtitle: '∂Γ/∂t — how the gamma profile melts from time alone, with zero price move' },
    { channel: 'theta', title: 'Theta Harvest',  xLabel: 'strike', zLabel: 'theta harvest', colorMode: 'signed', surfaceStyle: 'signed',
      subtitle: 'daily premium harvested by option sellers — a magnitude gauge, not a directional signal' },
];

const INSIGHT_SURFACE_MODELS = [
    { channel: 'vrp',           title: 'VRP Surface',           xLabel: 'strike', zLabel: 'IV - RV', colorMode: 'signed', surfaceStyle: 'signed',
      subtitle: 'IV minus RV — rich (green) favors fade/range, cheap (red) is a caution flag against fading moves' },
    { channel: 'rnd',           title: 'RND Surface',           xLabel: 'strike', zLabel: 'density', colorMode: 'teal', surfaceStyle: 'signed',
      wallLabels: ['Right Tail', 'Left Tail'],
      subtitle: 'market-implied probability density of terminal price — read for shape change, not shift' },
    { channel: 'call_delta_iv', title: 'Call-Delta IV Surface', xLabel: 'delta',  zLabel: 'IV', colorMode: 'blue', surfaceStyle: 'bloomberg',
      subtitle: 'IV re-indexed by call delta — skew tracking independent of spot movement' },
    { channel: 'put_delta_iv',  title: 'Put-Delta IV Surface',  xLabel: 'delta',  zLabel: 'IV', colorMode: 'red', surfaceStyle: 'bloomberg',
      subtitle: 'IV re-indexed by put delta — divergence vs. the call side above is the actual tradable signal' },
];

const panels = {};
const activeDte = 'term';

// ---- client-side dealer-flow aggregation, built up from whatever channels
// have published so far this session (values start null = "no data yet") ----
const GREEK_CHANNELS = ['gex', 'dex', 'vanna', 'charm', 'speed', 'color', 'theta'];
const flow = {
    net: {}, // channel -> bucket -> net exposure (feeds the sidebar DTE matrix)
    gex: {}, // bucket -> {net, callWall, putWall, zeroCross}
    vannaNet: null, charmNet: null, dexNet: null,
    flipDistance: null, flipRegime: null, flipLevel: null,
    callOiMomentum: null, putOiMomentum: null,
    ivZ: null, ivZn: null, ivZDelta: null, ivZDte: null,
    rvIvZ: null, rvIvZn: null, rvIvZBucket: null,
    atmVrp: null, atmIv: null,
};

let currentSpot = null;
const allPanelRefs = []; // every SurfacePanel instance, so a spot update can restyle all of them

function legendKeyHtml(colorMode) {
    if (colorMode === 'signed') return `<div class="vt-panel-legend-key">
        <span class="k"><span class="sw" style="background:rgba(83,182,128,0.85)"></span>supportive/dampening</span>
        <span class="k"><span class="sw" style="background:rgba(210,106,106,0.85)"></span>amplifying</span></div>`;
    if (colorMode === 'blue') return `<div class="vt-panel-legend-key"><span class="k"><span class="sw" style="background:rgba(54,101,199,0.8)"></span>call side</span></div>`;
    if (colorMode === 'red') return `<div class="vt-panel-legend-key"><span class="k"><span class="sw" style="background:rgba(210,106,106,0.8)"></span>put side</span></div>`;
    return '';
}

function buildPanelRow(containerEl, modelList) {
    for (const m of modelList) {
        const panelEl = document.createElement('div');
        panelEl.className = 'vt-panel vt-panel-wide';
        panelEl.dataset.channel = m.channel;
        panelEl.innerHTML = `
            <div class="vt-panel-toolbar">
                <div class="vt-panel-title">${m.title}</div>
            </div>
            <div class="vt-panel-subtitle">${m.subtitle || ''}</div>
            ${legendKeyHtml(m.colorMode)}
            <div class="vt-dte-tabs">
                ${['0DTE','1DTE','2DTE','term'].map(d =>
                    `<div class="vt-dte-tab ${d === activeDte ? 'active' : ''}" data-dte="${d}">${d}</div>`).join('')}
            </div>
            <div class="vt-panel-canvas"></div>`;
        containerEl.appendChild(panelEl);
        const canvas = panelEl.querySelector('.vt-panel-canvas');
        const panel = new SurfacePanel(canvas, {
            xLabel: m.xLabel, zLabel: m.zLabel, colorMode: m.colorMode || 'signed', spot: currentSpot,
            surfaceStyle: m.surfaceStyle || 'ridge',
            zeroCrossLabel: m.zeroCrossLabel || 'Zero-cross',
            wallLabels: m.wallLabels || ['Wall(+)', 'Wall(-)'],
        });
        panels[m.channel] = { panel, latestByDte: {} };
        allPanelRefs.push(panel);

        panelEl.querySelectorAll('.vt-dte-tab').forEach(tab => {
            tab.addEventListener('click', () => {
                panelEl.querySelectorAll('.vt-dte-tab').forEach(t => t.classList.remove('active'));
                tab.classList.add('active');
                const dte = tab.dataset.dte;
                panels[m.channel].selectedDte = dte;
                const cached = panels[m.channel].latestByDte[dte];
                // If this bucket hasn't published yet, show an explicit empty
                // state -- never leave the previous tab's data on screen under
                // the newly-selected DTE's label.
                if (cached) panels[m.channel].panel.update(cached);
                else panels[m.channel].panel.showAwaiting(dte);
            });
        });
    }
}

function buildSection1() {
    // first two Greek models sit beside the sidebar; the rest span the full width below
    buildPanelRow(document.getElementById('vt-col-greeks-lead'), GREEK_SURFACE_MODELS.slice(0, 2));
    buildPanelRow(document.getElementById('vt-col-greeks'), GREEK_SURFACE_MODELS.slice(2));
    buildPanelRow(document.getElementById('vt-col-pricing-lead'), INSIGHT_SURFACE_MODELS.slice(0, 2));
    buildPanelRow(document.getElementById('vt-col-pricing'), INSIGHT_SURFACE_MODELS.slice(2));
    renderSidebar();
    initPcaBiplot();
}

function netOf(points) { return points.reduce((s, p) => s + (p.value ?? p.y ?? 0), 0); }
function wallsOf(points) {
    // Aggregate across expiries first: the term bucket is now a real
    // (strike x expiry) grid, but walls/flip are read on the strike axis.
    const byStrike = new Map();
    for (const p of points) byStrike.set(p.strike, (byStrike.get(p.strike) || 0) + p.value);
    const sorted = [...byStrike.entries()].sort((a, b) => a[0] - b[0]).map(([strike, value]) => ({ strike, value }));
    if (!sorted.length) return { callWall: null, putWall: null, zeroCross: null };
    let zeroCross = null;
    for (let i = 1; i < sorted.length; i++) {
        const a = sorted[i - 1], b = sorted[i];
        if ((a.value <= 0 && b.value > 0) || (a.value >= 0 && b.value < 0)) {
            const t = (0 - a.value) / (b.value - a.value);
            zeroCross = a.strike + t * (b.strike - a.strike);
            break;
        }
    }
    const callWall = sorted.reduce((m, p) => (p.value > m.value ? p : m), sorted[0]);
    const putWall = sorted.reduce((m, p) => (p.value < m.value ? p : m), sorted[0]);
    return { callWall: callWall.strike, putWall: putWall.strike, zeroCross };
}

function onModelUpdate(channel, jsonPayload) {
    const data = JSON.parse(jsonPayload);

    if (panels[channel]) {
        let points;
        if (data.points && data.points.length && 'strike' in data.points[0]) {
            points = data.points.map(p => ({ x: p.strike, dte: p.dte ?? 0, y: p.value }));
        } else {
            points = data.points || [];
        }
        const dteKey = data.bucket || activeDte;
        panels[channel].latestByDte[dteKey] = points;
        const selected = panels[channel].selectedDte || activeDte;
        if (dteKey === selected) panels[channel].panel.update(points);
    }

    // client-side flow aggregation for the sidebar
    if (GREEK_CHANNELS.includes(channel) && data.points && data.bucket) {
        (flow.net[channel] = flow.net[channel] || {})[data.bucket] = netOf(data.points);
    }
    if (channel === 'gex' && data.points) {
        flow.gex[data.bucket] = Object.assign({ net: netOf(data.points) }, wallsOf(data.points));
    } else if (channel === 'vanna' && data.points && data.bucket === activeDte) {
        flow.vannaNet = netOf(data.points);
    } else if (channel === 'charm' && data.points && data.bucket === activeDte) {
        flow.charmNet = netOf(data.points);
    } else if (channel === 'dex' && data.points && data.bucket === activeDte) {
        flow.dexNet = netOf(data.points);
    } else if (channel === 'gamma_flip') {
        flow.flipDistance = data.value; flow.flipRegime = data.regime; flow.flipLevel = data.flip_level;
    } else if (channel === 'call_oi_momentum') {
        flow.callOiMomentum = data.value;
    } else if (channel === 'put_oi_momentum') {
        flow.putOiMomentum = data.value;
    } else if (channel === 'iv_zscore') {
        flow.ivZ = data.value; flow.ivZn = data.n; flow.ivZDelta = data.delta; flow.ivZDte = data.dte;
    } else if (channel === 'rv_iv_zscore_tod') {
        flow.rvIvZ = data.value; flow.rvIvZn = data.n; flow.rvIvZBucket = data.bucket;
    } else if (channel === 'vrp' && data.points && data.bucket === 'term') {
        const avg = data.points.reduce((s, p) => s + p.y, 0) / data.points.length;
        flow.atmVrp = avg;
    } else if (channel === 'spot') {
        currentSpot = data.value;
        allPanelRefs.forEach(p => { p.opts.spot = currentSpot; if (p.points) p.update(p.points); });
    } else if (channel === 'call_delta_iv' && data.points && data.bucket === 'term') {
        const near50 = data.points.slice().sort((a, b) => Math.abs(a.x - 0.5) - Math.abs(b.x - 0.5))[0];
        if (near50) flow.atmIv = near50.y;
    }
    renderSidebar();
}

// ---- sidebar rendering (dealer-flow style) ----
function fmtSigned(v, digits = 2) {
    if (v === null || v === undefined || Number.isNaN(v)) return '--';
    const s = v >= 0 ? '+' : '';
    return s + v.toFixed(digits);
}
function classFor(v, positiveIsBull = true, weakThresh = 0) {
    if (v === null || v === undefined) return 'v-weak';
    if (Math.abs(v) <= weakThresh) return 'v-weak';
    const bull = positiveIsBull ? v > 0 : v < 0;
    return bull ? 'v-bull' : 'v-bear';
}
function dot(cls) { return `<span class="vt-flow-dot ${cls === 'v-bull' ? 'dot-green' : cls === 'v-bear' ? 'dot-red' : cls === 'v-white' ? 'dot-white' : 'dot-grey'}"></span>`; }

function renderSidebar() {
    const el = document.getElementById('vt-sidebar-body');
    if (!el) return;
    if (window.VT_SIDEBAR) VT_SIDEBAR.refresh();
    const gexTerm = flow.gex['term'];
    const gexClass = classFor(gexTerm ? gexTerm.net : null, false);
    const vannaClass = classFor(flow.vannaNet, true, 0);
    const charmClass = classFor(flow.charmNet, true, 0);
    const dexClass = classFor(flow.dexNet, true, 0);
    const flipClass = flow.flipRegime === 'long_gamma' ? 'v-bull' : flow.flipRegime === 'short_gamma' ? 'v-bear' : 'v-weak';
    const skewApprox = (flow.ivZDelta === 0.25) ? flow.ivZ : null;
    const skewClass = classFor(skewApprox, false, 1.0);
    const rv = (flow.atmIv !== null && flow.atmVrp !== null) ? (flow.atmIv - flow.atmVrp) : null;

    el.innerHTML = `
        <div class="vt-flow-section">
            <div class="vt-flow-section-title">Options Force — dealer hedging pressure</div>
            <div class="vt-flow-row">${dot(gexClass)}<span class="vt-flow-label">Gamma (term)</span>
                <span class="vt-flow-val ${gexClass}">${gexTerm ? gexTerm.net.toExponential(2) : '--'}</span>
                <span class="vt-flow-note">${gexTerm ? (gexTerm.net > 0 ? '— dealers dampen moves → range/pin' : '— dealers amplify moves → trend runs') : 'awaiting data'}</span></div>
            <div class="vt-flow-row">${dot(vannaClass)}<span class="vt-flow-label">Vanna</span>
                <span class="vt-flow-val ${vannaClass}">${flow.vannaNet !== null ? flow.vannaNet.toExponential(2) : '--'}</span>
                <span class="vt-flow-note">${flow.vannaNet !== null ? (flow.vannaNet >= 0 ? 'vol ↓ → dealers BUY (rally fuel)' : 'vol ↓ → dealers SELL') : 'awaiting data'}</span></div>
            <div class="vt-flow-row">${dot(charmClass)}<span class="vt-flow-label">Charm</span>
                <span class="vt-flow-val ${charmClass}">${flow.charmNet !== null ? flow.charmNet.toExponential(2) : '--'}</span>
                <span class="vt-flow-note">${flow.charmNet !== null ? 'decay → dealer flow building into close' : 'awaiting data'}</span></div>
            <div class="vt-flow-row">${dot(dexClass)}<span class="vt-flow-label">Net Dealer Lean</span>
                <span class="vt-flow-val ${dexClass}">${fmtSigned(flow.dexNet, 2)}</span>
                <span class="vt-flow-note">${flow.dexNet !== null ? (flow.dexNet >= 0 ? '▲ supportive' : '▼ resistive') : 'awaiting data'}</span></div>
            <div class="vt-flow-row">${dot(gexTerm && flow.gex['0DTE'] ? classFor(flow.gex['0DTE'].net, false) : 'v-weak')}<span class="vt-flow-label">0DTE Gamma</span>
                <span class="vt-flow-val">${flow.gex['0DTE'] ? flow.gex['0DTE'].net.toExponential(2) : '--'}</span>
                <span class="vt-flow-note">${flow.gex['0DTE'] ? 'same-day pin/repulsion driver' : 'no 0DTE volume yet this session'}</span></div>
            <div class="vt-flow-row">${dot(skewClass)}<span class="vt-flow-label">Skew (25Δ put IV z)</span>
                <span class="vt-flow-val ${skewClass}">${skewApprox !== null ? skewApprox.toFixed(2) + 'σ' : '--'}</span>
                <span class="vt-flow-note">${skewApprox === null ? 'awaiting 25Δ reading' : (Math.abs(skewApprox) > 1.5 ? 'stretched — statistically notable' : 'normal')}</span></div>
        </div>

        <div class="vt-flow-section">
            <div class="vt-flow-section-title">Dealer exposure by DTE — net, per bucket</div>
            ${renderDteMatrix()}
        </div>

        <div class="vt-flow-section">
            <div class="vt-flow-section-title">Market State — range and volatility</div>
            <div class="vt-flow-row"><span class="vt-flow-label">VRP (ATM, term)</span>
                <span class="vt-flow-val v-white">${flow.atmVrp !== null ? flow.atmVrp.toFixed(3) : '--'}</span>
                <span class="vt-flow-note">${flow.atmVrp === null ? 'awaiting data' : (flow.atmVrp > 0.02 ? 'rich — favors range/fade' : flow.atmVrp < -0.01 ? 'compressed — caution fading moves' : 'normal')}
                    ${flow.atmIv !== null ? ` · IV ${(flow.atmIv * 100).toFixed(1)}%` : ''}${rv !== null ? ` RV ${(rv * 100).toFixed(1)}%` : ''}</span></div>
            <div class="vt-flow-row"><span class="vt-flow-label">RV vs IV (time-of-day z)</span>
                <span class="vt-flow-val ${classFor(flow.rvIvZ, false, 1.5)}">${flow.rvIvZ !== null ? flow.rvIvZ.toFixed(2) : '--'}</span>
                <span class="vt-flow-note">${flow.rvIvZ === null ? 'awaiting data' : `n=${flow.rvIvZn}, bucket ${flow.rvIvZBucket || '-'}`}</span></div>
            <div class="vt-flow-row"><span class="vt-flow-label">Call OI Momentum</span>
                <span class="vt-flow-val ${classFor(flow.callOiMomentum, true, 0.01)}">${flow.callOiMomentum !== null ? flow.callOiMomentum.toFixed(4) : '--'}</span></div>
            <div class="vt-flow-row"><span class="vt-flow-label">Put OI Momentum</span>
                <span class="vt-flow-val ${classFor(flow.putOiMomentum, false, 0.01)}">${flow.putOiMomentum !== null ? flow.putOiMomentum.toFixed(4) : '--'}</span></div>
        </div>

        <div class="vt-flow-section">
            <div class="vt-flow-section-title">Scenario — where it wants to go</div>
            ${renderScenarioSlider()}
            <div class="vt-flow-row"><span class="vt-flow-label">Regime</span>
                <span class="vt-flow-val ${flipClass}">${flow.flipRegime || '--'}</span></div>
            <div class="vt-levels-ladder">
                ${gexTerm && gexTerm.callWall !== null ? `<div class="vt-level-row"><span class="vt-level-arrow up">▲</span><span class="vt-level-price">${gexTerm.callWall.toFixed(0)}</span><span class="vt-level-desc">call wall (GEX max)</span></div>` : ''}
                ${flow.flipLevel !== null ? `<div class="vt-level-row"><span class="vt-level-arrow up">▲</span><span class="vt-level-price">${(+flow.flipLevel).toFixed(0)}</span><span class="vt-level-desc">gamma flip / zero-cross</span></div>` : ''}
                ${gexTerm && gexTerm.putWall !== null ? `<div class="vt-level-row"><span class="vt-level-arrow down">▼</span><span class="vt-level-price">${gexTerm.putWall.toFixed(0)}</span><span class="vt-level-desc">put wall (GEX min)</span></div>` : ''}
            </div>
        </div>
    `;
}

function renderDteMatrix() {
    const buckets = ['0DTE', '1DTE', '2DTE', 'term'];
    const names = { gex: 'GEX', dex: 'DEX', vanna: 'Vanna', charm: 'Charm', speed: 'Speed', color: 'Color', theta: 'Theta' };
    const cell = (v) => v === undefined ? '<td class="v-weak">--</td>'
        : `<td class="${v > 0 ? 'v-bull' : v < 0 ? 'v-bear' : 'v-weak'}">${Math.abs(v) >= 1e3 || (Math.abs(v) < 1e-2 && v !== 0) ? v.toExponential(1) : v.toFixed(2)}</td>`;
    return `<table class="vt-dte-matrix"><thead><tr><th></th>${buckets.map(b => `<th>${b === 'term' ? 'Term' : b}</th>`).join('')}</tr></thead><tbody>` +
        GREEK_CHANNELS.map(ch => `<tr><td>${names[ch]}</td>${buckets.map(b => cell((flow.net[ch] || {})[b])).join('')}</tr>`).join('') +
        `</tbody></table>`;
}

function renderScenarioSlider() {
    if (flow.flipDistance === null) return `<div class="vt-placeholder-note">awaiting gamma flip data</div>`;
    const bias = Math.max(-1, Math.min(1, flow.flipDistance / 40));
    const pct = ((bias + 1) / 2) * 100;
    const side = bias >= 0 ? 'LONG' : 'SHORT';
    const cls = bias >= 0 ? 'v-bull' : 'v-bear';
    const color = bias >= 0 ? '#2fd06a' : '#e04b4b';
    return `
        <div class="vt-flow-row"><span class="vt-flow-label">Bias</span>
            <span class="vt-flow-val ${cls}">${side} ${(Math.abs(bias) * 100).toFixed(0)}%</span>
            <span class="vt-flow-note">flip-distance driven</span></div>
        <div class="vt-scenario-slider">
            <div class="track-fill" style="left:${bias >= 0 ? 50 : pct}%; right:${bias >= 0 ? 100 - pct : 50}%; background:${color};"></div>
            <div class="dot" style="left:${pct}%; background:${color};"></div>
        </div>
        <div class="vt-scenario-endlabels"><span>SHORT</span><span>LONG</span></div>`;
}

// ---- PCA Regime Model (bottom of page) ----
let pcaStatic = null; // set once per batch refit
let pcaPanel = null;  // SurfacePanel-style 3D scatter, built lazily once Three.js/points are ready

function initPcaBiplot() {
    const el = document.getElementById('vt-pca-biplot');
    if (!el) return;
    pcaPanel = new PcaBiplotPanel(el);
    window.__pca = pcaPanel; // read-only handle for diagnostics/tests
}

function onRegimeStatic(jsonPayload) {
    pcaStatic = JSON.parse(jsonPayload);
    if (pcaPanel) pcaPanel.setStatic(pcaStatic);
    const evEl = document.getElementById('vt-pca-explained-variance');
    if (evEl) {
        const ev = pcaStatic.explained_variance;
        const cum = ev.cumulative;
        const cls = cum < 0.6 ? 'v-bear' : cum < 0.75 ? 'v-weak' : 'v-bull';
        evEl.innerHTML = `Explained variance — PC1 ${(ev.pc1*100).toFixed(0)}% · PC2 ${(ev.pc2*100).toFixed(0)}% · PC3 ${(ev.pc3*100).toFixed(0)}%
            · cumulative <b class="${cls}">${(cum*100).toFixed(0)}%</b> (n=${pcaStatic.n_days_in_fit} days)
            ${cum < 0.6 ? ' — LOW: treat cluster proximity as suggestive, not definitive' : ''}`;
    }
}

function onRegimeLive(jsonPayload) {
    const data = JSON.parse(jsonPayload);
    if (pcaPanel) pcaPanel.updateLive(data);
    renderPcaInsights(data);
    if (window.VT_SIDEBAR) VT_SIDEBAR.setRegime(data);
}

function renderPcaInsights(data) {
    const el = document.getElementById('vt-pca-insights');
    if (!el) return;
    const probRows = data.probabilities.slice().sort((a, b) => b.p - a.p).map(r => `
        <div class="vt-pca-prob-row">
            <span class="name"><span class="vt-flow-dot" style="background:${r.color}"></span>${r.name}</span>
            <div class="track"><div class="fill" style="width:${(r.p*100).toFixed(0)}%;background:${r.color}"></div></div>
            <span class="pct">${(r.p*100).toFixed(0)}%</span>
        </div>`).join('');

    const soft = data.triggers.soft_active, hard = data.triggers.hard_active;
    el.innerHTML = `
        <div class="vt-pca-card">
            <h4>Current Regime Read</h4>
            <div class="vt-pca-regime-label" style="color:${data.predicted_color}">${data.predicted_regime}</div>
            <div class="vt-pca-narrative">${data.narrative}</div>
            ${data.mean_reversion_override_active ? `<div class="vt-mr-override">Mean-Reversion regime under Charm/Vanna override risk</div>` : ''}
        </div>
        <div class="vt-pca-card">
            <h4>Regime Probability</h4>
            ${probRows}
        </div>
        <div class="vt-pca-card">
            <h4>Triggers &amp; Decay Deviation</h4>
            <div class="vt-trigger-box">
                <div class="t">Soft Warning — Speed Percentile</div>
                <div class="vt-trigger-state ${soft ? 'soft' : 'off'}">${soft ? '● ACTIVE — boundary destabilizing' : '○ off'}</div>
            </div>
            <div class="vt-trigger-box">
                <div class="t">Hard Trigger — Gamma Flip Cross</div>
                <div class="vt-trigger-state ${hard ? 'hard' : 'off'}">${hard ? '● ACTIVE — re-evaluate now' : '○ off'}</div>
            </div>
            <div class="vt-pca-greek-grid">
                <div class="vt-pca-greek-cell"><div class="name">D_FLIP%</div><div class="val ${data.triggers.flip_distance_pct >= 0 ? 'v-bull' : 'v-bear'}">${data.triggers.flip_distance_pct.toFixed(2)}</div></div>
                <div class="vt-pca-greek-cell"><div class="name">SPEED</div><div class="val v-white">${data.triggers.speed_value.toFixed(2)}</div></div>
                <div class="vt-pca-greek-cell"><div class="name">DECAY DEVIATION</div><div class="val v-white">${data.decay_path_deviation.toFixed(2)}</div></div>
                <div class="vt-pca-greek-cell"><div class="name">BIAS</div><div class="val ${data.directional_bias_bullish ? 'v-bull' : 'v-bear'}">${data.directional_bias_bullish ? 'BULLISH' : 'BEARISH'}</div></div>
            </div>
        </div>`;
}

// ---- Section 2: two distinct panels — a single current-state readout, and a
// separate, growing historical log below it (headers kept visually distinct
// so the two are never mistaken for duplicates of the same thing) ----
function regimeClass(regimeStr) {
    if (!regimeStr) return '';
    const s = regimeStr.toLowerCase();
    if (s.includes('mean-reversion') || s.includes('long-gamma')) return 'bullish';
    if (s.includes('trend') || s.includes('short-gamma')) return 'bearish';
    return '';
}

// Yellow and purple are reserved, at most one each per cycle: yellow marks
// THE single highest-conviction LONG strike, purple marks THE single highest
// -conviction SHORT strike (not "strong bullish/bearish" in general -- every
// other qualifying level, however significant, stays green/red by direction).
function levelBadgeClass(lvl) {
    const highest = lvl.conviction === 'highest';
    if (lvl.direction === 'long') return highest ? 'lvl-yellow' : 'lvl-green';
    return highest ? 'lvl-purple' : 'lvl-red';
}
function levelLabel(lvl) {
    return `${Math.round(lvl.price)} (${lvl.direction === 'long' ? 'Long' : 'Short'})`;
}
function probabilityLabel(lvl) {
    return (typeof lvl.probability === 'number') ? `${Math.round(lvl.probability)}%` : '';
}
function typeLabel(t) {
    return { magnet: 'Magnet', rejection: 'Rejection', breakthrough: 'Breakthrough', pin: 'Pin' }[t] || t || '';
}

// Wraps every mention of a qualifying level's price (integer-rounded, with or
// without a leading ~ or $, decimals tolerated) inline, wherever it appears
// in the prose -- so the level is highlighted exactly where the environment
// describing it is being stated, not just in a separate list.
function highlightLevelsInText(text, levels) {
    if (!text || !levels || !levels.length) return text || '';
    let html = text;
    // longest price first so e.g. 6018 doesn't get clipped by a shorter neighbor match
    const sorted = levels.slice().sort((a, b) => String(Math.round(b.price)).length - String(Math.round(a.price)).length);
    for (const lvl of sorted) {
        const price = Math.round(lvl.price);
        const cls = levelBadgeClass(lvl);
        const re = new RegExp('(~|\\$)?(' + price + ')(\\.[0-9]+)?', 'g');
        html = html.replace(re, (m, pre, intPart, dec) => {
            if (m.indexOf('vt-lvl-inline') !== -1) return m; // already wrapped by an earlier pass
            return `${pre || ''}<span class="vt-lvl-inline ${cls}">${intPart}${dec || ''} (${lvl.direction === 'long' ? 'Long' : 'Short'})</span>`;
        });
    }
    return html;
}

function levelEquivHtml(price) {
    const eq = window.VT_LENS ? VT_LENS.equiv(price) : [];
    return eq.length ? '<div class="vt-lvl-eq">' + eq.map(e => `<div style="--c:${e.color}">${e.text}</div>`).join('') + '</div>' : '';
}
let _lastInsight = null;
function insightCardHtml(data, ts) {
    _lastInsight = [data, ts];
    const levels = data.key_levels_structured || [];
    const row = (label, value) => value ? `<div class="vt-insight-row"><b>${label}:</b> ${highlightLevelsInText(value, levels)}</div>` : '';
    const checklist = levels.length
        ? levels.map(lv => `<div class="vt-lvl-row">
            <div class="vt-lvl-row-top">
                <span class="vt-lvl-inline ${levelBadgeClass(lv)}">${levelLabel(lv)}</span>
                <span class="vt-lvl-prob" title="confluence-based probability estimate">${probabilityLabel(lv)}</span>
            </div>
            <div class="vt-lvl-type">${typeLabel(lv.type)}</div>
            ${levelEquivHtml(lv.price)}
          </div>`).join('')
        : `<div class="vt-insight-levels-empty">no high-conviction levels this cycle</div>`;
    return `
        <div class="vt-insight-card ${regimeClass(data.regime)}">
            <div class="vt-insight-ts">${ts}</div>
            <h3>Regime: ${highlightLevelsInText(data.regime, levels) || 'n/a'}</h3>
            <div class="vt-insight-card-body">
                <div class="vt-insight-main">
                    ${row('Dominant force', data.dominant_greek)}
                    ${row('Key levels', data.key_levels)}
                    ${row('Layer agreement', data.layer_agreement)}
                    ${row('Composite signal (IV z-score × Vanna)', data.composite_signal)}
                    ${row('Scenario', data.scenario)}
                    ${row('Would break this read', data.invalidation)}
                    ${row('Multi-day context', data.multi_day_context)}
                    ${(data.named_patterns_active || []).map(p => `<span class="vt-tag">${p}</span>`).join('')}
                </div>
                <div class="vt-insight-levels">
                    <div class="vt-insight-levels-head">Key Levels</div>
                    ${window.VT_LENS && VT_LENS.chipsHtml ? `<div class="vt-lvl-chips">${VT_LENS.chipsHtml()}</div>` : ''}
                    ${checklist}
                </div>
            </div>
        </div>`;
}

function refreshInsightEquiv() { if (!_lastInsight) return; const cur = document.getElementById('vt-insight-current'); if (cur) cur.innerHTML = insightCardHtml(_lastInsight[0], _lastInsight[1]); }
window.addEventListener('DOMContentLoaded', () => {
    if (window.VT_LENS) VT_LENS.listeners.push(refreshInsightEquiv);
    document.addEventListener('click', e => { const c = e.target.closest('.vt-lvl-chips .vt-lens-chip'); if (c && window.VT_LENS) VT_LENS.toggle(c.dataset.id); });
});
function onInsightUpdate(jsonPayload) {
    const data = JSON.parse(jsonPayload);
    const ts = new Date().toLocaleTimeString();
    document.getElementById('vt-insight-current').innerHTML = insightCardHtml(data, 'updated ' + ts);
    const log = document.getElementById('vt-insight-log');
    const entry = document.createElement('div');
    entry.innerHTML = insightCardHtml(data, ts);
    log.prepend(entry.firstElementChild);
}

function placeSidebar(pageId) {
    const slots = { 'vt-page-greeks': 'vt-side-slot-greeks', 'vt-page-pricing': 'vt-side-slot-pricing', 'vt-page-cf': 'vt-side-slot-cf' };
    const slot = document.getElementById(slots[pageId]); // the main page has no sidebar
    const sb = document.getElementById('vt-sidebar');
    if (slot && sb && sb.parentElement !== slot) slot.appendChild(sb);
    // the Strike Lens is one shared element too: same state on both pages, never diverges
    const lslots = { 'vt-page-cf': 'vt-lens-slot-cf', 'vt-page-pricing': 'vt-lens-slot-pricing' };   // the three sections live on Chart Factors; the Greeks page no longer hosts any lens
    const ls = document.getElementById(lslots[pageId]), lens = document.getElementById('vt-lens');
    if (ls && lens && lens.parentElement !== ls) ls.appendChild(lens);
    if (window.VT_LENS && lslots[pageId]) VT_LENS.setView(pageId === 'vt-page-cf' ? 'greeks' : 'pricing');   // Pricing page = pricing levels, greeks page = Greek levels
}

function setupPageNav() {
    document.querySelectorAll('.vt-nav-link').forEach(btn => {
        btn.addEventListener('click', () => {
            document.querySelectorAll('.vt-nav-link').forEach(b => b.classList.remove('active'));
            document.querySelectorAll('.vt-page').forEach(p => p.classList.remove('active'));
            btn.classList.add('active');
            document.getElementById(btn.dataset.page).classList.add('active');
            placeSidebar(btn.dataset.page);
            renderSidebar(); // one shared element, repainted on every switch so it can never show stale state
            // Panels built while their page was display:none (e.g. greeksChannel,
            // built eagerly at startup so data keeps flowing to it in the
            // background) sized themselves against a collapsed container.
            // Re-fire resize once the page is actually visible so every
            // SurfacePanel's existing resize listener re-measures correctly.
            requestAnimationFrame(() => window.dispatchEvent(new Event('resize')));
        });
    });
}

// ---- underlying selector: SPX / SPY / NDX / QQQ ----
let activeTicker = 'SPX';
function resetForTicker() {
    for (const k of Object.keys(flow.net)) delete flow.net[k];
    for (const k of Object.keys(flow.gex)) delete flow.gex[k];
    currentSpot = null;
    if (pcaPanel) pcaPanel.resetForTicker();
    Object.values(panels).forEach(p => { p.latestByDte = {}; p.panel.showAwaiting(p.selectedDte); });
    renderSidebar();
}
const LEGACY_FEED = ['SPX', 'SPY', 'NDX', 'QQQ'];   // tickers the C++ scheduler currently has a source for
function setupTickers() {
    const host = document.getElementById('vt-tickers'); const SC = window.VT_SCOPE;
    SC.onChange(scope => {
        const changed = scope.ticker !== activeTicker;
        activeTicker = scope.ticker;
        if (changed) resetForTicker();
        // Single View on a legacy ticker takes exactly the old path. Other scopes are pushed as a Scope object.
        if (typeof window.vtSetUnderlying === 'function' && changed && LEGACY_FEED.includes(activeTicker)) window.vtSetUnderlying(activeTicker);
        if (typeof window.vtSetScope === 'function') window.vtSetScope(JSON.stringify(scope));
        window.__scope = scope;
    });
    SC.renderTickers(host, t => SC.select(t));
    document.querySelector('#vt-scope-tabs [data-m="single"]').addEventListener('click', () => SC.single());
    document.querySelector('#vt-scope-tabs [data-m="multi"]').addEventListener('click', () => SC.multi());
}

window.addEventListener('DOMContentLoaded', () => {
    buildSection1();
    setupTickers();
    setupPageNav();
});

window.__activeTicker = () => activeTicker;
window.__panels = panels; // read-only handle for diagnostics/tests
window.onModelUpdate = onModelUpdate;
window.onInsightUpdate = onInsightUpdate;
window.onRegimeStatic = onRegimeStatic;
window.onRegimeLive = onRegimeLive;
window.onRegimeViz = (j) => { if (pcaPanel) pcaPanel.setViz(typeof j === 'string' ? JSON.parse(j) : j); };
window.onRegimeVizLive = (j) => { if (pcaPanel) pcaPanel.updateViz(typeof j === 'string' ? JSON.parse(j) : j); };
