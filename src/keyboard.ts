// A full 88-key piano, A0 to C8. Velocity comes from where on the key you
// press: the top is soft, the bottom is hard. The computer keyboard plays too.
// Libraries may tint the spans they map, as Decent Sampler does.

import type { KeyRange } from './bridge';

const LOW = 21;
const HIGH = 108;
const BLACK = new Set([1, 3, 6, 8, 10]);
const NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const TYPING: Record<string, number> = {
  a: 0, w: 1, s: 2, e: 3, d: 4, f: 5, t: 6, g: 7, y: 8, h: 9, u: 10, j: 11, k: 12,
};

export function noteName(note: number): string {
  return `${NAMES[((note % 12) + 12) % 12]}${Math.floor(note / 12) - 1}`;
}

export class Keyboard {
  private readonly el: HTMLElement;
  private readonly onNote: (note: number, velocity: number, on: boolean) => void;
  private readonly keyEls = new Map<number, HTMLElement>();
  private readonly pointers = new Map<number, number>();
  private readonly typed = new Map<string, number>();
  private octave = 3;
  private sustain = false;
  private readonly low = LOW;
  private readonly high = HIGH;

  constructor(el: HTMLElement, onNote: (note: number, velocity: number, on: boolean) => void) {
    this.el = el;
    this.onNote = onNote;
    this.build(LOW, HIGH);
    this.bindPointer();
    this.bindTyping();
  }

  // Keys held down from outside the page (MIDI in), lit like a pressed key.
  setHeld(notes: number[]): void {
    const held = new Set(notes);
    for (const [note, key] of this.keyEls) key.classList.toggle('midi', held.has(note));
  }

  // Later ranges paint over earlier ones, like stacked <color> elements.
  setTints(ranges: KeyRange[]): void {
    for (const [note, key] of this.keyEls) {
      let colour = '';
      for (const r of ranges) if (note >= r.lo && note <= r.hi) colour = r.color;
      key.classList.toggle('tinted', colour !== '');
      if (colour) key.style.setProperty('--tint', colour);
      else key.style.removeProperty('--tint');
    }
  }

  private build(low: number, high: number): void {
    this.el.replaceChildren();
    const whites: number[] = [];
    for (let n = low; n <= high; n++) if (!BLACK.has(((n % 12) + 12) % 12)) whites.push(n);
    const w = 100 / whites.length;

    for (let i = 0; i < whites.length; i++) {
      const key = document.createElement('div');
      key.className = 'key white';
      key.dataset.note = String(whites[i]);
      key.style.left = `${i * w}%`;
      key.style.width = `${w}%`;
      const label = document.createElement('span');
      label.textContent = NAMES[((whites[i] % 12) + 12) % 12] === 'C' ? noteName(whites[i]) : '';
      key.appendChild(label);
      this.el.appendChild(key);
      this.keyEls.set(whites[i], key);
    }

    for (let i = 0; i < whites.length - 1; i++) {
      const next = whites[i] + 1;
      if (!BLACK.has(((next % 12) + 12) % 12)) continue;
      const key = document.createElement('div');
      key.className = 'key black';
      key.dataset.note = String(next);
      key.style.left = `${(i + 1) * w - w * 0.31}%`;
      key.style.width = `${w * 0.62}%`;
      this.el.appendChild(key);
      this.keyEls.set(next, key);
    }
  }

  private velocityOf(e: PointerEvent, key: HTMLElement): number {
    const rect = key.getBoundingClientRect();
    const rel = Math.min(1, Math.max(0, (e.clientY - rect.top) / rect.height));
    return Math.round(127 - rel * 82);
  }

  private bindPointer(): void {
    this.el.addEventListener('pointerdown', (e) => {
      const key = (e.target as HTMLElement).closest('[data-note]') as HTMLElement | null;
      if (!key) return;
      e.preventDefault();
      const note = Number(key.dataset.note);
      this.pointers.set(e.pointerId, note);
      key.classList.add('on');
      this.onNote(note, this.velocityOf(e, key), true);
    });

    const release = (e: PointerEvent) => {
      const note = this.pointers.get(e.pointerId);
      if (note === undefined) return;
      this.pointers.delete(e.pointerId);
      this.keyEls.get(note)?.classList.remove('on');
      this.onNote(note, 0, false);
    };
    window.addEventListener('pointerup', release);
    window.addEventListener('pointercancel', release);
  }

  private bindTyping(): void {
    window.addEventListener('keydown', (e) => {
      const tag = (e.target as HTMLElement | null)?.tagName;
      if (tag === 'INPUT' || tag === 'TEXTAREA' || e.metaKey || e.ctrlKey || e.altKey) return;

      if (e.code === 'Space') {
        e.preventDefault();
        if (!this.sustain) {
          this.sustain = true;
          this.el.classList.add('sustained');
          window.dispatchEvent(new CustomEvent('trove-sustain', { detail: true }));
        }
        return;
      }
      if (e.key === 'z' || e.key === 'x') {
        this.octave = Math.min(6, Math.max(1, this.octave + (e.key === 'z' ? -1 : 1)));
        return;
      }
      const step = TYPING[e.key.toLowerCase()];
      if (step === undefined || e.repeat || this.typed.has(e.key.toLowerCase())) return;
      const raw = 12 * (this.octave + 1) + step;
      const note = Math.min(this.high, Math.max(this.low, raw));
      this.typed.set(e.key.toLowerCase(), note);
      this.keyEls.get(note)?.classList.add('on');
      this.onNote(note, 100, true);
    });

    window.addEventListener('keyup', (e) => {
      if (e.code === 'Space') {
        if (this.sustain) {
          this.sustain = false;
          this.el.classList.remove('sustained');
          window.dispatchEvent(new CustomEvent('trove-sustain', { detail: false }));
        }
        return;
      }
      const note = this.typed.get(e.key.toLowerCase());
      if (note === undefined) return;
      this.typed.delete(e.key.toLowerCase());
      this.keyEls.get(note)?.classList.remove('on');
      this.onNote(note, 0, false);
    });
  }
}
