import './style.css';
import {
  inPlugin, send, onMessage,
  type LibEntry, type LoadedMsg, type ParamDef, type PresetEntry, type UiSpec,
} from './bridge';
import { Knob } from './knob';
import { buildPanel, type Panel, type PanelHost } from './panel';
import { Keyboard } from './keyboard';
import { mockLibs, mockParams, mockRoots, mockUi } from './mock';

type Kind = 'all' | 'folder' | 'archive';
type View = 'shelf' | 'library' | 'player';
type HostId = 'gain' | 'tone' | 'reverb' | 'attack' | 'decay' | 'sustain' | 'release';

const state = {
  libs: [] as LibEntry[],
  roots: [] as string[],
  params: [] as ParamDef[],
  values: {} as Record<string, number>,
  filter: '',
  kind: 'all' as Kind,
  view: 'shelf' as View,
  openLib: '',
  selected: '',
  loading: '',
  busy: false,
  voices: 0,
  peak: 0,
  live: false,
  ui: null as UiSpec | null,
  uiTab: '',
  uiValues: {} as Record<string, number>,
};

const knobs = new Map<string, Knob>();

const app = document.querySelector<HTMLDivElement>('#app')!;
app.innerHTML = `
  <header class="bar">
    <button class="brand" id="home" title="All libraries">
      <svg class="mark" viewBox="0 0 40 40" aria-hidden="true">
        <path d="M20 4 L34 12 L34 28 L20 36 L6 28 L6 12 Z" />
        <path class="inner" d="M20 11 L28 15.5 L28 24.5 L20 29 L12 24.5 L12 15.5 Z" />
        <circle cx="20" cy="20" r="2.4" />
      </svg>
      <span class="wordmark">
        <span class="word">TROVE</span>
        <span class="tagline">sampled instruments</span>
      </span>
    </button>

    <div class="search" id="search">
      <svg viewBox="0 0 20 20" aria-hidden="true"><circle cx="8.5" cy="8.5" r="5.2"/><path d="M12.6 12.6 L17 17"/></svg>
      <input id="q" type="search" placeholder="Search the shelf" spellcheck="false" autocomplete="off" />
      <kbd>/</kbd>
    </div>

    <div class="actions">
      <span class="scan" id="scan">scanning<i></i></span>
      <button id="rescan" class="ghost" title="Look for new libraries">
        <svg viewBox="0 0 20 20" aria-hidden="true"><path d="M16.5 8.5a6.5 6.5 0 1 0-1.2 5.4"/><path d="M16.8 3.6v5h-5"/></svg>
      </button>
      <button id="addroot" class="primary">Add folder</button>
    </div>
  </header>

  <main class="body">
    <aside class="side">
      <div class="side-head"><h2>Folders</h2><span id="libCount" class="pill">0</span></div>
      <ul id="roots" class="roots"></ul>
      <div class="side-note">
        Put <code>.dspreset</code> presets inside folders, or point Trove at
        <code>.dsbundle</code> and <code>.dslibrary</code> libraries. It finds
        everything on its own.
      </div>
      <div class="side-foot" id="demoNote"></div>
    </aside>

    <div class="main">
      <section class="view" id="shelfView">
        <div class="shelf-head">
          <div class="chips" id="chips">
            <button data-kind="all" class="on">All</button>
            <button data-kind="folder">Folders</button>
            <button data-kind="archive">Archives</button>
          </div>
          <div class="tally" id="tally"></div>
        </div>
        <div class="grid" id="grid"></div>
        <div class="empty" id="empty">
          <div class="empty-card">
            <h3>The shelf is empty</h3>
            <p>Point Trove at the folder where your sample libraries live.</p>
            <button id="emptyAdd" class="primary">Add a folder</button>
            <span>or run <code>npm run demo</code> to build two test libraries</span>
          </div>
        </div>
      </section>

      <section class="view" id="libraryView" hidden>
        <div class="lib-head">
          <button class="back" id="libBack">‹ All libraries</button>
          <div class="lib-art" id="libArt"></div>
          <div class="lib-info">
            <h2 id="libName"></h2>
            <div class="lib-meta" id="libMeta"></div>
          </div>
        </div>
        <div class="presets" id="presets"></div>
        <div class="empty" id="libEmpty" hidden>
          <div class="empty-card"><h3>No presets match</h3><p>Try another search.</p></div>
        </div>
      </section>

      <section class="view" id="playerView" hidden>
        <div class="player-head">
          <button class="back" id="playerBack">‹ Library</button>
          <div class="player-title">
            <b id="playerName"></b>
            <span id="playerSub"></span>
          </div>
          <div class="p-tabs" id="playerTabs"></div>
          <span class="p-voices" id="playerVoices"></span>
        </div>
        <div class="stage-wrap" id="stageWrap"></div>
      </section>
    </div>
  </main>

  <footer class="tray">
    <div class="tray-top">
      <div class="now">
        <span class="now-tag">loaded</span>
        <div class="now-name" id="nowName">Nothing loaded</div>
        <div class="now-sub" id="nowSub">Pick a library from the shelf</div>
      </div>
      <div class="meters">
        <div class="meter"><i id="meterFill"></i></div>
        <span id="voiceCount">0 voices</span>
      </div>
      <div class="knobs" id="knobs"></div>
    </div>
    <div class="keys" id="keys"></div>
    <div class="tray-foot">
      <span><b>A–K</b> play<span class="dot">·</span><b>Z X</b> octave<span class="dot">·</span><b>Space</b> sustain</span>
      <span class="hold" id="hold">sustain</span>
    </div>
  </footer>

  <div class="veil" id="veil" aria-live="polite">
    <div class="veil-card">
      <div class="veil-ring"><i></i><i></i><i></i></div>
      <div class="veil-name" id="veilName"></div>
      <div class="veil-sub">loading samples</div>
    </div>
  </div>

  <div class="toast" id="toast"></div>
`;

