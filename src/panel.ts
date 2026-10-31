// A library's own panel, drawn the way Decent Sampler draws it: controls sit
// at the pixel positions in <ui>, the background is the artist's image at its
// native size, and knobs are plain rings unless the preset ships a filmstrip.
// Nothing here is Trove styling on purpose; the library is the interface.
import type { UiBinding, UiControl, UiSpec } from './bridge';
import {
  engineMessages, fixedValue, hostTarget, nativeOf, translate, uiTarget, untranslate,
  type EngineMessage, type HostId, type Native,
} from './bindings';

export interface PanelHost {
  value(id: string): number | undefined;
  clamp(id: string, value: number): number;
  push(id: string, value: number): void;
  asset(path: string): string;
  bind(message: EngineMessage): void;
}

export interface Panel {
  root: HTMLElement;
  width: number;     // the part Decent Sampler leaves visible
  height: number;
  refresh(): void;
  dispose(): void;
}

interface BindingMap {
  id: HostId;
  toHost(value: number): number;
  fromHost(host: number): number | null;
}

// What another control's binding can reach on a control: its value, whether it
// shows, how opaque it is, and which frame of a strip it displays.
interface Handle {
  setValue(v: number): void;
  setVisible(on: boolean): void;
  setOpacity(o: number): void;
  setFrame(f: number): void;
}

// The bindings that steer Trove's own knobs (envelope, master level).
function bindingsFor(control: UiControl, host: PanelHost): BindingMap[] {
  const maps: BindingMap[] = [];
  for (const b of control.bindings ?? []) {
    const target = hostTarget(b);
    if (!target) continue;
    maps.push({
      id: target.id,
      toHost: (v) => host.clamp(target.id, target.toHost(translate(b, control, v))),
      fromHost: (h) => untranslate(b, control, target.fromHost(h)),
    });
  }
  return maps;
}

// ------------------------------------------------------------------ helpers

const FONT = '"Helvetica Neue", Helvetica, Arial, "Liberation Sans", sans-serif';

// Decent Sampler's window is exactly <ui width height>. Its toolbar covers the
// top TOOLBAR px and its keyboard the bottom KEYBOARD px; the background is
// painted under both, but every control is laid out below the toolbar. Measured
// against Decent Sampler itself (ASIMOV, Casio DG-20).
export const TOOLBAR = 50;
export const KEYBOARD = 112;
// How much of the top we crop. Slightly less than TOOLBAR: a library's first
// row of controls (y=0) sits a little below the edge of Decent Sampler's view,
// so cropping the full 50 clips its labels (Basic Piano).
const CROP_TOP = 40;
// JUCE font heights count ascent plus descent, so Decent Sampler's textSize
// draws about 0.76x as large as the same number of CSS pixels.
const FONT_SCALE = 0.76;
function fontPx(c: UiControl): number {
  // Decent Sampler stops growing a label at about 16px: Zither's textSize 30
  // draws the same size as its textSize 18.
  return Math.min(16, (c.textSize || 14) * FONT_SCALE);
}
let measurer: CanvasRenderingContext2D | null = null;

// Decent Sampler squeezes a label that is wider than its box instead of
// letting it spill, so "trem sub" at textSize 30 in a 70px knob draws at about
// 14px. Shrink to fit the box less a few px of padding.
function fitPx(c: UiControl, text: string): number {
  const px = fontPx(c);
  if (!c.w) return px;
  measurer ??= document.createElement('canvas').getContext('2d');
  if (!measurer) return px;
  measurer.font = `${px}px ${FONT}`;
  const width = measurer.measureText(text).width;
  const room = c.w - 10;
  return width > room ? Math.max(6, px * (room / width)) : px;
}
// Decent Sampler's defaults: the value arc is the dark grey, the track the light one.
const DEFAULT_FG = '#7f7f7f';
const DEFAULT_BG = '#cdcdcd';

function div(clazz: string): HTMLDivElement {
  const node = document.createElement('div');
  node.className = clazz;
  return node;
}

function place(node: HTMLElement, c: UiControl): void {
  node.style.left = `${c.x}px`;
  node.style.top = `${c.y + TOOLBAR}px`;
  node.style.width = `${Math.max(1, c.w)}px`;
  node.style.height = `${Math.max(1, c.h)}px`;
  if ((c.opacity ?? 1) < 1) node.style.opacity = String(c.opacity);
  if (c.visible === false) node.style.display = 'none';
}

