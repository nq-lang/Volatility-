// scope_ui.js - registry-driven ticker rows, scope popup, book picker, header badge + Single-Book / Multi-Book tabs.
(function () {
    const R = window.VT_REGISTRY; if (!R) return;
    const disp = t => R.display[t] || t;
    const canon = t => { t = String(t).toUpperCase(); return R.aliases[t] || t; };
    const isAnchor = t => R.futures.includes(canon(t));
    const isVol = t => (R.volatility || []).includes(canon(t));
    const familiesOf = t => Object.entries(R.families).filter(([, f]) => f.enabled && f.books.includes(canon(t))).map(([k]) => k);
    const LS = 'vt_scope_choice_';
    const remembered = t => { try { return JSON.parse(localStorage.getItem(LS + t) || 'null'); } catch (e) { return null; } };
    const remember = (t, v) => { try { localStorage.setItem(LS + t, JSON.stringify(v)); } catch (e) {} };
    // anchorFamily: set when a single book was picked from a futures family, so Multi-Book returns to that family.
    const S = { mode: 'single', ticker: 'SPX', family: null, anchorFamily: null, books: [], listeners: [] };
    S.scope = () => ({ mode: S.mode, ticker: S.ticker, family: S.family || S.anchorFamily, books: S.books.slice(), model_variant: S.mode === 'single' ? 'single_v1' : 'multibook_v1' });
    function badgeText() {
        if (isVol(S.ticker)) return 'VOLATILITY · ' + disp(S.ticker);
        if (S.mode === 'single') return 'SINGLE-BOOK' + (S.anchorFamily ? ' · ' + disp(S.ticker) + ' (' + S.anchorFamily + ')' : '');
        if (S.mode === 'family') return 'FAMILY · ' + S.family + ' · ' + S.books.length + ' books';
        return 'MULTI-BOOK · ' + S.family + ' · ' + S.books.length + ' books';
    }
    function hasFamily() { return !isVol(S.ticker) && !!(S.anchorFamily || S.family || (!isAnchor(S.ticker) && familiesOf(S.ticker).length)); }
    function renderBadge() {
        const b = document.getElementById('vt-scope-badge'); if (!b) return;
        b.textContent = badgeText(); b.dataset.mode = S.mode;
        const tabs = document.getElementById('vt-scope-tabs');
        if (tabs) { tabs.hidden = !hasFamily();
            tabs.querySelector('[data-m="single"]').classList.toggle('on', S.mode === 'single');
            tabs.querySelector('[data-m="multi"]').classList.toggle('on', S.mode !== 'single'); }
        const hl = S.anchorFamily || S.ticker;
        document.querySelectorAll('.vt-ticker').forEach(x => x.classList.toggle('active', x.dataset.ticker === hl));
    }
    function apply(ticker, mode, family, anchorFamily) {
        ticker = canon(ticker); S.ticker = ticker; S.mode = mode; S.family = mode === 'single' ? null : family;
        S.anchorFamily = anchorFamily || null;
        S.books = mode === 'single' ? [ticker] : R.families[family].books.slice();
        renderBadge(); S.listeners.forEach(fn => fn(S.scope()));
    }
    S.apply = apply; S.onChange = fn => S.listeners.push(fn);
    S.select = function (ticker, force) {
        ticker = canon(ticker);
        if (isVol(ticker)) return apply(ticker, 'single', null);               // volatility index: own section, no books/family
        if (isAnchor(ticker)) return apply(ticker, 'family', ticker);          // futures anchor: Family mode, no popup
        const fams = familiesOf(ticker);
        if (!fams.length) return apply(ticker, 'single', null);
        const r = !force && remembered(ticker);
        if (r) return apply(ticker, r.mode, r.family);
        openScopePopup(ticker, fams);
    };
    S.multi = function () {
        if (S.mode !== 'single') return;
        if (S.anchorFamily) return apply(S.anchorFamily, 'family', S.anchorFamily);
        const f = (remembered(S.ticker) || {}).family || familiesOf(S.ticker)[0]; if (f) apply(S.ticker, 'multi_book', f);
    };
    S.single = function () {
        if (S.mode === 'single') return;
        if (S.mode === 'family') return openBookPicker(S.family);               // futures: choose which book
        apply(S.ticker, 'single', null);
    };
    // ---- popups ----
    let pop = null, prevFocus = null, keyFn = null;
    function closePopup() { if (!pop) return; pop.remove(); pop = null; document.removeEventListener('keydown', keyFn, true); keyFn = null; if (prevFocus && prevFocus.focus) prevFocus.focus(); }
    function mount(html, ticker, keys) {
        closePopup(); prevFocus = document.activeElement;
        pop = document.createElement('div'); pop.className = 'vt-pop-back'; pop.dataset.ticker = ticker || ''; pop.innerHTML = html; document.body.appendChild(pop);
        keyFn = e => { if (!pop) return; if (e.key === 'Escape') { e.preventDefault(); closePopup(); } else if (keys[e.key]) { e.preventDefault(); keys[e.key](); } };
        document.addEventListener('keydown', keyFn, true);
        pop.addEventListener('click', e => { if (e.target === pop) closePopup(); });
    }
    function openBookPicker(fam) {
        const F = R.families[fam], un = new Set(F.unresolved || []);
        const rows = F.books.map((b, i) => ({ id: b, pr: i + 1, bad: un.has(b), note: (F.notes || {})[b] }));
        mount('<div class="vt-pop" role="dialog" aria-modal="true" aria-label="' + fam + ': choose a book">' +
            '<div class="vt-pop-title">' + fam + ': choose a book</div><div class="vt-pop-sub">Ranked by priority. Single-Book analyzes only the one you pick.</div>' +
            '<div class="vt-pick" role="listbox">' + rows.map(r => '<button class="vt-pick-row" role="option" data-id="' + r.id + '"' + (r.bad ? ' disabled' : '') + '><span class="vt-pick-n">#' + r.pr + '</span><span class="vt-pick-name">' + disp(r.id) +
                '</span><small>' + (r.bad ? 'unresolved symbol' : (r.note ? 'unverified' : '')) + '</small></button>').join('') + '</div>' +
            '<div class="vt-pop-hint">1-' + Math.min(9, rows.length) + ' pick · Esc cancel</div></div>', fam,
            Object.fromEntries(rows.slice(0, 9).filter(r => !r.bad).map(r => [String(r.pr), () => pickBook(r.id, fam)])));
        pop.querySelectorAll('.vt-pick-row:not([disabled])').forEach(b => b.addEventListener('click', () => pickBook(b.dataset.id, fam)));
        const first = pop.querySelector('.vt-pick-row:not([disabled])'); if (first) first.focus();
    }
    function pickBook(id, fam) { closePopup(); apply(id, 'single', null, fam); }
    function openScopePopup(ticker, fams) {
        const def = fams[0];
        const fsel = fams.length > 1 ? '<label class="vt-pop-fam">Family <select aria-label="Family">' + fams.map(f => '<option value="' + f + '">' + f + '</option>').join('') + '</select></label>' : '';
        const choose = mode => { const fam = pop.querySelector('select') ? pop.querySelector('select').value : def;
            if (pop.querySelector('#vt-pop-remember').checked) remember(ticker, { mode, family: fam });
            closePopup(); apply(ticker, mode, mode === 'single' ? null : fam); };
        mount('<div class="vt-pop" role="dialog" aria-modal="true" aria-label="' + disp(ticker) + ': choose analysis scope">' +
            '<div class="vt-pop-title">' + disp(ticker) + ': choose analysis scope</div>' +
            '<button class="vt-pop-opt" data-m="single"><span class="vt-pop-ic">1</span><span><b>Single-Book</b><small>Every panel and model uses only ' + disp(ticker) + '’s own options book.</small></span></button>' +
            '<button class="vt-pop-opt" data-m="multi_book"><span class="vt-pop-ic">2</span><span><b>Multi-Book</b><small>' + disp(ticker) + ' read through its whole family. Books: <i id="vt-pop-books"></i></small></span></button>' +
            fsel + '<label class="vt-pop-rem"><input type="checkbox" id="vt-pop-remember"> Remember my choice for ' + disp(ticker) + '</label>' +
            '<div class="vt-pop-hint">1 Single-Book · 2 Multi-Book · Esc cancel</div></div>', ticker,
            { '1': () => choose('single'), '2': () => choose('multi_book') });
        const upd = () => { const f = pop.querySelector('select') ? pop.querySelector('select').value : def;
            pop.querySelector('#vt-pop-books').textContent = R.families[f].books.map(disp).join(', ') + ' (' + f + ')'; };
        upd(); if (pop.querySelector('select')) pop.querySelector('select').addEventListener('change', upd);
        pop.querySelectorAll('.vt-pop-opt').forEach(b => b.addEventListener('click', () => choose(b.dataset.m)));
        pop.querySelector('.vt-pop-opt').focus();
    }
    S.popupOpen = () => !!pop;
    S.renderTickers = function (host, onPick) {
        const mk = (t, cls) => '<button class="vt-ticker' + cls + '" data-ticker="' + t + '">' + disp(t) + '</button>';
        host.innerHTML = '<div class="vt-trow" data-row="indx"><span class="vt-rlab">INDX</span>' + R.standard.map(t => mk(t, '')).join('') + '</div>' +
            '<div class="vt-trow" data-row="fut"><span class="vt-rlab">=F</span>' + R.futures.map(t => mk(t, ' vt-fut')).join('') + '</div>' +
            ((R.volatility || []).length ? '<div class="vt-trow" data-row="vol"><span class="vt-rlab">Volatility</span>' + R.volatility.map(t => mk(t, ' vt-volt')).join('') + '</div>' : '');
        host.querySelectorAll('.vt-ticker').forEach(b => b.addEventListener('click', () => onPick(b.dataset.ticker)));
        renderBadge();
    };
    S.renderBadge = renderBadge; S.isAnchor = isAnchor; S.isVol = isVol; S.familiesOf = familiesOf;
    window.VT_SCOPE = S;
})();