const $ = <T extends HTMLElement>(id: string): T => document.getElementById(id) as T;
const grid = $('grid');
const empty = $('empty');
const rootsList = $('roots');
const tally = $('tally');
const libCount = $('libCount');
const scanTag = $('scan');
const meterFill = $('meterFill');
const voiceCount = $('voiceCount');
const nowName = $('nowName');
const nowSub = $('nowSub');
const toastEl = $('toast');
const search = $<HTMLInputElement>('q');
const searchBox = $('search');
const stageWrap = $('stageWrap');

let toastTimer = 0;
// A full-window "loading" card, so opening a library answers straight away.
// It waits a beat before showing, so a quick load never flashes it.
let veilTimer = 0;
let veilGuard = 0;
function setLoading(name: string | null): void {
  const veil = $('veil');
  clearTimeout(veilTimer);
  clearTimeout(veilGuard);
  if (name === null) {
    veil.classList.remove('show');
    return;
  }
  $('veilName').textContent = name;
  veilTimer = window.setTimeout(() => veil.classList.add('show'), 140);
  veilGuard = window.setTimeout(() => veil.classList.remove('show'), 90000);
}

function toast(message: string, bad = false): void {
  toastEl.textContent = message;
  toastEl.className = `toast show${bad ? ' bad' : ''}`;
  clearTimeout(toastTimer);
  toastTimer = window.setTimeout(() => { toastEl.className = 'toast'; }, 4200);
}

function initials(name: string): string {
  return name.split(/\s+/).filter(Boolean).slice(0, 2).map((w) => w[0]?.toUpperCase() ?? '').join('');
}

function plural(n: number, word: string): string {
  const many = word.endsWith('y') ? `${word.slice(0, -1)}ies` : `${word}s`;
  return `${n} ${n === 1 ? word : many}`;
}

function el<K extends keyof HTMLElementTagNameMap>(tag: K, clazz?: string): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag);
  if (clazz) node.className = clazz;
  return node;
}

