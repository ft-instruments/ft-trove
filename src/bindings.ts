// What a <binding> does. A control's value is in the preset's own units; a
// binding maps it (translation, factor, fixed value) onto a parameter, and the
// parameter lives in one of three places:
//   - a host knob (envelope, master level), which Trove already has
//   - the sound engine (groups, tags, effects, tuning), sent as a "bind" message
//   - another control on the same panel (VALUE, VISIBLE, OPACITY, CURRENT_FRAME)
import type { UiBinding, UiControl } from './bridge';

export type Native = number | boolean | string;

export type HostId = 'gain' | 'tone' | 'reverb' | 'attack' | 'decay' | 'sustain' | 'release';

export interface HostTarget {
  id: HostId;
  toHost(native: number): number;
  fromHost(host: number): number;
}

// A message for the sound engine, already reduced to plain targets.
export type EngineMessage =
  | { kind: 'groupOn'; group: number; value: number }
  | { kind: 'groupVolume'; group: number; value: number }
  | { kind: 'groupPan'; group: number; value: number }
  | { kind: 'tagVolume'; tag: string; value: number }
  | { kind: 'groupEnv'; group: number; stage: 'attack' | 'decay' | 'sustain' | 'release'; value: number }
  | { kind: 'groupFx'; group: number; index: number; param: string; value: number }
  | { kind: 'fx'; index: number; param: string; value: number }
  | { kind: 'fxIr'; index: number; file: string }
  | { kind: 'tuning'; value: number }
  | { kind: 'modParam'; index: number; param: string; value: number }
  | { kind: 'velEnabled'; index: number; value: number }
  | { kind: 'groupFxPos'; group: number; index: number; value: number };

export type UiParameter = 'VALUE' | 'VISIBLE' | 'OPACITY' | 'CURRENT_FRAME';

// ------------------------------------------------------------- translation

function tablePoints(table: string): number[][] {
  return table.split(';').map((p) => p.split(',').map(Number))
    .filter((p) => p.length === 2 && p.every(Number.isFinite))
    .sort((p, q) => p[0] - q[0]);
}

// Control value -> the binding's parameter range.
export function translate(b: UiBinding, control: UiControl, value: number): number {
  let out = value;
  if (b.translation === 'table' && b.table) {
    const pts = tablePoints(b.table);
    // Most tables are keyed in the control's own units (ASIMOV's cutoff goes
    // 0..10, LØ's velocity table 0..127). Basic Piano is the odd one: its Tone
    // knob shows 60..22000 but its table is keyed 0..1, so normalize only when
    // the table itself ends at 1.
    const tableMax = pts.length ? pts[pts.length - 1][0] : 1;
    const x = tableMax <= 1.001
      ? (value - control.min) / Math.max(1e-9, control.max - control.min)
      : value;
    if (pts.length) {
      out = pts[pts.length - 1][1];
      if (x <= pts[0][0]) out = pts[0][1];
      else {
        for (let i = 1; i < pts.length; i++) {
          if (x <= pts[i][0]) {
            const t = (x - pts[i - 1][0]) / Math.max(1e-9, pts[i][0] - pts[i - 1][0]);
            out = pts[i - 1][1] + t * (pts[i][1] - pts[i - 1][1]);
            break;
          }
        }
      }
    }
  } else if (b.translation !== 'fixed_value' && typeof b.outMin === 'number' && typeof b.outMax === 'number') {
    const norm = (value - control.min) / Math.max(1e-9, control.max - control.min);
    out = b.outMin + norm * (b.outMax - b.outMin);
  }
  return b.factor !== undefined ? out * b.factor : out;
}

export function untranslate(b: UiBinding, control: UiControl, native: number): number | null {
  if (b.translation === 'table' || b.translation === 'fixed_value') return null;
  let n = native;
  if (b.factor) n = n / b.factor;
  if (typeof b.outMin === 'number' && typeof b.outMax === 'number' && b.outMax !== b.outMin) {
    const norm = (n - b.outMin) / (b.outMax - b.outMin);
    return control.min + norm * (control.max - control.min);
  }
  return n;
}

// A state's or option's own value, which does not depend on any control.
export function fixedValue(b: UiBinding): Native {
  const raw = (b.fixed ?? '').trim();
  if (raw === 'true') return true;
  if (raw === 'false') return false;
  const n = Number(raw);
  return raw !== '' && Number.isFinite(n) ? n : raw;
}

export function nativeOf(b: UiBinding, control: UiControl | null, value: number): Native {
  if (b.translation === 'fixed_value') return fixedValue(b);
  return control ? translate(b, control, value) : value;
}

// ------------------------------------------------------------------ targets

const FILTER_LOW_HZ = 20;
const FILTER_HIGH_HZ = 20000;

