// A rotary control: drag vertically (shift for fine), wheel, double-click to
// reset. The value is always the real parameter value, never normalized.
import type { ParamDef } from './bridge';

const SWEEP = 270;
const START = 135;

function polar(cx: number, cy: number, r: number, deg: number): [number, number] {
  const rad = (deg * Math.PI) / 180;
  return [cx + r * Math.cos(rad), cy + r * Math.sin(rad)];
}

function arc(cx: number, cy: number, r: number, a0: number, a1: number): string {
  const [x0, y0] = polar(cx, cy, r, a0);
  const [x1, y1] = polar(cx, cy, r, a1);
  const large = Math.abs(a1 - a0) > 180 ? 1 : 0;
  const sweep = a1 > a0 ? 1 : 0;
  return `M ${x0.toFixed(2)} ${y0.toFixed(2)} A ${r} ${r} 0 ${large} ${sweep} ${x1.toFixed(2)} ${y1.toFixed(2)}`;
}

export function formatValue(def: ParamDef, value: number): string {
  if (def.unit === 'dB') {
    const v = Math.abs(value) < 0.05 ? 0 : value;
    return `${v > 0 ? '+' : ''}${v.toFixed(1)} dB`;
  }
  if (def.unit === 's') return value < 1 ? `${Math.round(value * 1000)} ms` : `${value.toFixed(2)} s`;
  return `${Math.round(value * 100)}%`;
}

export function toNorm(def: ParamDef, value: number): number {
  const t = Math.min(1, Math.max(0, (value - def.min) / (def.max - def.min)));
  return Math.pow(t, 1 / def.skew);
}

export function fromNorm(def: ParamDef, norm: number): number {
  const t = Math.pow(Math.min(1, Math.max(0, norm)), def.skew);
  return def.min + (def.max - def.min) * t;
}

export class Knob {
  readonly el: HTMLElement;

  private readonly def: ParamDef;
  private readonly onChange: (value: number) => void;
  private readonly arcPath: SVGPathElement;
  private readonly tip: SVGLineElement;
  private readonly readout: HTMLElement;
  private value: number;
  private dragY = 0;
  private dragNorm = 0;

  constructor(def: ParamDef, value: number, onChange: (value: number) => void) {
    this.def = def;
    this.value = value;
    this.onChange = onChange;

    const el = document.createElement('div');
    el.className = 'knob';
    el.title = def.name;
    el.innerHTML = `
      <div class="dial">
        <svg viewBox="0 0 44 44" width="44" height="44">
          <circle class="track" cx="22" cy="22" r="17" />
          <path class="arc" />
          <circle class="cap" cx="22" cy="22" r="12.5" />
          <line class="tip" x1="22" y1="22" x2="22" y2="12" />
          <circle class="glow" cx="22" cy="22" r="17" />
        </svg>
      </div>
      <div class="label"></div>
      <div class="readout"></div>`;
    this.el = el;
    this.arcPath = el.querySelector('.arc') as SVGPathElement;
    this.tip = el.querySelector('.tip') as SVGLineElement;
    this.readout = el.querySelector('.readout') as HTMLElement;
    (el.querySelector('.label') as HTMLElement).textContent = def.name;

    el.addEventListener('pointerdown', (e) => {
      e.preventDefault();
      this.dragY = e.clientY;
      this.dragNorm = toNorm(def, this.value);
      el.setPointerCapture(e.pointerId);
      el.classList.add('dragging');
    });
    el.addEventListener('pointermove', (e) => {
      if (!el.hasPointerCapture(e.pointerId)) return;
      const scale = e.shiftKey ? 900 : 170;
      const norm = Math.min(1, Math.max(0, this.dragNorm + (this.dragY - e.clientY) / scale));
      this.set(fromNorm(def, norm), true);
    });
    const end = (e: PointerEvent) => {
      if (el.hasPointerCapture(e.pointerId)) el.releasePointerCapture(e.pointerId);
      el.classList.remove('dragging');
    };
    el.addEventListener('pointerup', end);
    el.addEventListener('pointercancel', end);
    el.addEventListener('wheel', (e) => {
      e.preventDefault();
      const norm = Math.min(1, Math.max(0, toNorm(def, this.value) - Math.sign(e.deltaY) * 0.02));
      this.set(fromNorm(def, norm), true);
    }, { passive: false });
    el.addEventListener('dblclick', () => this.set(def.def, true));

    this.paint();
  }

  set(value: number, notify = false): void {
    this.value = Math.min(this.def.max, Math.max(this.def.min, value));
    this.paint();
    if (notify) this.onChange(this.value);
  }

  private paint(): void {
    const norm = toNorm(this.def, this.value);
    const a0 = START;
    const a1 = START + SWEEP * norm;
    this.arcPath.setAttribute('d', arc(22, 22, 17, a0, a1));
    const [x0, y0] = polar(22, 22, 4.2, a1);
    const [x1, y1] = polar(22, 22, 11.4, a1);
    this.tip.setAttribute('x1', x0.toFixed(2));
    this.tip.setAttribute('y1', y0.toFixed(2));
    this.tip.setAttribute('x2', x1.toFixed(2));
    this.tip.setAttribute('y2', y1.toFixed(2));
    this.readout.textContent = formatValue(this.def, this.value);
  }
}