// ------------------------------------------------------------------- views

function showView(view: View): void {
  state.view = view;
  $('shelfView').hidden = view !== 'shelf';
  $('libraryView').hidden = view !== 'library';
  $('playerView').hidden = view !== 'player';
  searchBox.classList.toggle('off', view === 'player');
  if (view !== 'player') {
    search.placeholder = view === 'library' ? 'Search presets' : 'Search the shelf';
  }
}

function showShelf(): void {
  state.openLib = '';
  showView('shelf');
  renderGrid();
  renderRoots();
}

function openLibrary(lib: LibEntry): void {
  if (lib.error && lib.presets.length === 0) {
    toast(lib.error, true);
    return;
  }
  state.openLib = lib.id;
  state.filter = '';
  search.value = '';
  showView('library');
  renderLibrary();
}

function currentLibrary(): LibEntry | undefined {
  return state.libs.find((l) => l.id === state.openLib);
}

function renderLibrary(): void {
  const lib = currentLibrary();
  if (!lib) return showShelf();

  const art = $('libArt');
  art.replaceChildren();
  art.style.setProperty('--tint', lib.tint || '#2f3a35');
  art.className = 'lib-art';
  art.style.backgroundImage = '';
  if (lib.art) {
    art.style.backgroundImage = `url("${lib.art}")`;
    art.classList.add('photo');
  } else {
    art.textContent = initials(lib.name);
  }

  $('libName').textContent = lib.name;
  const bits = [plural(lib.count, 'preset'), lib.kind === 'archive' ? 'archive' : lib.kind === 'bundle' ? 'bundle' : 'folder'];
  if (lib.zones) bits.push(plural(lib.zones, 'zone'));
  $('libMeta').replaceChildren(
    ...bits.flatMap((text, i) => {
      const nodes: Node[] = [document.createTextNode(text)];
      if (i < bits.length - 1) nodes.push(el('span', 'sep'));
      return nodes;
    }),
  );
  $('libMeta').title = lib.source;

  renderPresets();
}

function renderPresets(): void {
  const lib = currentLibrary();
  const list = $('presets');
  if (!lib) return;
  const q = state.filter.trim().toLowerCase();
  const shown = lib.presets.filter((p) => !q || p.name.toLowerCase().includes(q) || p.preset.toLowerCase().includes(q));

  list.replaceChildren(...shown.map((preset, i) => presetRow(preset, i)));
  $('libEmpty').hidden = shown.length > 0;
}

function presetRow(preset: PresetEntry, index: number): HTMLElement {
  const row = el('button', 'preset');
  row.dataset.id = preset.id;
  if (preset.id === state.selected) row.classList.add('selected');
  if (preset.id === state.loading) row.classList.add('loading');
  if (!preset.ok) row.classList.add('bad');

  const num = el('span', 'num');
  num.textContent = String(index + 1).padStart(2, '0');
  const name = el('span', 'pn');
  name.textContent = preset.name;
  const zones = el('span', 'pz');
  zones.textContent = preset.ok ? plural(preset.zones, 'zone') : 'unreadable';
  const range = el('span', 'pr');
  range.textContent = preset.ok ? `${noteLabel(preset.low)}–${noteLabel(preset.high)}` : '';
  const mark = el('span', 'go');
  mark.textContent = preset.id === state.loading ? '···' : preset.id === state.selected ? 'playing' : '›';

  row.append(num, name, zones, range, mark);
  row.title = preset.ok ? preset.preset : `${preset.preset}\n${preset.error ?? ''}`;
  return row;
}

const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
function noteLabel(note: number): string {
  return `${NOTE_NAMES[((note % 12) + 12) % 12]}${Math.floor(note / 12) - 1}`;
}

// ------------------------------------------------------------------- shelf

