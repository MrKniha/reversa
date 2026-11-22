/* REVERSA GUI – vanilla JS single page app talking to the local JSON API.
 * Kept dependency-free on purpose (no CDN, works offline, nothing to audit). */
'use strict';

const TOKEN = new URLSearchParams(location.search).get('token') || '';
const $ = (sel) => document.querySelector(sel);
const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const store = {
  get(k, d) { try { const v = localStorage.getItem('reversa.' + k); return v === null ? d : JSON.parse(v); } catch (e) { return d; } },
  set(k, v) { try { localStorage.setItem('reversa.' + k, JSON.stringify(v)); } catch (e) { /* private mode */ } },
};

const DEFAULT_KEYS = {
  rename: 'N', comment: ';', bookmark: 'B', goto: 'G', undo: 'Ctrl+Z', redo: 'Ctrl+Y', save: 'Ctrl+S', search: 'Ctrl+F',
  listing: '1', pseudocode: '2', cfg: '3', callgraph: '4', back: 'Alt+ArrowLeft', help: 'F1',
};
const TABS = ['listing', 'pseudocode', 'cfg', 'callgraph', 'strings', 'imports', 'exports', 'sections', 'bookmarks', 'search', 'views'];
const HELP_DOC = { functions: 'quickstart.md', listing: 'quickstart.md', pseudocode: 'tutorials.md', cfg: 'tutorials.md',
  callgraph: 'quickstart.md', details: 'glossary.md', strings: 'tutorials.md', imports: 'glossary.md', sections: 'glossary.md' };

const S = {
  i18n: {}, fallback: {}, state: null, funcs: [], tab: 'listing', cur: null, func: null, history: [],
  revision: -1, status: null, keys: Object.assign({}, DEFAULT_KEYS, store.get('keys', {})), selVar: null,
  searchHits: [], sort: {}, listing: null, lastError: null,
};