function normOf(c: UiControl, v: number): number {
  return Math.min(1, Math.max(0, (v - c.min) / Math.max(1e-9, c.max - c.min)));
}

function formatValue(c: UiControl, v: number): string {
  const kind = (c.valueType ?? '').toLowerCase();
  if (kind === 'percent') return `${Math.round(c.max <= 1 ? v * 100 : v)}%`;
  if (kind === 'integer') return String(Math.round(v));
  const a = Math.abs(v);
  if (a >= 1000) return String(Math.round(v));
  if (a >= 100) return v.toFixed(1);
  return v.toFixed(2);
}

function labelNode(c: UiControl): HTMLElement | null {
  if (c.showLabel === false || !c.text) return null;
  const node = div('p-caption');
  node.textContent = c.text;
  node.style.fontSize = `${fitPx(c, c.text)}px`;
  node.style.color = c.textColor || '#fff';
  return node;
}

// Drag to change, shift for fine control, wheel to nudge, double click to reset.
function makeDraggable(node: HTMLElement, c: UiControl, get: () => number, set: (v: number) => void, reset: number): void {
  const vertical = c.dragVertical !== false;
  const pixels = c.dragPixels && c.dragPixels > 0 ? c.dragPixels : 200;
  let origin = 0;
  let startNorm = 0;
  if (c.type !== 'bar') {   // bars jump to the pointer instead
    node.addEventListener('pointerdown', (e) => {
      e.preventDefault();
      origin = vertical ? e.clientY : e.clientX;
      startNorm = normOf(c, get());
      node.setPointerCapture(e.pointerId);
      node.classList.add('dragging');
    });
    node.addEventListener('pointermove', (e) => {
      if (!node.hasPointerCapture(e.pointerId)) return;
      const travelled = vertical ? origin - e.clientY : e.clientX - origin;
      const norm = Math.min(1, Math.max(0, startNorm + travelled / (e.shiftKey ? pixels * 5 : pixels)));
      set(c.min + norm * (c.max - c.min));
    });
    const end = (e: PointerEvent): void => {
      if (node.hasPointerCapture(e.pointerId)) node.releasePointerCapture(e.pointerId);
      node.classList.remove('dragging');
    };
    node.addEventListener('pointerup', end);
    node.addEventListener('pointercancel', end);
  }
  node.addEventListener('dblclick', () => set(reset));
  node.addEventListener('wheel', (e) => {
    e.preventDefault();
    const norm = Math.min(1, Math.max(0, normOf(c, get()) - Math.sign(e.deltaY) * 0.02));
    set(c.min + norm * (c.max - c.min));
  }, { passive: false });
}

// ------------------------------------------------------------------- panel