function cardFor(lib: LibEntry): HTMLElement {
  const card = el('button', 'card');
  card.dataset.id = lib.id;
  if (lib.id === state.openLib) card.classList.add('selected');
  if (lib.error && lib.presets.length === 0) card.classList.add('bad');

  const art = el('div', 'art');
  art.style.setProperty('--tint', lib.tint || '#2f3a35');
  if (lib.art) {
    art.style.backgroundImage = `url("${lib.art}")`;
    art.classList.add('photo');
  } else {
    const span = el('span', 'initials');
    span.textContent = initials(lib.name);
    art.appendChild(span);
  }
  if (lib.error && lib.presets.length === 0) art.insertAdjacentHTML('beforeend', '<span class="badge">unreadable</span>');
  if (lib.id === state.loading) art.insertAdjacentHTML('beforeend', '<span class="spin"></span>');

  const meta = el('div', 'meta');
  const name = el('div', 'name');
  name.textContent = lib.name;
  name.title = lib.name;
  const sub = el('div', 'sub');
  sub.textContent = lib.presets.length
    ? `${plural(lib.presets.length, 'preset')} · ${lib.kind}`
    : 'empty';
  meta.append(name, sub);

  card.append(art, meta);
  card.title = lib.source;
  return card;
}

function renderGrid(): void {
  const q = state.filter.trim().toLowerCase();
  const shown = state.libs.filter((lib) => {
    if (state.kind !== 'all' && lib.kind !== state.kind) return false;
    if (!q) return true;
    if (lib.name.toLowerCase().includes(q) || lib.source.toLowerCase().includes(q)) return true;
    return lib.presets.some((p) => p.name.toLowerCase().includes(q));
  });

  const scroll = grid.scrollTop;
  grid.replaceChildren(...shown.map(cardFor));
  grid.scrollTop = scroll;

  empty.hidden = shown.length > 0;
  tally.textContent = `${plural(shown.length, 'library')} · ${plural(state.libs.reduce((n, l) => n + l.presets.length, 0), 'preset')}`;
  libCount.textContent = String(state.libs.length);
  $('chips').querySelectorAll<HTMLButtonElement>('button').forEach((b) => {
    b.classList.toggle('on', b.dataset.kind === state.kind);
  });
}

function renderRoots(): void {
  rootsList.replaceChildren(...state.roots.map((root) => {
    const li = el('li');
    const count = state.libs.filter((l) => l.source.startsWith(root)).length;
    const name = root.split('/').filter(Boolean).pop() ?? root;
    li.innerHTML = `<span class="folder"></span>
      <span class="path"><b></b><small></small></span>
      <span class="count"></span>`;
    (li.querySelector('b') as HTMLElement).textContent = name;
    (li.querySelector('small') as HTMLElement).textContent = root;
    (li.querySelector('.count') as HTMLElement).textContent = String(count);
    li.title = root;
    return li;
  }));
}

function setBusy(busy: boolean): void {
  state.busy = busy;
  scanTag.classList.toggle('on', busy);
}

// --------------------------------------------------------- generic controls

function buildKnobs(): void {
  const wrap = $('knobs');
  wrap.replaceChildren();
  const sections: Array<[string, string[]]> = [
    ['instrument', ['gain', 'tone', 'reverb']],
    ['envelope', ['attack', 'decay', 'sustain', 'release']],
  ];
  for (const [title, names] of sections) {
    const section = el('div', 'knob-set');
    const head = el('span', 'set-title');
    head.textContent = title;
    section.appendChild(head);
    const row = el('div', 'knob-row');
    for (const id of names) {
      const def = state.params.find((p) => p.id === id);
      if (!def) continue;
      const value = state.values[id] ?? def.def;
      const knob = new Knob(def, value, (v) => {
        state.values[id] = v;
        send({ type: 'param', id, value: v });
      });
      knobs.set(id, knob);
      row.appendChild(knob.el);
    }
    section.appendChild(row);
    wrap.appendChild(section);
  }
}

function syncKnobs(): void {
  for (const [id, knob] of knobs) {
    const v = state.values[id];
    if (v !== undefined) knob.set(v);
  }
}