// ------------------------------------------------------------------ API
async function api(path, params = {}) {
  const qs = new URLSearchParams(Object.entries(params).filter(([, v]) => v !== undefined && v !== null)).toString();
  const r = await fetch('/api/' + path + (qs ? '?' + qs : ''), { headers: { 'X-Reversa-Token': TOKEN } });
  const j = await r.json().catch(() => ({ error: { message: 'Server vrátil neplatnú odpoveď.' } }));
  if (!r.ok || (j && j.error)) throw j.error || { message: 'HTTP ' + r.status };
  return j;
}
async function post(action, body = {}) {
  const r = await fetch('/api/' + action, { method: 'POST', headers: { 'X-Reversa-Token': TOKEN, 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
  const j = await r.json().catch(() => ({ error: { message: 'Server vrátil neplatnú odpoveď.' } }));
  if (!r.ok || j.error) throw j.error || { message: 'HTTP ' + r.status };
  return j;
}
async function guarded(fn) { try { return await fn(); } catch (e) { showError(e); return null; } }

// ------------------------------------------------------------------ i18n
function t(key, vars) {
  let s = S.i18n[key] ?? S.fallback[key] ?? key;
  if (vars) for (const [k, v] of Object.entries(vars)) s = s.replaceAll('{' + k + '}', v);
  return s;
}
const ABBR = ['CFG', 'x-refs', 'xref', 'RVA', 'PLT', 'IAT'];
function abbrify(text) {
  let h = esc(text);
  for (const a of ABBR) {
    const key = 'abbr.' + (a === 'x-refs' ? 'xref' : a);
    h = h.replace(new RegExp('\\b' + a.replace('-', '\\-') + '\\b', 'g'), `<abbr title="${esc(t(key))}">${a}</abbr>`);
  }
  return h;
}
async function loadI18n() {
  const lang = (new URLSearchParams(location.search).get('lang') || store.get('lang', null) || 'sk').slice(0, 2);
  try { S.fallback = await (await fetch('/i18n/sk.json')).json(); } catch (e) { S.fallback = {}; }
  S.i18n = S.fallback;
  if (lang !== 'sk') { try { S.i18n = Object.assign({}, S.fallback, await (await fetch('/i18n/' + lang + '.json')).json()); } catch (e) { /* keep sk */ } }
  document.querySelectorAll('[data-i18n]').forEach((el) => { el.textContent = t(el.dataset.i18n); });
  document.querySelectorAll('[data-i18n-ph]').forEach((el) => { el.placeholder = t(el.dataset.i18nPh); });
  document.querySelectorAll('[data-i18n-title]').forEach((el) => { el.title = t(el.dataset.i18nTitle); });
}

// ------------------------------------------------------------------ feedback
function showError(e) {
  const msg = (e && e.message) || String(e);
  S.lastError = e;
  $('#error-text').textContent = msg;
  $('#error-copy').hidden = !(e && e.detail);
  $('#errorbar').hidden = false;
}
function toast(msg) {
  const el = $('#toast'); el.textContent = msg; el.hidden = false;
  clearTimeout(toast._t); toast._t = setTimeout(() => { el.hidden = true; }, 2600);
}
function dialog({ text, value = '', input = true, body = null }) {
  return new Promise((resolve) => {
    const d = $('#dlg');
    $('#dlg-text').textContent = text || '';
    $('#dlg-body').innerHTML = '';
    if (body) $('#dlg-body').appendChild(body);
    const inp = $('#dlg-input'); inp.hidden = !input; inp.value = value;
    d.returnValue = '';
    d.onclose = () => resolve(d.returnValue === 'ok' ? (input ? inp.value : true) : null);
    d.showModal();
    if (input) { inp.focus(); inp.select(); }
  });
}

// ------------------------------------------------------------------ state & polling
async function poll() {
  try {
    const st = await api('state');
    const prev = S.status;
    S.state = st; S.status = st.status;
    renderStatus(st);
    if (st.status === 'ready' && (prev !== 'ready' || st.revision !== S.revision)) {
      const first = prev !== 'ready';
      S.revision = st.revision;
      await onData(first);
    }
    if (st.status === 'error' && prev !== 'error' && st.error) showError(st.error);
    if (st.status === 'empty' && prev !== 'empty') { S.funcs = []; renderFuncs(); renderView(); renderDetails(); }
  } catch (e) {
    renderStatus({ status: 'error', message: e.message });
  }
  setTimeout(poll, S.status === 'loading' || S.status === 'analyzing' ? 300 : 1500);
}

function renderStatus(st) {
  const busy = st.status === 'loading' || st.status === 'analyzing';
  $('#status-text').textContent = t('status.' + st.status) + (st.message && busy ? ' – ' + st.message : '');
  $('#progress').hidden = !busy; $('#btn-cancel').hidden = !busy;
  $('#progress-bar').style.width = Math.round((st.progress || 0) * 100) + '%';
  const ready = st.status === 'ready';
  $('#btn-save').disabled = !ready;
  $('#btn-undo').disabled = !(ready && st.can_undo);
  $('#btn-redo').disabled = !(ready && st.can_redo);
  $('#btn-advanced').title = ready ? '' : t('advanced.locked');
  const f = st.file;
  $('#status-file').textContent = f ? `${f.name} · ${f.format} ${f.arch} · ${f.compiler}${st.dirty ? ' · ' + t('msg.unsaved') : ''}` : '';
  $('#status-file').title = f ? `${f.path}\nSHA-256: ${f.sha256}` : '';
  document.title = f ? `${f.name}${st.dirty ? ' •' : ''} – REVERSA` : 'REVERSA';
  const errs = (st.diagnostics || []).filter((d) => d.severity === 'error');
  if (errs.length && !S._diagShown) { S._diagShown = true; showError({ message: errs[0].message, detail: errs.map((d) => d.message + '\n' + d.detail).join('\n\n') }); }
  if (!f) S._diagShown = false;
  $('#tabs').querySelector('[data-tab="views"]').hidden = !(st.views && st.views.length);
}

async function onData(first) {
  S.funcs = await api('functions');
  if (first) {
    S.history = [];
    const main = S.state.file && (S.state.file.main || S.state.file.entry);
    const start = main || (S.funcs.find((f) => f.kind === 'normal') || {}).addr;
    S.cur = start || null; S.func = start || null;
    S.listing = null;
  }
  renderFuncs();
  await renderView();
  await renderDetails();
}

// ------------------------------------------------------------------ functions sidebar
function renderFuncs() {
  const list = $('#func-list');
  const q = $('#func-filter').value.trim().toLowerCase();
  if (!S.funcs.length) {
    list.innerHTML = `<div class="empty" style="margin:2em 1em">${esc(t('empty.functions'))}<div class="actions"><button onclick="document.getElementById('btn-demo').click()">${esc(t('btn.demo'))}</button></div></div>`;
    $('#func-count').textContent = '';
    return;
  }
  const rows = S.funcs.filter((f) => !q || f.name.toLowerCase().includes(q) || f.addr.includes(q));
  rows.sort((a, b) => (b.main - a.main) || ((a.kind !== 'normal') - (b.kind !== 'normal')) || (parseInt(a.addr, 16) - parseInt(b.addr, 16)));
  $('#func-count').textContent = `(${rows.length})`;
  list.innerHTML = rows.slice(0, 5000).map((f) => `<div class="frow ${f.kind !== 'normal' ? 'imp' : ''} ${f.user ? 'user' : ''} ${f.addr === S.func ? 'sel' : ''}" data-addr="${f.addr}" title="${esc(f.name)} @ ${f.addr} · ${f.size} B · volaná z ${f.callers} miest${f.library ? ' · ' + esc(f.library) : ''}"><span class="nm">${f.main ? '★ ' : ''}${esc(f.name)}</span><span class="meta">${f.kind === 'normal' ? f.size + ' B · ' + f.callers + '×' : 'import'}</span></div>`).join('');
}

// ------------------------------------------------------------------ navigation
function funcOf(addr) {
  const a = parseInt(addr, 16);
  let best = null;
  for (const f of S.funcs) {
    const fa = parseInt(f.addr, 16);
    if (fa === a) return f.addr;
    if (f.kind === 'normal' && fa <= a && a < fa + f.size && (!best || fa > parseInt(best, 16))) best = f.addr;
  }
  return best;
}
async function goTo(addr, opts = {}) {
  if (!addr) return;
  if (S.cur && opts.history !== false) { S.history.push({ cur: S.cur, func: S.func, tab: S.tab }); if (S.history.length > 200) S.history.shift(); }
  S.cur = addr;
  const f = funcOf(addr);
  if (f) S.func = f;
  if (opts.tab) S.tab = opts.tab;
  S.listing = null;
  renderTabs(); renderFuncs();
  await renderView();
  await renderDetails();
}
async function goBack() {
  const h = S.history.pop();
  if (!h) return;
  S.cur = h.cur; S.func = h.func; S.tab = h.tab; S.listing = null;
  renderTabs(); renderFuncs(); await renderView(); await renderDetails();
}
async function select(addr) { S.cur = addr; await renderDetails(); highlightCur(); }
function highlightCur() {
  document.querySelectorAll('.ln.cur').forEach((e) => e.classList.remove('cur'));
  if (S.cur) document.querySelectorAll(`.ln[data-addr="${S.cur}"]`).forEach((e) => e.classList.add('cur'));
}

// ------------------------------------------------------------------ tabs
function renderTabs() {
  const nav = $('#tabs');
  if (!nav.children.length) {
    nav.innerHTML = TABS.map((k) => `<button role="tab" data-tab="${k}" ${k === 'search' ? 'hidden' : ''}>${esc(t('tab.' + k))}</button>`).join('');
    nav.addEventListener('click', (e) => { const b = e.target.closest('button[data-tab]'); if (b) { S.tab = b.dataset.tab; renderTabs(); renderView(); } });
  }
  nav.querySelectorAll('button').forEach((b) => b.setAttribute('aria-selected', String(b.dataset.tab === S.tab)));
  if (S.searchHits.length) nav.querySelector('[data-tab="search"]').hidden = false;
  showBanner(S.tab);
}
function showBanner(tab) {
  const key = 'banner.' + tab;
  const seen = store.get('banners', {});
  if (!S.i18n[key] && !S.fallback[key]) { $('#banner').hidden = true; return; }
  if (seen[tab]) { $('#banner').hidden = true; return; }
  $('#banner-text').innerHTML = abbrify(t(key));
  $('#banner').hidden = false;
  $('#banner-close').onclick = () => { seen[tab] = true; store.set('banners', seen); $('#banner').hidden = true; };
}

async function renderView() {
  const v = $('#view');
  if (!S.state || S.state.status !== 'ready') {
    v.innerHTML = `<div class="empty"><h2>REVERSA</h2><p>${esc(t('empty.main'))}</p><div class="actions"><button class="primary" onclick="document.getElementById('btn-open').click()">${esc(t('btn.open'))}</button><button onclick="document.getElementById('btn-demo').click()">${esc(t('btn.demo'))}</button></div></div>`;
    return;
  }
  const r = { listing: viewListing, pseudocode: viewPseudo, cfg: viewCfg, callgraph: viewCallgraph, strings: viewStrings,
    imports: viewImports, exports: viewExports, sections: viewSections, bookmarks: viewBookmarks, search: viewSearch, views: viewViews }[S.tab];
  await guarded(() => r(v));
}

// ------------------------------------------------------------------ listing
const REG_RE = /\b([re]?[abcd]x|[abcd][lh]|[re]?[sd]il?|[re]?[sb]pl?|r\d+[dwb]?|[xw]\d+|sp|wzr|xzr|fp|lr|rip|eip|[cdefgs]s)\b/g;
function fmtOps(line) {
  let h = esc(line.ops);
  h = h.replace(REG_RE, '<span class="reg">$1</span>').replace(/\b(0x[0-9a-f]+|\d+)\b(?![^<]*>)/g, '<span class="num">$1</span>');
  if (line.target) h = `<span class="lk" data-go="${line.target.addr}">${esc(line.target.name)}</span>`;
  return h;
}
function lineHtml(l) {
  const a = `<span class="a" data-sel="${l.addr}">${l.addr}</span>`;
  switch (l.type) {
    case 'section': return `<div class="ln secl" data-addr="${l.addr}">; ── sekcia ${esc(l.name)} (${l.perms}) ──</div>`;
    case 'function': {
      const callers = l.callers.map((c) => `<span class="lk" data-go="${c.addr}">${esc(c.name)}</span>`).join(', ');
      return `<div class="ln fn" data-addr="${l.addr}">${a}<span><span class="lk" data-sel="${l.addr}">${esc(l.name)}</span>${l.kind !== 'normal' ? ' (import)' : ''}:</span><span class="c">${l.ncallers ? '; volaná z: ' + callers + (l.ncallers > l.callers.length ? ' …' : '') : ''}</span></div>`;
    }
    case 'label': return `<div class="ln lbl" data-addr="${l.addr}">${a}<span>${esc(l.name)}:</span></div>`;
    case 'sep': return '<div class="ln sepl"></div>';
    case 'data': {
      let txt = l.kind === 'string' ? `<span class="sk-str">${l.enc === 'ascii' ? 'db' : 'du'} "${esc(l.text)}"</span>` : esc(l.text);
      if (l.target) txt += ` <span class="c">; → <span class="lk" data-go="${l.target.addr}">${esc(l.target.name)}</span></span>`;
      const xr = (l.xrefs || []).map((x) => `<span class="lk" data-go="${x.addr}">${esc(x.func || x.addr)}</span>`).join(', ');
      return `<div class="ln" data-addr="${l.addr}">${a}${l.name ? `<span class="sk-glob">${esc(l.name)}</span>` : ''}<span>${txt}</span>${xr ? `<span class="c">; xref: ${xr}</span>` : ''}</div>`;
    }
    default: {
      const cls = l.flow === 'call' || l.flow === 'icall' ? 'call' : l.flow === 'ret' ? 'ret' : (l.flow.includes('jmp') ? 'jmp' : '');
      let cm = '';
      for (const r of l.refs || []) {
        if (r.string !== undefined) cm += ` ; "${esc(r.string)}"`;
        else if (!l.target) cm += ` ; <span class="lk" data-go="${r.addr}">${esc(r.name)}</span>`;
      }
      if (l.comment) cm += ` ; ${esc(l.comment)}`;
      if (l.bookmark) cm += ` ; 🔖 ${esc(l.bookmark)}`;
      return `<div class="ln" data-addr="${l.addr}">${a}<span class="b">${esc(l.bytes)}</span><span class="m ${cls}">${esc(l.mn)}</span><span class="o">${fmtOps(l)}</span><span class="c">${cm}</span></div>`;
    }
  }
}
async function viewListing(v) {
  const start = S.cur || S.func;
  const res = await api('listing', { addr: start, count: 250 });
  S.listing = { first: res.first, next: res.next, loading: false };
  v.innerHTML = `<div class="code" id="listing">${res.lines.map(lineHtml).join('')}</div>`;
  highlightCur();
  const el = v.querySelector(`.ln[data-addr="${start}"]`);
  if (S.cur && el) el.scrollIntoView({ block: 'center' });
  if (res.first && res.first === start) await loadMore('up', true);
}
async function loadMore(dir, keepPos) {
  const L = S.listing; const v = $('#view'); const box = $('#listing');
  if (!L || L.loading || !box) return;
  L.loading = true;
  try {
    if (dir === 'down' && L.next) {
      const r = await api('listing', { addr: L.next, count: 200 });
      box.insertAdjacentHTML('beforeend', r.lines.map(lineHtml).join(''));
      L.next = r.next;
    } else if (dir === 'up' && L.first) {
      const r = await api('listing', { addr: L.first, count: 120, dir: 'up' });
      if (r.lines.length) {
        const h0 = box.scrollHeight;
        box.insertAdjacentHTML('afterbegin', r.lines.map(lineHtml).join(''));
        v.scrollTop += box.scrollHeight - h0;
        L.first = r.lines[0].addr;
      } else { L.first = null; }
    }
    highlightCur();
  } catch (e) { /* end of section */ L[dir === 'down' ? 'next' : 'first'] = null; }
  L.loading = false;
}

// ------------------------------------------------------------------ pseudocode
async function viewPseudo(v) {
  if (!S.func) { v.innerHTML = `<div class="empty">${esc(t('msg.select_first'))}</div>`; return; }
  const res = await api('pseudocode', { addr: S.func });
  const html = res.lines.map((ln) => {
    const segs = ln.segs.map(([text, kind, ref]) => {
      const k = 'sk-' + kind;
      if ((kind === 'func' || kind === 'glob' || kind === 'str' || kind === 'label') && ref) return `<span class="${k}" data-go="${ref}">${esc(text)}</span>`;
      if (kind === 'var' && ref) return `<span class="${k}" data-var="${esc(ref)}">${esc(text)}</span>`;
      return `<span class="${k}">${esc(text)}</span>`;
    }).join('');
    return `<div class="ln" ${ln.addr ? `data-addr="${ln.addr}"` : ''}><span>${'    '.repeat(ln.indent)}${segs}</span></div>`;
  }).join('');
  v.innerHTML = `<div class="code" id="pseudo">${html}</div>`;
  highlightCur();
  if (S.selVar) markVar(S.selVar);
}
function markVar(key) {
  S.selVar = key;
  document.querySelectorAll('#pseudo .sk-var').forEach((e) => e.classList.toggle('hl', e.dataset.var === key));
}

// ------------------------------------------------------------------ graphs
function layoutGraph(nodes, edges, entryId) {
  const byId = new Map(nodes.map((n) => [n.id, n]));
  const out = new Map(nodes.map((n) => [n.id, []]));
  const inc = new Map(nodes.map((n) => [n.id, []]));
  for (const e of edges) { if (byId.has(e.from) && byId.has(e.to) && e.from !== e.to) { out.get(e.from).push(e); inc.get(e.to).push(e); } }
  // DFS to find back edges (cycles) and an initial order
  const state = new Map(); const order = []; const back = new Set();
  const roots = [entryId, ...nodes.filter((n) => !inc.get(n.id).length).map((n) => n.id), ...nodes.map((n) => n.id)];
  for (const r of roots) {
    if (!byId.has(r) || state.has(r)) continue;
    const stack = [[r, 0]]; state.set(r, 1);
    while (stack.length) {
      const top = stack[stack.length - 1]; const [id, i] = top; const os = out.get(id);
      if (i < os.length) {
        top[1]++; const e = os[i];
        const s = state.get(e.to);
        if (s === 1) back.add(e); else if (!s) { state.set(e.to, 1); stack.push([e.to, 0]); }
      } else { state.set(id, 2); order.push(id); stack.pop(); }
    }
  }
  order.reverse();
  // longest-path layering on the DAG
  const layer = new Map();
  for (const id of order) {
    let l = 0;
    for (const e of inc.get(id)) if (!back.has(e) && layer.has(e.from)) l = Math.max(l, layer.get(e.from) + 1);
    layer.set(id, l);
  }
  const layers = [];
  for (const id of order) { const l = layer.get(id); (layers[l] = layers[l] || []).push(id); }
  // barycenter ordering sweeps
  const pos = new Map();
  layers.forEach((L) => L.forEach((id, i) => pos.set(id, i)));
  for (let sweep = 0; sweep < 6; sweep++) {
    const down = sweep % 2 === 0;
    const seq = down ? layers.slice(1) : layers.slice(0, -1).reverse();
    for (const L of seq) {
      const bc = new Map(L.map((id) => {
        const nb = (down ? inc.get(id).map((e) => e.from) : out.get(id).map((e) => e.to)).filter((x) => pos.has(x));
        return [id, nb.length ? nb.reduce((s, x) => s + pos.get(x), 0) / nb.length : pos.get(id)];
      }));
      L.sort((a, b) => bc.get(a) - bc.get(b));
      L.forEach((id, i) => pos.set(id, i));
    }
  }
  // coordinates
  const GX = 40, GY = 60;
  let y = 20;
  const coords = new Map();
  let maxW = 0;
  const widths = layers.map((L) => L.reduce((s, id) => s + byId.get(id).w + GX, -GX));
  maxW = Math.max(...widths, 0);
  layers.forEach((L, li) => {
    let x = 20 + (maxW - widths[li]) / 2;
    const h = Math.max(...L.map((id) => byId.get(id).h));
    for (const id of L) { const n = byId.get(id); coords.set(id, { x, y, w: n.w, h: n.h }); x += n.w + GX; }
    y += h + GY;
  });
  return { coords, back, width: maxW + 40, height: y };
}

function svgGraph(container, nodes, edges, opts) {
  const { coords, back, width, height } = layoutGraph(nodes, edges, opts.entry);
  let paths = '';
  for (const e of edges) {
    const a = coords.get(e.from), b = coords.get(e.to);
    if (!a || !b) continue;
    const isBack = e.back || back.has(e);
    const x1 = a.x + a.w / 2, y1 = a.y + a.h, x2 = b.x + b.w / 2, y2 = b.y;
    let d;
    if (isBack || y2 <= y1) {
      const side = Math.max(a.x + a.w, b.x + b.w) + 30;
      d = `M${x1},${y1} C${x1},${y1 + 30} ${side},${y1 + 30} ${side},${(y1 + y2) / 2} S${x2},${y2 - 30} ${x2},${y2}`;
    } else {
      const my = (y1 + y2) / 2;
      d = `M${x1},${y1} C${x1},${my} ${x2},${my} ${x2},${y2}`;
    }
    paths += `<path class="gedge ${e.kind || ''} ${isBack ? 'back' : ''}" d="${d}" marker-end="url(#arr)"/>`;
  }
  let gs = '';
  for (const n of nodes) {
    const c = coords.get(n.id);
    if (!c) continue;
    const txt = n.lines.map((l, i) => `<text x="8" y="${16 + i * 15}" class="${i === 0 && n.titleLine ? 'addr' : ''}">${esc(l)}</text>`).join('');
    gs += `<g class="gnode ${n.cls || ''}" data-id="${n.id}" transform="translate(${c.x},${c.y})"><title>${esc(n.title || n.id)}</title><rect width="${c.w}" height="${c.h}"/>${txt}</g>`;
  }
  container.innerHTML = `<svg class="graph" xmlns="http://www.w3.org/2000/svg"><defs><marker id="arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor" style="color:var(--muted)"/></marker></defs><g class="vp">${paths}${gs}</g></svg>`;
  const svg = container.querySelector('svg'); const vp = svg.querySelector('.vp');
  const T = { x: 0, y: 0, k: 1 };
  const apply = () => vp.setAttribute('transform', `translate(${T.x},${T.y}) scale(${T.k})`);
  const fit = () => {
    const r = svg.getBoundingClientRect();
    T.k = Math.min(1.2, Math.max(0.05, Math.min(r.width / width, r.height / height)));
    T.x = (r.width - width * T.k) / 2; T.y = 10; apply();
  };
  const focus = (id) => {
    const c = coords.get(id); if (!c) return fit();
    const r = svg.getBoundingClientRect();
    T.k = Math.max(T.k, 0.8); T.x = r.width / 2 - (c.x + c.w / 2) * T.k; T.y = 40 - c.y * T.k; apply();
  };
  svg.addEventListener('wheel', (ev) => {
    ev.preventDefault();
    const r = svg.getBoundingClientRect(); const mx = ev.clientX - r.left, my = ev.clientY - r.top;
    const k2 = Math.min(4, Math.max(0.03, T.k * (ev.deltaY < 0 ? 1.15 : 1 / 1.15)));
    T.x = mx - (mx - T.x) * (k2 / T.k); T.y = my - (my - T.y) * (k2 / T.k); T.k = k2; apply();
  }, { passive: false });
  let drag = null;
  svg.addEventListener('mousedown', (ev) => { drag = { x: ev.clientX - T.x, y: ev.clientY - T.y, moved: false }; svg.classList.add('drag'); });
  window.addEventListener('mousemove', (ev) => { if (drag) { T.x = ev.clientX - drag.x; T.y = ev.clientY - drag.y; drag.moved = true; apply(); } });
  window.addEventListener('mouseup', () => { drag = null; svg.classList.remove('drag'); });
  svg.addEventListener('click', (ev) => { const g = ev.target.closest('.gnode'); if (g && opts.onClick) opts.onClick(g.dataset.id); });
  svg.addEventListener('dblclick', (ev) => { const g = ev.target.closest('.gnode'); if (g && opts.onDbl) opts.onDbl(g.dataset.id); });
  requestAnimationFrame(() => (opts.focus ? focus(opts.focus) : fit()));
  return { fit, focus, zoom: (f) => { T.k = Math.min(4, Math.max(0.03, T.k * f)); apply(); } };
}

async function viewCfg(v) {
  if (!S.func) { v.innerHTML = `<div class="empty">${esc(t('msg.select_first'))}</div>`; return; }
  const g = await api('cfg', { addr: S.func });
  v.innerHTML = `<div class="toolbar"><b>${esc(g.name)}</b><span class="muted">${g.nodes.length} blokov · ${g.loops} slučiek</span><span class="grow"></span><button id="g-zin">+</button><button id="g-zout">−</button><button id="g-fit">${esc(t('btn.fit'))}</button><button class="help" data-help="cfg">?</button></div><div class="graph-wrap" id="gw"></div>`;
  const nodes = g.nodes.map((n) => {
    const lines = [n.id + (n.loop_header ? '  ⟲ začiatok slučky' : '') + (n.entry ? '  ▶ začiatok' : ''), ...n.lines.map((l) => l.text)];
    const w = Math.min(560, Math.max(120, Math.max(...lines.map((s) => s.length)) * 7.3 + 16));
    return { id: n.id, lines: lines.map((s) => (s.length > 76 ? s.slice(0, 75) + '…' : s)), w, h: lines.length * 15 + 10, titleLine: true,
      cls: [n.entry ? 'entry' : '', n.loop_header ? 'loop' : '', n.in_loop ? 'inloop' : ''].join(' '), title: n.id };
  });
  const gr = svgGraph($('#gw'), nodes, g.edges, { entry: g.nodes.length ? g.nodes[0].id : null, onClick: (id) => select(id), onDbl: (id) => goTo(id, { tab: 'listing' }) });
  $('#g-fit').onclick = gr.fit; $('#g-zin').onclick = () => gr.zoom(1.25); $('#g-zout').onclick = () => gr.zoom(0.8);
}

async function viewCallgraph(v) {
  const depth = store.get('cg.depth', '2'); const imps = store.get('cg.imports', true);
  const root = depth === 'all' ? null : S.func;
  const g = await api('callgraph', { root, depth: depth === 'all' ? 0 : depth, imports: imps ? '1' : '0' });
  v.innerHTML = `<div class="toolbar"><label>Hĺbka okolia <select id="cg-depth">${['1', '2', '3', '5', 'all'].map((d) => `<option ${d === depth ? 'selected' : ''} value="${d}">${d === 'all' ? 'celý program' : d}</option>`).join('')}</select></label><label><input type="checkbox" id="cg-imp" ${imps ? 'checked' : ''}> importy</label><input id="cg-filter" type="search" placeholder="Zvýrazniť funkciu…"><span class="muted">${g.nodes.length} funkcií${g.truncated ? ' (orezané)' : ''}</span><span class="grow"></span><button id="g-zin">+</button><button id="g-zout">−</button><button id="g-fit">${esc(t('btn.fit'))}</button><button class="help" data-help="callgraph">?</button></div><div class="graph-wrap" id="gw"></div>`;
  const nodes = g.nodes.map((n) => ({ id: n.id, lines: [n.name], w: Math.max(80, n.name.length * 7.3 + 16), h: 26, title: `${n.name} @ ${n.id}`,
    cls: [n.kind !== 'normal' ? 'imp' : '', n.id === S.func ? 'sel' : '', n.main ? 'entry' : ''].join(' ') }));
  const gr = svgGraph($('#gw'), nodes, g.edges, { entry: root || (g.nodes.find((n) => n.main) || {}).id, focus: root,
    onClick: (id) => select(id), onDbl: (id) => goTo(id, { tab: 'pseudocode' }) });
  $('#g-fit').onclick = gr.fit; $('#g-zin').onclick = () => gr.zoom(1.25); $('#g-zout').onclick = () => gr.zoom(0.8);
  $('#cg-depth').onchange = (e) => { store.set('cg.depth', e.target.value); viewCallgraph(v); };
  $('#cg-imp').onchange = (e) => { store.set('cg.imports', e.target.checked); viewCallgraph(v); };
  $('#cg-filter').oninput = (e) => {
    const q = e.target.value.toLowerCase();
    let first = null;
    v.querySelectorAll('.gnode').forEach((n) => { const m = q && n.textContent.toLowerCase().includes(q); n.classList.toggle('match', !!m); if (m && !first) first = n.dataset.id; });
    if (first) gr.focus(first);
  };
}

// ------------------------------------------------------------------ tables
function table(v, cols, rows, opts = {}) {
  const key = opts.key || 'tbl';
  const st = S.sort[key] || {};
  const filter = (opts.filterEl && opts.filterEl.value || '').toLowerCase();
  let data = rows.filter((r) => !filter || cols.some((c) => String(r[c.k] ?? '').toLowerCase().includes(filter)));
  if (st.k) data = data.slice().sort((a, b) => { const x = a[st.k], y = b[st.k]; const c = typeof x === 'number' ? x - y : String(x).localeCompare(String(y)); return st.d ? -c : c; });
  const head = cols.map((c) => `<th data-k="${c.k}" title="${esc(c.tip || '')}">${esc(c.t)}${st.k === c.k ? (st.d ? ' ▼' : ' ▲') : ''}</th>`).join('');
  const body = data.slice(0, 5000).map((r) => `<tr class="click" data-addr="${r.addr || ''}">${cols.map((c) => `<td class="${c.cls || ''}">${c.f ? c.f(r) : esc(r[c.k])}</td>`).join('')}</tr>`).join('');
  const box = opts.box || v;
  box.innerHTML = `<table class="grid"><thead><tr>${head}</tr></thead><tbody>${body}</tbody></table>${data.length > 5000 ? '<p class="muted" style="padding:8px">Zobrazených prvých 5000 – spresnite filter.</p>' : ''}`;
  box.querySelectorAll('th').forEach((th) => th.onclick = () => { const k = th.dataset.k; S.sort[key] = { k, d: st.k === k ? !st.d : false }; table(v, cols, rows, opts); });
  box.querySelectorAll('tr.click').forEach((tr) => {
    tr.onclick = () => tr.dataset.addr && (opts.onClick ? opts.onClick(tr.dataset.addr) : select(tr.dataset.addr));
    tr.ondblclick = () => tr.dataset.addr && goTo(tr.dataset.addr, { tab: opts.dblTab || 'listing' });
  });
}
function tableView(v, title, rows, cols, opts = {}) {
  v.innerHTML = `<div class="toolbar"><b>${esc(title)}</b><span class="muted">${rows.length}</span><input type="search" id="tbl-filter" placeholder="${esc(t('filter.placeholder'))}"><span class="grow"></span>${opts.help ? `<button class="help" data-help="${opts.help}">?</button>` : ''}</div><div id="tbl-box"></div>`;
  const box = $('#tbl-box'); const fe = $('#tbl-filter');
  const draw = () => table(v, cols, rows, Object.assign({ box, filterEl: fe }, opts));
  fe.oninput = draw; draw();
  if (!rows.length && opts.empty) box.innerHTML = `<div class="empty">${esc(opts.empty)}</div>`;
}
const addrCol = { k: 'addr', t: 'Adresa', cls: 'addr' };
async function viewStrings(v) {
  const rows = await api('strings');
  tableView(v, t('tab.strings'), rows, [addrCol, { k: 'text', t: 'Text', cls: 'wrap mono', f: (r) => `<span class="sk-str">${esc(r.text)}</span>` },
    { k: 'enc', t: 'Kódovanie' }, { k: 'xrefs', t: 'Odkazy', tip: 'Koľko miest v kóde reťazec používa (x-refs)' }], { key: 'str', help: 'strings' });
}
async function viewImports(v) {
  const rows = await api('imports');
  tableView(v, t('tab.imports'), rows, [{ k: 'name', t: 'Meno', cls: 'mono' }, { k: 'library', t: 'Knižnica' },
    { k: 'callers', t: 'Volaná z', tip: 'Počet funkcií, ktoré import volajú' }, addrCol], { key: 'imp', help: 'imports', dblTab: 'pseudocode' });
}
async function viewExports(v) {
  const rows = await api('exports');
  tableView(v, t('tab.exports'), rows, [{ k: 'name', t: 'Meno', cls: 'mono' }, addrCol, { k: 'ordinal', t: 'Ordinál' }, { k: 'forwarder', t: 'Presmerovanie' }], { key: 'exp', empty: t('empty.exports') });
}
async function viewSections(v) {
  const rows = await api('sections');
  v.innerHTML = `<div class="toolbar"><b>${esc(t('tab.sections'))}</b><span class="muted">${esc((S.state.file || {}).format || '')}</span><span class="grow"></span><button id="sec-rel">${esc(t('adv.relocations'))}</button><button class="help" data-help="sections">?</button></div><div id="tbl-box"></div><div id="hex" class="hex"></div>`;
  const showHex = async (addr) => {
    const h = await api('hexdump', { addr, len: 1024 });
    $('#hex').innerHTML = `<div class="muted">Hexadecimálny výpis od ${h.addr} (offset v súbore ${h.offset || '—'})</div>` +
      h.rows.map((r) => `<span class="a">${r.addr}</span>  <span class="h">${r.hex.padEnd(47)}</span>  <span class="t">${esc(r.ascii)}</span>`).join('\n');
  };
  table(v, [{ k: 'name', t: 'Meno', cls: 'mono' }, addrCol, { k: 'size', t: 'Veľkosť' }, { k: 'offset', t: 'Offset', cls: 'mono' },
    { k: 'perms', t: 'Práva', cls: 'mono', tip: 'r = čítanie, w = zápis, x = spúšťanie' }, { k: 'kind', t: 'Typ' }], rows,
  { key: 'sec', box: $('#tbl-box'), onClick: (a) => guarded(() => showHex(a)) });
  $('#sec-rel').onclick = async () => { const rel = await api('relocations'); tableView(v, t('adv.relocations'), rel, [addrCol, { k: 'type', t: 'Typ' }, { k: 'symbol', t: 'Symbol', cls: 'mono' }, { k: 'addend', t: 'Addend' }], { key: 'rel' }); };
  if (rows.length) guarded(() => showHex(rows[0].addr));
}
async function viewBookmarks(v) {
  const rows = await api('bookmarks');
  tableView(v, t('tab.bookmarks'), rows, [addrCol, { k: 'type', t: 'Typ', f: (r) => (r.type === 'bookmark' ? '🔖 záložka' : '💬 komentár') }, { k: 'label', t: 'Text' }, { k: 'name', t: 'Miesto', cls: 'mono' }], { key: 'bm', empty: t('empty.bookmarks') });
}
async function viewSearch(v) {
  tableView(v, t('tab.search') + (S.searchQuery ? `: „${S.searchQuery}“` : ''), S.searchHits, [addrCol, { k: 'text', t: 'Nájdené', cls: 'wrap mono' }, { k: 'func', t: 'Vo funkcii', cls: 'mono' }], { key: 'search', empty: t('empty.search') });
}
async function viewViews(v) {
  const names = (S.state.views || []);
  if (!names.length) { v.innerHTML = `<div class="empty">${esc(t('empty.views'))}</div>`; return; }
  const cur = S.viewName && names.includes(S.viewName) ? S.viewName : names[0];
  const r = await api('view', { name: cur });
  v.innerHTML = `<div class="toolbar"><select id="view-sel">${names.map((n) => `<option ${n === cur ? 'selected' : ''}>${esc(n)}</option>`).join('')}</select></div><pre class="code" style="padding:12px">${esc(r.text)}</pre>`;
  $('#view-sel').onchange = (e) => { S.viewName = e.target.value; viewViews(v); };
}

// ------------------------------------------------------------------ details panel
async function renderDetails() {
  const d = $('#details');
  if (!S.cur || !S.state || S.state.status !== 'ready') { d.innerHTML = `<p class="muted">${esc(t('empty.details'))}</p>`; return; }
  let info;
  try { info = await api('addrinfo', { addr: S.cur }); } catch (e) { d.innerHTML = `<p class="muted">${esc(e.message)}</p>`; return; }
  const kindLabel = { function: 'funkcia', import: 'importovaná funkcia', string: 'reťazec', data: 'dáta', code: 'kód', unmapped: 'mimo súboru' }[info.kind] || info.kind;
  const xr = (list) => list.length ? list.map((x) => `<div class="xref"><span class="k">${esc(x.label)}</span><span class="lk" data-go="${x.addr}">${esc(x.func ? x.func + (x.addr !== x.func_addr ? '+' + (parseInt(x.addr, 16) - parseInt(x.func_addr, 16)).toString(16) : '') : x.addr)}</span></div>`).join('') : '<p class="muted">žiadne</p>';
  const fl = (list) => list.map((c) => `<span class="lk" data-go="${c.addr}">${esc(c.name)}</span>`).join(', ') || '<span class="muted">—</span>';
  d.innerHTML = `
    <div class="name">${esc(info.name)}</div>
    <div class="muted">${esc(kindLabel)}${info.user_named ? ' · premenované' : ''}</div>
    <div class="acts">
      <button data-act="rename">${esc(t('btn.rename'))} <span class="muted">(${esc(S.keys.rename)})</span></button>
      <button data-act="comment">${esc(t('btn.comment'))}</button>
      <button data-act="bookmark">${esc(t('btn.bookmark'))}</button>
    </div>
    <div class="acts">
      <button data-tabgo="listing">${esc(t('tab.listing'))}</button>
      ${info.function || info.kind === 'function' ? `<button data-tabgo="pseudocode">${esc(t('tab.pseudocode'))}</button><button data-tabgo="cfg">${esc(t('tab.cfg'))}</button>` : ''}
    </div>
    <div class="kv">
      <span>Adresa</span><span class="mono">${info.addr}</span>
      ${info.offset ? `<span>Offset v súbore</span><span class="mono">${info.offset}</span>` : ''}
      ${info.rva && (S.state.file || {}).format === 'PE' ? `<span><abbr title="${esc(t('abbr.RVA'))}">RVA</abbr></span><span class="mono">${info.rva}</span>` : ''}
      ${info.section ? `<span>Sekcia</span><span>${esc(info.section)}</span>` : ''}
      ${info.function && info.function.addr !== info.addr ? `<span>Vo funkcii</span><span class="lk" data-go="${info.function.addr}">${esc(info.function.name)}</span>` : ''}
      ${info.size !== undefined ? `<span>Veľkosť</span><span>${info.size} B, ${info.blocks} blokov, ${info.insns} inštr.</span>` : ''}
      ${info.library ? `<span>Knižnica</span><span>${esc(info.library)}</span>` : ''}
    </div>
    ${info.string ? `<h3>Text</h3><div class="sk-str mono" style="white-space:pre-wrap;word-break:break-all">${esc(info.string)}</div>` : ''}
    ${info.comment ? `<h3>💬 Komentár</h3><div>${esc(info.comment)}</div>` : ''}
    ${info.bookmark ? `<h3>🔖 Záložka</h3><div>${esc(info.bookmark)}</div>` : ''}
    ${info.callers ? `<h3>${esc(t('details.callers'))} (${info.callers.length})</h3><div class="mono">${fl(info.callers)}</div><h3>${esc(t('details.callees'))} (${info.callees.length})</h3><div class="mono">${fl(info.callees)}</div>` : ''}
    <h3>${abbrify(t('details.xrefs_to'))} (${info.xrefs_to.length})</h3>${xr(info.xrefs_to)}
    ${info.xrefs_from.length ? `<h3>${esc(t('details.xrefs_from'))} (${info.xrefs_from.length})</h3>${info.xrefs_from.map((x) => `<div class="xref"><span class="k">${esc(x.kind)}</span><span class="lk" data-go="${x.addr}">${esc(x.target)}</span></div>`).join('')}` : ''}
  `;
  d.querySelectorAll('[data-act]').forEach((b) => b.onclick = () => actions[b.dataset.act]());
  d.querySelectorAll('[data-tabgo]').forEach((b) => b.onclick = () => goTo(S.cur, { tab: b.dataset.tabgo, history: false }));
}

// ------------------------------------------------------------------ actions
async function afterEdit() { await poll.once(); }
poll.once = async () => {
  const st = await api('state'); S.state = st; renderStatus(st);
  if (st.revision !== S.revision) { S.revision = st.revision; await onData(false); }
};
const actions = {
  async rename() {
    if (S.tab === 'pseudocode' && S.selVar && S.func) {
      const el = document.querySelector(`#pseudo .sk-var[data-var="${CSS.escape(S.selVar)}"]`);
      const nv = await dialog({ text: t('prompt.rename_var', { name: el ? el.textContent : S.selVar }), value: el ? el.textContent : '' });
      if (nv !== null) await guarded(async () => { await post('rename_var', { func: S.func, key: S.selVar, name: nv }); await afterEdit(); });
      return;
    }
    if (!S.cur) return toast(t('msg.select_first'));
    const info = await guarded(() => api('addrinfo', { addr: S.cur })); if (!info) return;
    const nv = await dialog({ text: t('prompt.rename', { name: info.name }), value: info.user_named ? info.name : info.name });
    if (nv === null) return;
    await guarded(async () => { await post('rename', { addr: S.cur, name: nv }); await afterEdit(); });
  },
  async comment() {
    if (!S.cur) return toast(t('msg.select_first'));
    const info = await guarded(() => api('addrinfo', { addr: S.cur })); if (!info) return;
    const nv = await dialog({ text: t('prompt.comment', { addr: S.cur }), value: info.comment || '' });
    if (nv !== null) await guarded(async () => { await post('comment', { addr: S.cur, text: nv }); await afterEdit(); });
  },
  async bookmark() {
    if (!S.cur) return toast(t('msg.select_first'));
    const info = await guarded(() => api('addrinfo', { addr: S.cur })); if (!info) return;
    const nv = await dialog({ text: t('prompt.bookmark', { addr: S.cur }), value: info.bookmark || info.name });
    if (nv !== null) await guarded(async () => { await post('bookmark', { addr: S.cur, label: nv }); await afterEdit(); });
  },
  async goto() {
    const q = await dialog({ text: t('prompt.goto') });
    if (!q) return;
    const f = S.funcs.find((x) => x.name === q.trim());
    if (f) return goTo(f.addr);
    await guarded(async () => { const info = await api('addrinfo', /^(0x)?[0-9a-f]+$/i.test(q.trim()) ? { addr: q.trim().startsWith('0x') ? q.trim() : '0x' + q.trim() } : { name: q.trim() }); await goTo(info.addr); });
  },
  async undo() { if (S.state && S.state.status === 'ready') await guarded(async () => { const r = await post('undo'); toast(r.what ? t('msg.undo', { what: r.what }) : t('msg.nothing_undo')); await afterEdit(); }); },
  async redo() { if (S.state && S.state.status === 'ready') await guarded(async () => { const r = await post('redo'); if (r.what) toast(t('msg.redo', { what: r.what })); await afterEdit(); }); },
  async save() { if (S.state && S.state.status === 'ready') await guarded(async () => { const r = await post('save'); toast(t('msg.saved', { path: r.path })); await afterEdit(); }); },
  search() { $('#search').focus(); $('#search').select(); },
  listing() { S.tab = 'listing'; renderTabs(); renderView(); },
  pseudocode() { S.tab = 'pseudocode'; renderTabs(); renderView(); },
  cfg() { S.tab = 'cfg'; renderTabs(); renderView(); },
  callgraph() { S.tab = 'callgraph'; renderTabs(); renderView(); },
  back() { goBack(); },
  help() { showHelp('functions', $('#btn-help')); },
};

// ------------------------------------------------------------------ search
let searchTimer = null;
async function suggest(q) {
  const box = $('#search-suggest');
  if (!q || !S.state || S.state.status !== 'ready') { box.hidden = true; return; }
  const ql = q.toLowerCase();
  const items = [];
  for (const f of S.funcs) if (f.name.toLowerCase().includes(ql) || f.addr === ql) { items.push({ addr: f.addr, label: f.name, kind: f.kind === 'normal' ? 'funkcia' : 'import' }); if (items.length >= 8) break; }
  try { (await api('search', { kind: 'string', q })).slice(0, 6).forEach((h) => items.push({ addr: h.addr, label: '"' + h.text + '"', kind: 'reťazec' })); } catch (e) { /* ignore */ }
  if (/^(0x)?[0-9a-f]{3,}$/i.test(q)) items.unshift({ addr: q.startsWith('0x') ? q.toLowerCase() : '0x' + q.toLowerCase(), label: 'Skočiť na adresu ' + q, kind: 'adresa' });
  items.push({ addr: null, label: `Hľadať „${q}“ všade (Enter)`, kind: '' });
  box.innerHTML = items.map((it, i) => `<div data-i="${i}"><span class="pill">${esc(it.kind)}</span><span class="mono">${esc(it.label)}</span></div>`).join('');
  box.hidden = false;
  box.querySelectorAll('div').forEach((el) => el.onmousedown = (e) => { e.preventDefault(); const it = items[+el.dataset.i]; box.hidden = true; it.addr ? goTo(it.addr) : fullSearch(q, 'string'); });
}
async function fullSearch(q, kind) {
  await guarded(async () => {
    const regex = kind === 'insn' || kind === 'code' ? '1' : '0';
    let hits = await api('search', { kind, q, regex });
    if (kind === 'string') hits = hits.concat(await api('search', { kind: 'name', q }));
    S.searchHits = hits; S.searchQuery = q; S.tab = 'search'; renderTabs(); renderView();
  });
}

// ------------------------------------------------------------------ advanced menu, help, settings, onboarding
function buildAdvanced() {
  const m = $('#adv-menu');
  const ready = S.state && S.state.status === 'ready';
  if (!ready) { m.innerHTML = `<div class="note">${esc(t('advanced.locked'))}</div>`; return; }
  const items = [['hex', 'adv.hex'], ['insn', 'adv.insn'], ['code', 'adv.code'], ['md', 'adv.report_md'], ['html', 'adv.report_html'], ['plugins', 'adv.plugins'], ['diag', 'adv.diagnostics']];
  m.innerHTML = items.map(([k, l]) => `<button data-adv="${k}">${esc(t(l))}</button>`).join('') + `<div class="note">${abbrify(t('adv.future'))}</div>`;
  m.querySelectorAll('[data-adv]').forEach((b) => b.onclick = async () => {
    m.hidden = true;
    const k = b.dataset.adv;
    if (k === 'hex' || k === 'insn' || k === 'code') {
      const hint = { hex: 'Napr. 48 8b ?? 05 (?? = ľubovoľný bajt)', insn: 'Napr. call.*strcmp alebo ^xor', code: 'Napr. strcmp\\( alebo while' }[k];
      const q = await dialog({ text: t('adv.' + k) + ' ' + hint });
      if (q) fullSearch(q, k);
    } else if (k === 'md' || k === 'html') {
      await guarded(async () => {
        const r = await api('report', { fmt: k });
        const blob = new Blob([r.text], { type: k === 'html' ? 'text/html' : 'text/markdown' });
        const a = document.createElement('a'); a.href = URL.createObjectURL(blob);
        a.download = ((S.state.file || {}).name || 'report') + '.report.' + k; a.click(); URL.revokeObjectURL(a.href);
      });
    } else if (k === 'plugins') {
      const ps = S.state.plugins || [];
      const div = document.createElement('div');
      div.innerHTML = ps.length ? `<table class="grid"><tr><th>Plugin</th><th>Verzia</th><th>Stav</th><th>Hooky</th></tr>${ps.map((p) => `<tr><td>${esc(p.name)}</td><td>${esc(p.version)}</td><td>${esc(p.status)}${p.error ? '<br><small>' + esc(p.error) + '</small>' : ''}</td><td>${esc(p.hooks.join(', '))}</td></tr>`).join('')}</table>`
        : '<p>Žiadne pluginy. Návod: docs/plugin-tutorial.md (priečinok ~/.reversa/plugins).</p>';
      await dialog({ text: t('adv.plugins'), input: false, body: div });
    } else if (k === 'diag') {
      const ds = S.state.diagnostics || [];
      const div = document.createElement('div');
      div.innerHTML = ds.length ? ds.map((d) => `<p><b>${esc(d.severity)}</b>: ${esc(d.message)}</p>`).join('') : '<p>Súbor sa načítal bez problémov.</p>';
      await dialog({ text: t('adv.diagnostics'), input: false, body: div });
    }
  });
}
function showHelp(key, anchor) {
  const p = $('#popover');
  const doc = HELP_DOC[key] || 'quickstart.md';
  p.innerHTML = `<div>${abbrify(t('help.' + key))}</div><p><a href="/docs/${doc}" target="_blank" rel="noopener">Dokumentácia: docs/${doc}</a> · <a href="#" id="pop-onb">Úvodný sprievodca</a></p>`;
  const r = anchor.getBoundingClientRect();
  p.style.top = Math.min(window.innerHeight - 160, r.bottom + 6) + 'px';
  p.style.left = Math.max(8, Math.min(window.innerWidth - 380, r.left - 160)) + 'px';
  p.hidden = false;
  $('#pop-onb').onclick = (e) => { e.preventDefault(); p.hidden = true; onboarding(0); };
}
function comboOf(e) {
  const k = e.key.length === 1 ? e.key.toUpperCase() : e.key;
  return [e.ctrlKey || e.metaKey ? 'Ctrl' : '', e.altKey ? 'Alt' : '', e.shiftKey && e.key.length > 1 ? 'Shift' : '', k].filter(Boolean).join('+');
}
async function openSettings() {
  const div = document.createElement('div');
  const fs = store.get('fontsize', 14);
  div.innerHTML = `<label>${esc(t('settings.font'))} <input type="number" id="set-fs" min="10" max="24" value="${fs}" style="width:70px"></label>
    <p class="muted">${esc(t('settings.keys'))}</p>
    <div class="keys">${Object.keys(DEFAULT_KEYS).map((a) => `<span>${esc(t('action.' + a))}</span><input data-action="${a}" value="${esc(S.keys[a])}" readonly>`).join('')}</div>
    <button type="button" id="set-reset">${esc(t('settings.reset'))}</button> <button type="button" id="set-onb">${esc(t('settings.onboarding'))}</button>`;
  div.querySelectorAll('.keys input').forEach((inp) => inp.addEventListener('keydown', (e) => { e.preventDefault(); e.stopPropagation(); if (['Control', 'Shift', 'Alt', 'Meta'].includes(e.key)) return; inp.value = comboOf(e); }));
  div.querySelector('#set-reset').onclick = () => div.querySelectorAll('.keys input').forEach((inp) => { inp.value = DEFAULT_KEYS[inp.dataset.action]; });
  div.querySelector('#set-onb').onclick = () => { $('#dlg').close('cancel'); onboarding(0); };
  const ok = await dialog({ text: t('settings.title'), input: false, body: div });
  if (!ok) return;
  div.querySelectorAll('.keys input').forEach((inp) => { S.keys[inp.dataset.action] = inp.value; });
  store.set('keys', S.keys);
  const size = Math.min(24, Math.max(10, +div.querySelector('#set-fs').value || 14));
  store.set('fontsize', size); document.documentElement.style.setProperty('--fs', size + 'px');
  renderDetails();
}
function onboarding(step) {
  const o = $('#onboarding');
  const n = 5;
  if (step >= n) { o.hidden = true; store.set('onboarded', true); return; }
  o.hidden = false;
  $('#onb-step').textContent = `${step + 1} / ${n}`;
  $('#onb-title').textContent = t(`onb.${step + 1}.title`);
  $('#onb-text').innerHTML = abbrify(t(`onb.${step + 1}.text`));
  $('#onb-prev').hidden = step === 0;
  $('#onb-demo').hidden = step !== 1;
  $('#onb-next').textContent = step === n - 1 ? t('btn.finish') : t('btn.next');
  $('#onb-next').onclick = () => onboarding(step + 1);
  $('#onb-prev').onclick = () => onboarding(step - 1);
  $('#onb-skip').onclick = () => onboarding(n);
  $('#onb-demo').onclick = () => { $('#btn-demo').click(); onboarding(step + 1); };
}

// ------------------------------------------------------------------ wiring
function wire() {
  $('#btn-open').onclick = () => $('#file-input').click();
  $('#file-input').onchange = async (e) => {
    const f = e.target.files[0]; if (!f) return;
    $('#errorbar').hidden = true;
    await guarded(async () => {
      const r = await fetch('/api/upload', { method: 'POST', headers: { 'X-Reversa-Token': TOKEN, 'X-Filename': encodeURIComponent(f.name) }, body: f });
      const j = await r.json(); if (j.error) throw j.error;
      S.status = 'loading'; S.revision = -1;
    });
    e.target.value = '';
  };
  $('#btn-demo').onclick = () => guarded(async () => { $('#errorbar').hidden = true; await post('demo'); S.status = 'loading'; S.revision = -1; });
  $('#btn-save').onclick = actions.save; $('#btn-undo').onclick = actions.undo; $('#btn-redo').onclick = actions.redo;
  $('#btn-cancel').onclick = () => guarded(() => post('cancel'));
  $('#btn-theme').onclick = () => {
    const cur = document.documentElement.dataset.theme || (matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light');
    const next = cur === 'dark' ? 'light' : 'dark';
    document.documentElement.dataset.theme = next; store.set('theme', next);
  };
  $('#btn-settings').onclick = openSettings;
  $('#btn-help').onclick = (e) => showHelp('functions', e.currentTarget);
  $('#btn-advanced').onclick = (e) => { e.stopPropagation(); buildAdvanced(); $('#adv-menu').hidden = !$('#adv-menu').hidden; };
  $('#error-close').onclick = () => { $('#errorbar').hidden = true; };
  $('#error-copy').onclick = async () => {
    const e = S.lastError || {};
    const text = `REVERSA ${S.state ? S.state.version : ''}\n${e.message}\n\n${e.detail || ''}`;
    try { await navigator.clipboard.writeText(text); toast(t('msg.copied')); } catch (err) { await dialog({ text: t('btn.copy_detail'), value: text }); }
  };
  $('#func-filter').oninput = renderFuncs;
  $('#func-list').onclick = (e) => { const r = e.target.closest('.frow'); if (r) goTo(r.dataset.addr, { tab: S.tab === 'listing' || S.tab === 'pseudocode' || S.tab === 'cfg' || S.tab === 'callgraph' ? S.tab : 'pseudocode' }); };
  const search = $('#search');
  search.oninput = () => { clearTimeout(searchTimer); searchTimer = setTimeout(() => suggest(search.value.trim()), 150); };
  search.onkeydown = (e) => { if (e.key === 'Enter' && search.value.trim()) { $('#search-suggest').hidden = true; fullSearch(search.value.trim(), 'string'); } if (e.key === 'Escape') { $('#search-suggest').hidden = true; search.blur(); } };
  search.onblur = () => setTimeout(() => { $('#search-suggest').hidden = true; }, 150);
  document.addEventListener('click', (e) => {
    const go = e.target.closest('[data-go]');
    if (go) { e.preventDefault(); goTo(go.dataset.go, { tab: S.tab === 'pseudocode' && funcOf(go.dataset.go) === go.dataset.go ? 'pseudocode' : (S.tab === 'pseudocode' ? 'listing' : undefined) }); return; }
    const sel = e.target.closest('[data-sel]');
    if (sel) { select(sel.dataset.sel); return; }
    const v = e.target.closest('[data-var]');
    if (v) { markVar(v.dataset.var); return; }
    const ln = e.target.closest('.ln[data-addr]');
    if (ln && !e.target.closest('.lk')) select(ln.dataset.addr);
    const h = e.target.closest('button.help[data-help]');
    if (h) { e.stopPropagation(); showHelp(h.dataset.help, h); return; }
    if (!e.target.closest('#popover')) $('#popover').hidden = true;
    if (!e.target.closest('.menu-wrap')) $('#adv-menu').hidden = true;
  });
  $('#view').addEventListener('scroll', () => {
    if (S.tab !== 'listing') return;
    const v = $('#view');
    if (v.scrollTop + v.clientHeight > v.scrollHeight - 400) loadMore('down');
    if (v.scrollTop < 200) loadMore('up');
  });
  document.addEventListener('keydown', (e) => {
    if ($('#dlg').open || !$('#onboarding').hidden) return;
    const inInput = ['INPUT', 'TEXTAREA', 'SELECT'].includes(document.activeElement.tagName);
    const combo = comboOf(e);
    const action = Object.keys(S.keys).find((a) => S.keys[a] === combo);
    if (!action) return;
    if (inInput && !combo.startsWith('Ctrl') && combo !== 'F1') return;
    e.preventDefault();
    actions[action]();
  });
  window.addEventListener('beforeunload', (e) => { if (S.state && S.state.dirty) { e.preventDefault(); e.returnValue = ''; } });
}

async function init() {
  const theme = store.get('theme', null); if (theme) document.documentElement.dataset.theme = theme;
  document.documentElement.style.setProperty('--fs', store.get('fontsize', 14) + 'px');
  await loadI18n();
  renderTabs(); wire(); renderView(); renderDetails(); renderFuncs();
  if (!store.get('onboarded', false)) onboarding(0);
  poll();
}
init();