export function buildPanel(ui: UiSpec, tab: string, host: PanelHost, saved: Record<string, number>): Panel {
  // Show what Decent Sampler shows: the band between its toolbar and keyboard.
  const visible = Math.max(80, ui.height - CROP_TOP - KEYBOARD);
  const view = div('p-stage');
  view.style.width = `${ui.width}px`;
  view.style.height = `${visible}px`;
  const root = div('p-canvas');
  root.style.width = `${ui.width}px`;
  root.style.height = `${ui.height}px`;
  root.style.top = `${-CROP_TOP}px`;
  root.style.fontFamily = FONT;
  view.appendChild(root);
  if (ui.bgColor) root.style.backgroundColor = ui.bgColor;
  if (ui.bgImage) {
    if (ui.bgTopLeft) {
      // Native size, unless the artwork is a hi-res export (2x, 4x) of the panel.
      const bg = document.createElement('img');
      bg.className = 'p-bg';
      bg.alt = '';
      bg.src = host.asset(ui.bgImage);
      bg.addEventListener('load', () => {
        const big = bg.naturalWidth >= ui.width * 1.5 && bg.naturalHeight >= ui.height * 1.5;
        if (big) { bg.style.width = `${ui.width}px`; bg.style.height = `${ui.height}px`; }
      });
      root.appendChild(bg);
    } else {
      root.style.backgroundImage = `url("${host.asset(ui.bgImage)}")`;
      root.style.backgroundRepeat = 'no-repeat';
      root.style.backgroundSize = '100% 100%';
    }
  }

  const refreshers: Array<() => void> = [];
  const timers: number[] = [];
  const tabs = (ui.tabs ?? []).filter(Boolean);

  const byTag = new Map<string, Handle[]>();
  const byIndex = new Map<number, Handle>();
  const starts: Array<() => void> = [];   // initial bindings, once every control exists

  function register(c: UiControl, node: HTMLElement, extra: Partial<Handle> = {}): Handle {
    const handle: Handle = {
      setValue: () => {},
      setVisible: (on) => { node.style.display = on ? '' : 'none'; },
      setOpacity: (o) => { node.style.opacity = String(o); },
      setFrame: () => {},
      ...extra,
    };
    for (const t of (c.tags ?? '').split(',').map((x) => x.trim()).filter(Boolean)) {
      const list = byTag.get(t) ?? [];
      list.push(handle);
      byTag.set(t, list);
    }
    if (c.uiIndex !== undefined) byIndex.set(c.uiIndex, handle);
    return handle;
  }

  // Send a binding wherever its parameter lives: another control on this
  // panel, or the sound engine. (Trove's own knobs are the control's business.)
  function deliver(b: UiBinding, native: Native, position?: number): void {
    const target = uiTarget(b);
    if (target) {
      const handles = target.tags.length
        ? target.tags.flatMap((t) => byTag.get(t) ?? [])
        : [byIndex.get(target.index ?? -1)].filter((h): h is Handle => !!h);
      const n = typeof native === 'boolean' ? (native ? 1 : 0) : Number(native);
      for (const h of handles) {
        if (target.parameter === 'VALUE') h.setValue(n);
        else if (target.parameter === 'VISIBLE') h.setVisible(typeof native === 'boolean' ? native : n >= 0.5);
        else if (target.parameter === 'OPACITY') h.setOpacity(n);
        else h.setFrame(n);
      }
      return;
    }
    if (hostTarget(b)) return;
    for (const message of engineMessages(b, native, position)) host.bind(message);
  }

  const startValue = (c: UiControl, key: string): number => Math.min(c.max, Math.max(c.min, saved[key] ?? c.value));

  ui.controls.forEach((c, index) => {
    const key = String(index);
    if (tabs.length > 1 && c.tab && c.tab !== tab) {
      // Not on screen, but what it does to the sound still counts.
      starts.push(() => {
        if (c.type === 'knob' || c.type === 'bar' || c.type === 'skin') {
          const v = startValue(c, key);
          for (const b of c.bindings ?? []) if (b.triggerOnLoad !== false) deliver(b, nativeOf(b, c, v), normOf(c, v));
        } else if (c.type === 'menu') {
          const i = saved[`menu:${c.tab}:${c.x},${c.y}`] ?? Math.max(0, Math.round(c.value) - 1);
          for (const b of c.optionBindings?.[i] ?? []) deliver(b, fixedValue(b));
        } else if (c.type === 'button') {
          const i = Math.round(saved[key] ?? c.value ?? 0);
          for (const b of c.states?.[i]?.bindings ?? []) if (b.triggerOnLoad !== false) deliver(b, fixedValue(b));
        }
      });
      return;
    }
    const node = build(c, key);
    if (node) root.appendChild(node);
  });
  starts.forEach((fn) => fn());

  function build(c: UiControl, key: string): HTMLElement | null {
    switch (c.type) {
      case 'knob': return valued(c, key, knobNode);
      case 'bar': return valued(c, key, barNode);
      case 'skin': return valued(c, key, skinNode);
      case 'button': return buttonNode(c, key);
      case 'menu': return menuNode(c);
      case 'label': return textNode(c);
      case 'image': return imageNode(c);
      case 'strip': return stripNode(c);
      default: return null;
    }
  }

  // The shared part of every value control: start value, host binding, and
  // keeping the host knobs in step.
  function valued(
    c: UiControl, key: string,
    make: (c: UiControl, node: HTMLElement, label: HTMLElement | null, set: (v: number) => void) => (v: number) => void,
  ): HTMLElement {
    const node = div('p-control');
    place(node, c);
    const maps = bindingsFor(c, host);
    const engine = (c.bindings ?? []).filter((b) => !hostTarget(b));
    const primary = maps.find((m) => m.fromHost(host.value(m.id) ?? 0) !== null) ?? null;
    const fromHost = primary && typeof host.value(primary.id) === 'number'
      ? primary.fromHost(host.value(primary.id)!) : null;
    // The preset's own value wins at load, as in Decent Sampler, and is then
    // sent on to the envelope and level knobs it is bound to.
    void fromHost;
    let value = Math.min(c.max, Math.max(c.min, saved[key] ?? c.value));
    saved[key] = value;

    let paint: (v: number) => void = () => {};
    const set = (v: number, notify = true): void => {
      value = Math.min(c.max, Math.max(c.min, v));
      saved[key] = value;
      paint(value);
      if (!notify) return;
      for (const m of maps) host.push(m.id, m.toHost(value));
      for (const b of engine) deliver(b, nativeOf(b, c, value), normOf(c, value));
    };
    register(c, node, { setValue: (v) => set(v) });
    starts.push(() => {
      for (const m of maps) host.push(m.id, m.toHost(value));
      for (const b of engine) if (b.triggerOnLoad !== false) deliver(b, nativeOf(b, c, value), normOf(c, value));
    });

    const label = labelNode(c);
    paint = make(c, node, label, (v) => set(v));
    const reset = c.defaultValue ?? c.value;
    makeDraggable(node, c, () => value, (v) => set(v), reset);
    paint(value);

    if (primary) {
      refreshers.push(() => {
        const h = host.value(primary.id);
        if (typeof h !== 'number') return;
        const native = primary.fromHost(h);
        if (native === null) return;
        const v = Math.min(c.max, Math.max(c.min, native));
        if (Math.abs(v - value) > 1e-4 * Math.max(1, Math.abs(c.max - c.min))) set(v, false);
      });
    }
    return node;
  }

  // JUCE's stock rotary, which is what Decent Sampler draws: a label row
  // textSize tall at the top, then the ring in the rest of the box inset by
  // 10px, a 288 degree sweep, stroke min(8, r/2), round caps, no thumb and no
  // value text (it shows only while dragging).
  function knobNode(c: UiControl, node: HTMLElement, label: HTMLElement | null): (v: number) => void {
    node.classList.add('p-knob');
    // Measured against Decent Sampler on eight libraries: the dial is about
    // 64% of the smaller side, its centre sits at 25px + 34% of the height
    // (or mid-box when there is no label), and the label is centred 0.7 x
    // textSize down, never more than 15. Default-sized text gets a bigger dial.
    const hasLabel = c.showLabel !== false;
    const ts = c.textSize || 0;
    const boxMin = Math.min(c.w, c.h);
    const dia = ts > 0 ? 0.64 * boxMin : Math.max(0.64 * boxMin, boxMin - 30);
    const radius = Math.max(4, dia / 2);
    const labelMid = Math.min(15, ts > 0 ? 0.7 * ts : 11);
    const rowH = 2 * labelMid;
    const lineW = Math.min(8, radius * 0.5);
    const r = radius - lineW / 2;
    const cx = c.w / 2;
    const cy = hasLabel ? 25 + 0.34 * c.h : c.h / 2;
    const start = 1.2 * Math.PI;
    const end = 2.8 * Math.PI;

    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    svg.setAttribute('class', 'p-ring');
    svg.setAttribute('width', String(c.w));
    svg.setAttribute('height', String(c.h));
    svg.setAttribute('viewBox', `0 0 ${c.w} ${c.h}`);
    const point = (a: number): string =>
      `${(cx + r * Math.sin(a)).toFixed(2)} ${(cy - r * Math.cos(a)).toFixed(2)}`;
    const arcPath = (to: number): string =>
      `M ${point(start)} A ${r} ${r} 0 ${to - start > Math.PI ? 1 : 0} 1 ${point(to)}`;
    const stroke = (path: SVGPathElement, colour: string): void => {
      path.setAttribute('fill', 'none');
      path.setAttribute('stroke', colour);
      path.setAttribute('stroke-width', String(lineW));
      path.setAttribute('stroke-linecap', 'round');
    };
    const track = document.createElementNS('http://www.w3.org/2000/svg', 'path');
    stroke(track, c.bg || DEFAULT_BG);
    track.setAttribute('d', arcPath(end - 1e-4));
    const arc = document.createElementNS('http://www.w3.org/2000/svg', 'path');
    stroke(arc, c.fg || DEFAULT_FG);
    svg.append(track, arc);
    node.appendChild(svg);

    if (label) {
      label.classList.add('row');
      label.style.height = `${rowH}px`;
      label.style.lineHeight = `${rowH}px`;
      node.appendChild(label);
    }
    const readout = div('p-value');
    readout.style.color = c.textColor || '#fff';
    readout.style.top = `${cy}px`;
    readout.style.fontSize = `${Math.max(9, Math.min(13, r * 0.5))}px`;
    node.appendChild(readout);

    return (v: number): void => {
      const norm = normOf(c, v);
      arc.style.display = norm <= 0 ? 'none' : '';
      arc.setAttribute('d', arcPath(start + Math.max(1e-4, norm) * (end - start)));
      readout.textContent = formatValue(c, v);
    };
  }

  // Two looks. A <control> with linear_bar is JUCE's LinearBar: a flat
  // rectangle filled up to the value, no label. A <labeled-knob> given a
  // linear_horizontal/vertical style is JUCE's linear slider under a label
  // row: a thin rounded track, the value part in the foreground colour, no handle.
  function barNode(c: UiControl, node: HTMLElement, label: HTMLElement | null, set: (v: number) => void): (v: number) => void {
    node.classList.add('p-bar');
    const slim = /^linear_(horizontal|vertical)$/i.test(c.style ?? '');
    let pick: (e: PointerEvent) => void;
    let paint: (v: number) => void;

    if (!slim) {
      // Decent Sampler draws only a thin grey outline; the inside stays clear.
      const track = div('p-bar-track');
      const fill = div('p-bar-fill');
      fill.style.background = c.fg || DEFAULT_FG;
      track.appendChild(fill);
      node.appendChild(track);
      pick = (e) => {
        const rect = track.getBoundingClientRect();
        const norm = c.vertical
          ? 1 - (e.clientY - rect.top) / Math.max(1, rect.height)
          : (e.clientX - rect.left) / Math.max(1, rect.width);
        set(c.min + Math.min(1, Math.max(0, norm)) * (c.max - c.min));
      };
      paint = (v) => {
        const pct = normOf(c, v) * 100;
        if (c.vertical) { fill.style.height = `${pct}%`; fill.style.width = '100%'; }
        else { fill.style.width = `${pct}%`; fill.style.height = '100%'; }
      };
    } else {
      const rowH = c.showLabel === false ? 0 : (c.textSize || 10);
      if (label) {
        label.classList.add('row');
        label.style.height = `${rowH}px`;
        label.style.lineHeight = `${rowH}px`;
        node.appendChild(label);
      }
      const areaW = c.w;
      const areaH = c.h - rowH;
      const thumb = Math.min(12, c.vertical ? areaW / 2 : areaH / 2);
      const trackW = Math.min(6, c.vertical ? areaW * 0.25 : areaH * 0.25);
      // the track runs between the thumb's two extreme centres
      const x0 = c.vertical ? areaW / 2 : thumb;
      const x1 = c.vertical ? areaW / 2 : areaW - thumb;
      const y0 = c.vertical ? rowH + areaH - thumb : rowH + areaH / 2;
      const y1 = c.vertical ? rowH + thumb : rowH + areaH / 2;

      const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
      svg.setAttribute('class', 'p-ring');
      svg.setAttribute('width', String(c.w));
      svg.setAttribute('height', String(c.h));
      const line = (colour: string): SVGLineElement => {
        const l = document.createElementNS('http://www.w3.org/2000/svg', 'line');
        l.setAttribute('x1', String(x0));
        l.setAttribute('y1', String(y0));
        l.setAttribute('stroke', colour);
        l.setAttribute('stroke-width', String(trackW));
        l.setAttribute('stroke-linecap', 'round');
        return l;
      };
      const back = line(c.bg || DEFAULT_BG);
      back.setAttribute('x2', String(x1));
      back.setAttribute('y2', String(y1));
      const front = line(c.fg || DEFAULT_FG);
      const dot = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
      dot.setAttribute('r', String(thumb / 2));
      dot.setAttribute('fill', c.textColor || '#fff');
      // Decent Sampler draws no handle, just the track and its fill.
      svg.append(back, front);
      node.appendChild(svg);

      pick = (e) => {
        const rect = node.getBoundingClientRect();
        const scale = rect.width / Math.max(1, c.w);
        const px = (e.clientX - rect.left) / scale;
        const py = (e.clientY - rect.top) / scale;
        const norm = c.vertical ? (y0 - py) / Math.max(1, y0 - y1) : (px - x0) / Math.max(1, x1 - x0);
        set(c.min + Math.min(1, Math.max(0, norm)) * (c.max - c.min));
      };
      paint = (v) => {
        const n = normOf(c, v);
        const x = x0 + (x1 - x0) * n;
        const y = y0 + (y1 - y0) * n;
        front.setAttribute('x2', String(x));
        front.setAttribute('y2', String(y));
        dot.setAttribute('cx', String(x));
        dot.setAttribute('cy', String(y));
      };
    }

    node.addEventListener('pointerdown', (e) => { node.setPointerCapture(e.pointerId); pick(e); }, true);
    node.addEventListener('pointermove', (e) => { if (node.hasPointerCapture(e.pointerId)) pick(e); }, true);
    return paint;
  }

  // A filmstrip skin: the frame is picked from the value, nothing is drawn on top.
  function skinNode(c: UiControl, node: HTMLElement, label: HTMLElement | null): (v: number) => void {
    node.classList.add('p-skin');
    const face = div('p-skin-face');
    face.style.backgroundImage = `url("${host.asset(c.source ?? '')}")`;
    const frames = Math.max(1, c.frames ?? 1);
    face.style.backgroundSize = c.vertical ? `100% ${frames * 100}%` : `${frames * 100}% 100%`;
    node.appendChild(face);
    // Decent Sampler shows no caption on a skinned control. A rotary-sized
    // skin is drawn inside the box less 10px all round, keeping the frame's
    // proportions; a thin slider simply fills its box.
    if (c.labeled && Math.min(c.w, c.h) >= 40) {
      const probe = new Image();
      probe.onload = () => {
        const fw = c.vertical ? probe.naturalWidth : probe.naturalWidth / frames;
        const fh = c.vertical ? probe.naturalHeight / frames : probe.naturalHeight;
        if (!fw || !fh) return;
        const scale = Math.min((c.w - 20) / fw, (c.h - 20) / fh);
        face.style.position = 'absolute';
        face.style.width = `${fw * scale}px`;
        face.style.height = `${fh * scale}px`;
        face.style.left = `${(c.w - fw * scale) / 2}px`;
        face.style.top = `${(c.h - fh * scale) / 2}px`;
      };
      probe.src = host.asset(c.source ?? '');
    }
    return (v: number): void => {
      const frame = frames > 1 ? Math.round(normOf(c, v) * (frames - 1)) / (frames - 1) * 100 : 0;
      face.style.backgroundPosition = c.vertical ? `0 ${frame}%` : `${frame}% 0`;
    };
  }

  // Each press steps to the next state, and a state does something when it
  // becomes current: switch groups, set another control, hide a message.
  function buttonNode(c: UiControl, key: string): HTMLElement {
    const node = div('p-button');
    place(node, c);
    const states = c.states ?? [];
    if (!states.length) {
      node.classList.add('plain');
      node.textContent = c.text ?? '';
      node.style.color = c.textColor || '#fff';
      node.style.fontSize = `${fontPx(c)}px`;
      register(c, node);
      return node;
    }
    let index = Math.min(states.length - 1, Math.max(0, Math.round(saved[key] ?? c.value ?? 0)));
    let pressed = false;
    let hover = false;
    const paint = (): void => {
      const st = states[index];
      const src = pressed ? st.click || st.main : hover ? st.hover || st.main : st.main;
      node.style.backgroundImage = src ? `url("${host.asset(src)}")` : 'none';
    };
    const enter = (next: number, initial = false): void => {
      if (!initial && next === index) return;
      index = next;
      saved[key] = index;
      paint();
      for (const b of states[index].bindings ?? []) {
        if (initial && b.triggerOnLoad === false) continue;
        deliver(b, fixedValue(b));
      }
    };
    node.addEventListener('pointerenter', () => { hover = true; paint(); });
    node.addEventListener('pointerleave', () => { hover = false; pressed = false; paint(); });
    node.addEventListener('pointerdown', () => { pressed = true; paint(); });
    node.addEventListener('pointerup', () => {
      if (!pressed) return;
      pressed = false;
      enter((index + 1) % states.length);
    });
    register(c, node, { setValue: (v) => enter(Math.min(states.length - 1, Math.max(0, Math.round(v)))) });
    paint();
    starts.push(() => enter(index, true));
    return node;
  }

  function menuNode(c: UiControl): HTMLElement {
    const node = document.createElement('select');
    node.className = 'p-menu';
    place(node, c);
    const options = c.options ?? [];
    // Narrow menus (Global Swarm's 50px On/Off) need less padding, and a long
    // entry shrinks to fit rather than being cut off behind the chevron.
    const padL = c.w < 80 ? 6 : 10;
    const padR = c.w < 80 ? 17 : 24;
    node.style.padding = `0 ${padR}px 0 ${padL}px`;
    node.style.backgroundPosition = `right ${c.w < 80 ? 5 : 9}px center`;
    const basePx = Math.min(15, Math.max(10, c.h * 0.45));
    let menuPx = basePx;
    measurer ??= document.createElement('canvas').getContext('2d');
    if (measurer) {
      measurer.font = `700 ${basePx}px ${FONT}`;
      const widest = Math.max(0, ...options.map((o) => measurer!.measureText(o).width));
      const room = c.w - padL - padR - 2;
      if (widest > room) menuPx = Math.max(8, basePx * (room / widest));
    }
    node.style.fontSize = `${menuPx}px`;
    if (c.placeholder) {
      const hint = document.createElement('option');
      hint.value = '';
      hint.textContent = c.placeholder;
      hint.disabled = true;
      node.appendChild(hint);
    }
    options.forEach((name, i) => {
      const o = document.createElement('option');
      o.value = String(i);
      o.textContent = name;
      node.appendChild(o);
    });
    // A menu's value is 1-based. Its entries switch groups on and off, so the
    // chosen one has to be applied now, not only when it is changed.
    const key = `menu:${c.tab}:${c.x},${c.y}`;
    const apply = (i: number): void => {
      for (const b of c.optionBindings?.[i] ?? []) deliver(b, fixedValue(b));
    };
    const fallback = Math.min(options.length - 1, Math.max(0, Math.round(c.value) - 1));
    const start = saved[key] ?? fallback;
    node.value = start >= 0 && start < options.length ? String(start) : '';
    if (options.length > 0) starts.push(() => apply(start));
    node.addEventListener('change', () => {
      const i = Number(node.value);
      saved[key] = i;
      apply(i);
    });
    register(c, node, {
      setValue: (v) => {
        const i = Math.min(options.length - 1, Math.max(0, Math.round(v)));
        node.value = String(i);
        saved[key] = i;
        apply(i);
      },
    });
    return node;
  }

  function textNode(c: UiControl): HTMLElement {
    const node = div('p-label');
    place(node, c);
    node.textContent = c.text ?? '';
    node.style.color = c.textColor || '#fff';
    node.style.fontSize = `${fontPx(c)}px`;
    if (c.align) node.style.justifyContent = c.align === 'right' ? 'flex-end' : c.align === 'centre' || c.align === 'center' ? 'center' : 'flex-start';
    register(c, node);
    return node;
  }

  function imageNode(c: UiControl): HTMLElement | null {
    if (!c.source) return null;
    const node = document.createElement('img');
    node.className = 'p-image';
    place(node, c);
    node.src = host.asset(c.source);
    node.alt = '';
    node.style.objectFit = c.stretch === false ? 'contain' : 'fill';
    register(c, node);
    return node;
  }

  // multiFrameImage: a strip of frames, played in a loop when the preset asks.
  function stripNode(c: UiControl): HTMLElement | null {
    if (!c.source) return null;
    const node = div('p-strip');
    place(node, c);
    node.style.backgroundImage = `url("${host.asset(c.source)}")`;
    const frames = Math.max(1, c.frames ?? 1);
    node.style.backgroundSize = c.vertical ? `100% ${frames * 100}%` : `${frames * 100}% 100%`;
    const show = (f: number): void => {
      const pos = frames > 1 ? (f / (frames - 1)) * 100 : 0;
      node.style.backgroundPosition = c.vertical ? `0 ${pos}%` : `${pos}% 0`;
    };
    show(0);
    register(c, node, { setFrame: (f) => show(Math.min(frames - 1, Math.max(0, Math.round(f)))) });
    const rate = c.frameRate ?? 0;
    if (frames > 1 && rate > 0) {
      let f = 0;
      timers.push(window.setInterval(() => { f = (f + 1) % frames; show(f); }, 1000 / rate));
    }
    return node;
  }

  return {
    root: view,
    width: ui.width,
    height: visible,
    refresh: () => refreshers.forEach((fn) => fn()),
    dispose: () => timers.forEach((t) => window.clearInterval(t)),
  };
}