function useValues(values: Record<string, number>): void {
  state.values = { ...state.values, ...values };
  syncKnobs();
  panel?.refresh();
}

// ------------------------------------------------------------- preset player

let panel: Panel | null = null;

const panelHost: PanelHost = {
  value: (id) => state.values[id],
  clamp(id, value) {
    const def = state.params.find((p) => p.id === id);
    return def ? Math.min(def.max, Math.max(def.min, value)) : value;
  },
  push(id, value) {
    state.values[id] = value;
    send({ type: 'param', id, value });
    knobs.get(id)?.set(value);
  },
  asset: (path) => `/ui/${encodeURI(path)}`,
  bind: (message) => send({ type: 'bind', ...message }),
};

function renderPlayer(msg: LoadedMsg): void {
  const lib = currentLibrary();
  const preset = lib?.presets.find((p) => p.id === msg.id);
  $('playerName').textContent = msg.name;
  $('playerSub').textContent = [lib?.name, preset ? plural(preset.zones, 'zone') : ''].filter(Boolean).join(' · ');
  $('playerBack').textContent = lib ? '‹ ' + (lib.name.length > 26 ? 'Library' : lib.name) : '‹ Library';

  state.ui = msg.ui ?? null;
  app.classList.toggle('has-ui', !!state.ui);
  showView('player');

  if (state.ui) {
    state.uiTab = (state.ui.tabs ?? []).filter(Boolean)[0] ?? '';
    state.uiValues = {};
    buildStage(state.ui);
    keyboard.setTints(state.ui.keyRanges ?? []);
  } else {
    keyboard.setTints([]);
    panel?.dispose();
    panel = null;
    stageWrap.replaceChildren(genericPanel(msg));
    state.uiTab = '';
  }
  renderTabs();
}

function genericPanel(msg: LoadedMsg): HTMLElement {
  const wrap = el('div', 'generic');
  const art = el('div', 'generic-art');
  art.textContent = initials(msg.name);
  art.style.setProperty('--tint', currentLibrary()?.tint || '#2f3a35');
  const body = el('div', 'generic-body');
  const h = el('h3');
  h.textContent = msg.name;
  const p = el('p');
  p.textContent = 'This preset has no custom panel. Its level, space and envelope controls are in the bar below.';
  const meta = el('span');
  meta.textContent = `${plural(msg.zones ?? 0, 'zone')} · ${plural(msg.groups ?? 0, 'group')}`;
  body.append(h, p, meta);
  wrap.append(art, body);
  return wrap;
}

function renderTabs(): void {
  const holder = $('playerTabs');
  holder.replaceChildren();
  const tabs = (state.ui?.tabs ?? []).filter(Boolean);
  if (!state.ui || tabs.length < 2) return;
  for (const name of tabs) {
    const b = el('button', 'tab');
    b.textContent = name;
    b.classList.toggle('on', name === state.uiTab);
    b.addEventListener('click', () => {
      state.uiTab = name;
      renderTabs();
      if (state.ui) buildStage(state.ui);
    });
    holder.appendChild(b);
  }
}

function buildStage(ui: UiSpec): void {
  panel?.dispose();
  panel = buildPanel(ui, state.uiTab, panelHost, state.uiValues);
  const frame = el('div', 'stage-frame');
  frame.appendChild(panel.root);
  stageWrap.replaceChildren(frame);
  requestAnimationFrame(fitStage);
}

function fitStage(): void {
  const frame = stageWrap.querySelector<HTMLElement>('.stage-frame');
  if (!panel || !frame) return;
  const pad = 28;
  const aw = Math.max(120, stageWrap.clientWidth - pad);
  const ah = Math.max(90, stageWrap.clientHeight - pad);
  const bare = new URLSearchParams(location.search).has('bare');
  const scale = bare ? 1 : Math.min(aw / panel.width, ah / panel.height);
  panel.root.style.transform = `scale(${scale})`;
  frame.style.width = `${panel.width * scale}px`;
  frame.style.height = `${panel.height * scale}px`;
}