const upper = (s: string | undefined): string => (s ?? '').toUpperCase();
const lower = (s: string | undefined): string => (s ?? '').toLowerCase();

// The knobs Trove already owns: the envelope and the master level.
export function hostTarget(b: UiBinding): HostTarget | null {
  if (lower(b.type) !== 'amp') return null;
  const level = lower(b.level) || 'instrument';
  // A group's envelope belongs to that group; the engine handles it (groupEnv).
  if (level === 'group' || level === 'tag') return null;
  const parameter = upper(b.parameter);
  const same = (id: HostId): HostTarget => ({ id, toHost: (n) => n, fromHost: (h) => h });
  if (parameter.includes('ATTACK')) return same('attack');
  if (parameter.includes('DECAY')) return same('decay');
  if (parameter.includes('RELEASE')) return same('release');
  if (parameter.includes('SUSTAIN')) return same('sustain');
  if (parameter === 'AMP_VOLUME' && level === 'instrument') {
    return { id: 'gain', toHost: (n) => 20 * Math.log10(Math.max(1e-4, n)), fromHost: (h) => Math.pow(10, h / 20) };
  }
  return null;
}

export { FILTER_LOW_HZ, FILTER_HIGH_HZ };

const asNumber = (v: Native): number => (typeof v === 'boolean' ? (v ? 1 : 0) : typeof v === 'number' ? v : Number(v) || 0);

// Everything a binding sends to the engine. A cutoff that something modulates
// (Bitscape's filter envelope) also needs where the knob sits along its table.
export function engineMessages(b: UiBinding, native: Native, position?: number): EngineMessage[] {
  const main = engineMessage(b, native);
  if (!main) return [];
  if (main.kind === 'groupFx' && b.translation === 'table' && position !== undefined) {
    return [main, { kind: 'groupFxPos', group: main.group, index: main.index, value: position }];
  }
  return [main];
}

// The sound-engine message for a binding, or null when it is not one.
export function engineMessage(b: UiBinding, native: Native): EngineMessage | null {
  const type = lower(b.type);
  const level = lower(b.level) || 'instrument';
  const parameter = upper(b.parameter);
  const n = asNumber(native);
  const position = b.position ?? 0;

  if (parameter === 'ENABLED' && level === 'group' && (type === 'amp' || type === 'general' || type === 'effect')) {
    return { kind: 'groupOn', group: position, value: n >= 0.5 ? 1 : 0 };
  }
  if (level === 'group') {
    if (type === 'amp' && parameter === 'AMP_VOLUME') return { kind: 'groupVolume', group: position, value: n };
    if (type === 'amp' && parameter === 'PAN') return { kind: 'groupPan', group: position, value: n / 100 };
    if (type === 'amp') {
      const stage = (['attack', 'decay', 'sustain', 'release'] as const).find((s) => parameter === `ENV_${s.toUpperCase()}`);
      if (stage) return { kind: 'groupEnv', group: position, stage, value: n };
    }
    if (type === 'effect') {
      return { kind: 'groupFx', group: b.groupIndex ?? position, index: b.effectIndex ?? 0, param: parameter, value: n };
    }
    return null;
  }
  if (level === 'tag') {
    if (type === 'amp' && parameter === 'AMP_VOLUME' && b.identifier) return { kind: 'tagVolume', tag: b.identifier, value: n };
    return null;
  }
  if (type === 'velocity_binding' && parameter === 'ENABLED') {
    return { kind: 'velEnabled', index: b.bindingIndex ?? 0, value: n >= 0.5 ? 1 : 0 };
  }
  if (type === 'modulator') {
    return { kind: 'modParam', index: b.modulatorIndex ?? position, param: parameter, value: n };
  }
  if (level === 'instrument') {
    if (type === 'effect') {
      if (parameter === 'FX_IR_FILE') return typeof native === 'string' ? { kind: 'fxIr', index: position, file: native } : null;
      return { kind: 'fx', index: b.effectIndex ?? position, param: parameter, value: n };
    }
    if (type === 'amp' && parameter === 'GLOBAL_TUNING') return { kind: 'tuning', value: n };
  }
  return null;
}

// Which UI property a ui-level binding drives, and which controls it targets.
export function uiTarget(b: UiBinding): { parameter: UiParameter; tags: string[]; index: number | null } | null {
  if (lower(b.type) !== 'control' || lower(b.level) !== 'ui') return null;
  const parameter = upper(b.parameter) as UiParameter;
  if (!['VALUE', 'VISIBLE', 'OPACITY', 'CURRENT_FRAME'].includes(parameter)) return null;
  const tags = (b.tags ?? '').split(',').map((t) => t.trim()).filter(Boolean);
  return { parameter, tags, index: tags.length ? null : (b.position ?? 0) };
}
