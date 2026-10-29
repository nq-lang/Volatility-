// model_card.js - `multibook_v1` model card (inputs, fit date, n, validation metrics, comparison vs single_v1, known limits).
// Payload comes from the C++ core on the `multibook_model` channel (window.onMultibookModel). The button only exists in Multi-Book / Family scope;
// Single-Book stays on single_v1 and has no card. Nothing here computes a metric: every number shown is read from the payload.
(function () {
    const M = { card: null };
    const pct = (v, d) => v == null ? 'n/a' : (v * 100).toFixed(d == null ? 0 : d) + '%';
    const f2 = v => v == null ? 'n/a' : (+v).toFixed(2);
    function html(c) {
        const wf = c.walk_forward || {}, cmp = c.vs_single_v1, used = c.features.filter(f => f.used).length;
        const chips = c.features.map(f => '<span class="chip' + (f.group === 'cross_book' ? ' x' : '') + (f.used ? '' : ' off') + '" title="' + f.group + (f.used ? '' : ' - pruned (constant or collinear)') + '">' + f.name + '</span>').join('');
        return '<div class="box"><button class="close" aria-label="close">close</button><h3>MODEL CARD &middot; ' + c.variant + '</h3><div style="color:#7d8794">family ' + c.family + ' &middot; ' + c.ticker_view + ' view &middot; fit ' + c.fit_date + ' &middot; its own standardization, PCA and GMM (nothing shared with single_v1)</div>' +
            '<h5>Fit</h5><div class="kv"><div><span>training days (n)</span><b>' + c.n_train + '</b></div><div><span>features (used / total)</span><b>' + used + ' / ' + c.n_features + '</b></div><div><span>days per feature</span><b>' + f2(c.samples_per_feature) + '</b></div>' +
            '<div><span>PCA components</span><b>' + c.pca_components + '</b></div><div><span>GMM components</span><b>' + c.gmm_components + '</b></div><div><span>cumulative explained variance</span><b class="' + (c.low_variance_warning ? 'warn' : '') + '">' + pct(c.cum_explained_variance) + '</b></div></div>' +
            (c.low_variance_warning ? '<div class="warn" style="margin-top:6px">Low explained variance: the low-dimensional view captures under 60% of the structure, so treat plotted distances as a partial picture.</div>' : '') +
            '<h5>Walk-forward validation (no look-ahead)</h5><div class="kv"><div><span>scored out-of-sample days</span><b>' + wf.scored_days + '</b></div><div><span>refits</span><b>' + wf.refits + '</b></div><div><span>avg regime dwell (days)</span><b>' + f2(wf.avg_dwell_days) + '</b></div><div><span>flip frequency / day</span><b>' + pct(wf.flip_frequency, 1) + '</b></div>' +
            '<div><span>calibration error (ECE)</span><b>' + f2(wf.calibration_ece) + '</b></div><div><span>min loading |cos| across refits</span><b>' + f2(wf.min_loading_cos) + '</b></div><div><span>label agreement across refits</span><b>' + pct(wf.mean_label_agreement_across_refits) + '</b></div></div>' +
            '<h5>Versus single_v1 on the same days</h5>' + (cmp ? '<div class="kv"><div><span>same-label rate</span><b>' + pct(cmp.label_agreement) + '</b></div><div><span>outcome separation, single_v1</span><b>' + f2(cmp.eta2_single) + '</b></div><div><span>outcome separation, multibook_v1</span><b>' + f2(cmp.eta2_multi) + '</b></div><div><span>difference / permutation p</span><b>' + (cmp.delta >= 0 ? '+' : '') + f2(cmp.delta) + ' / ' + f2(cmp.p_value) + '</b></div></div><div class="' + (cmp.adds_information ? 'ok' : 'warn') + '" style="margin-top:6px">' + cmp.verdict + '</div>' : '<div class="warn">comparison not available</div>') +
            '<h5>Inputs</h5><div>' + chips + '</div>' + ((c.unavailable_features || []).length ? '<h5>Not available (not imputed)</h5><ul>' + c.unavailable_features.map(u => '<li>' + u + '</li>').join('') + '</ul>' : '') +
            '<h5>Known limits</h5><ul>' + c.known_limits.map(u => '<li>' + u + '</li>').join('') + '</ul>' + (c.mock ? '<div class="warn" style="margin-top:8px">MOCK DATA - illustrative only</div>' : '') + '</div>';
    }
    function open() { if (!M.card) return; let el = document.getElementById('vt-mcard'); if (!el) { el = document.createElement('div'); el.id = 'vt-mcard'; document.body.appendChild(el); el.addEventListener('click', e => { if (e.target === el || e.target.closest('.close')) el.hidden = true; }); }
        el.innerHTML = html(M.card); el.hidden = false; }
    function sync() { const b = document.getElementById('vt-mcard-btn'); const sc = window.VT_SCOPE ? VT_SCOPE.scope() : { mode: 'single' }; const show = sc.mode !== 'single' && !!M.card; if (b) b.hidden = !show; }
    window.onMultibookModel = p => { M.card = typeof p === 'string' ? JSON.parse(p) : p; sync(); };
    window.addEventListener('DOMContentLoaded', () => {
        const host = document.getElementById('vt-scope-badge'); if (host && !document.getElementById('vt-mcard-btn')) { const b = document.createElement('button'); b.id = 'vt-mcard-btn'; b.className = 'vt-mcard-btn'; b.textContent = 'MODEL CARD'; b.hidden = true; b.addEventListener('click', open); host.parentNode.insertBefore(b, host.nextSibling); }
        if (window.VT_SCOPE) VT_SCOPE.onChange(sync); sync(); });
    window.VT_MCARD = M; M.open = open;
})();