// ---------------------------------------------------------------- messages

function onIndex(msg: any): void {
  state.libs = (msg.libs ?? []) as LibEntry[];
  state.roots = (msg.roots ?? []) as string[];
  setBusy(Boolean(msg.busy));
  renderRoots();

  if (state.view === 'library') {
    const lib = currentLibrary();
    if (!lib) showShelf();
    else renderLibrary();
  } else if (state.view === 'shelf') {
    renderGrid();
  }

  if (state.selected) {
    const owner = state.libs.find((l) => l.presets.some((p) => p.id === state.selected));
    if (owner && state.openLib !== owner.id && state.view === 'player') state.openLib = owner.id;
  }
}

function onLoaded(msg: LoadedMsg): void {
  state.loading = '';
  setLoading(null);
  if (msg.error) {
    toast(msg.error, true);
    if (state.view === 'library') renderPresets();
    renderGrid();
    return;
  }

  state.selected = msg.id;
  const owner = state.libs.find((l) => l.presets.some((p) => p.id === msg.id));
  if (owner) state.openLib = owner.id;
  else if (state.view === 'library') {
    // restored from a saved session: match by preset path
    const match = state.libs.find((l) => l.presets.some((p) => p.preset === msg.preset));
    if (match) state.openLib = match.id;
  }

  nowName.textContent = msg.name;
  nowSub.textContent = `${plural(msg.zones ?? 0, 'zone')} · ${plural(msg.groups ?? 0, 'group')}`;

  if (msg.values) useValues(msg.values);
  for (const [id, value] of Object.entries({
    attack: msg.attack, decay: msg.decay, sustain: msg.sustain,
    release: msg.release, gain: msg.gainDb,
  })) {
    if (typeof value !== 'number') continue;
    state.values[id] = value;
    knobs.get(id)?.set(value);
  }
  syncKnobs();

  renderPlayer(msg);
}

// ---------------------------------------------------------------- wiring

app.addEventListener('click', (e) => {
  const card = (e.target as HTMLElement).closest('.card') as HTMLElement | null;
  if (card) {
    const lib = state.libs.find((l) => l.id === card.dataset.id);
    if (lib) openLibrary(lib);
    return;
  }

  const row = (e.target as HTMLElement).closest('.preset') as HTMLElement | null;
  if (row) {
    const lib = currentLibrary();
    const preset = lib?.presets.find((p) => p.id === row.dataset.id);
    if (!preset) return;
    if (!preset.ok) {
      toast(preset.error || 'This preset could not be read', true);
      return;
    }
    state.loading = preset.id;
    setLoading(preset.name);
    renderPresets();
    send({ type: 'load', id: preset.id });
    if (!state.live) {
      window.setTimeout(() => {
        onLoaded({
          id: preset.id,
          name: preset.name,
          preset: preset.preset,
          zones: preset.zones,
          groups: preset.groups,
          ui: preset.name.toLowerCase().includes('glass') ? mockUi : undefined,
          values: { ...state.values },
        });
      }, 220);
    }
  }
});

$('chips').addEventListener('click', (e) => {
  const button = (e.target as HTMLElement).closest('button') as HTMLButtonElement | null;
  if (!button) return;
  state.kind = (button.dataset.kind as Kind) ?? 'all';
  renderGrid();
});

search.addEventListener('input', () => {
  state.filter = search.value;
  if (state.view === 'library') renderPresets();
  else if (state.view === 'shelf') renderGrid();
});
search.addEventListener('keydown', (e) => {
  if (e.key === 'Escape') {
    search.value = '';
    state.filter = '';
    if (state.view === 'library') renderPresets();
    else renderGrid();
    search.blur();
  }
});

$('rescan').addEventListener('click', () => {
  setBusy(true);
  send({ type: 'scan' });
});
$('addroot').addEventListener('click', () => send({ type: 'addRoot' }));
$('emptyAdd').addEventListener('click', () => send({ type: 'addRoot' }));
$('home').addEventListener('click', showShelf);
$('libBack').addEventListener('click', showShelf);
$('playerBack').addEventListener('click', () => {
  showView('library');
  renderLibrary();
});

window.addEventListener('keydown', (e) => {
  const tag = (e.target as HTMLElement | null)?.tagName;
  if (tag === 'INPUT' || tag === 'TEXTAREA') return;
  if (e.key === '/') {
    e.preventDefault();
    search.focus();
  }
  if (e.key === 'Escape') {
    if (state.view === 'player') { showView('library'); renderLibrary(); }
    else if (state.view === 'library') showShelf();
  }
});

window.addEventListener('trove-sustain', (e) => {
  const down = Boolean((e as CustomEvent).detail);
  $('hold').classList.toggle('on', down);
  send({ type: 'sustain', down });
});

window.addEventListener('resize', fitStage);

const keyboard = new Keyboard($('keys'), (note, velocity, on) => {
  if (!state.live) return;
  send(on ? { type: 'noteOn', note, velocity } : { type: 'noteOff', note });
});

// ---------------------------------------------------------------- engine link

if (inPlugin()) {
  onMessage((msg) => {
    switch (msg.type) {
      case 'hello':
        state.live = true;
        state.params = (msg.params ?? []) as ParamDef[];
        buildKnobs();
        useValues((msg.values ?? {}) as Record<string, number>);
        break;
      case 'index':
        onIndex(msg);
        break;
      case 'loaded':
        onLoaded(msg as LoadedMsg);
        break;
      case 'params':
        useValues((msg.values ?? {}) as Record<string, number>);
        break;
      case 'held':
        keyboard.setHeld((msg.notes ?? []) as number[]);
        break;
      case 'stats':
        state.voices = Number(msg.voices ?? 0);
        state.peak = Math.max(Number(msg.peak ?? 0), state.peak * 0.86);
        meterFill.style.width = `${Math.min(100, state.peak * 118)}%`;
        voiceCount.textContent = `${state.voices} voice${state.voices === 1 ? '' : 's'}`;
        $('playerVoices').textContent = state.voices ? `${plural(state.voices, 'voice')}` : '';
        break;
    }
  });
  send({ type: 'ready' });
} else {
  state.live = false;
  state.params = mockParams;
  state.libs = mockLibs;
  state.roots = mockRoots;
  state.values = Object.fromEntries(mockParams.map((p) => [p.id, p.def]));
  buildKnobs();
  renderRoots();
  renderGrid();
  $('demoNote').textContent = 'browser preview — demo data, no audio';
  showShelf();
}

// Browser preview only: ?panel=/path/to/Preset.dspreset shows that preset's own
// panel, parsed by the real reader (see vite.config.ts).
if (!inPlugin()) {
  const preset = new URLSearchParams(location.search).get('panel');
  if (preset) {
    // Synchronous on purpose: a screenshot taken on `load` then includes the panel.
    const req = new XMLHttpRequest();
    req.open('GET', `/__panel?preset=${encodeURIComponent(preset)}`, false);
    req.send();
    if (new URLSearchParams(location.search).has('bare')) document.documentElement.classList.add('bare');
    const ui = JSON.parse(req.responseText) as UiSpec;
    const name = preset.split('/').pop()?.replace(/\.dspreset$/i, '') ?? 'Preset';
    onLoaded({ id: 'preview', name, preset, zones: 0, groups: 0, ui, values: { ...state.values } });
    const shown = panel as Panel | null;
    if (document.documentElement.classList.contains('bare') && shown) {
      // just the panel, 1:1 at the top-left, to lay next to a Decent Sampler capture
      const frame = stageWrap.querySelector<HTMLElement>('.stage-frame');
      if (frame) {
        frame.style.cssText = `position:absolute;left:0;top:0;width:${shown.width}px;height:${shown.height}px;overflow:hidden`;
        shown.root.style.transform = 'none';
        document.body.replaceChildren(frame);
      }
    }
  }
}
