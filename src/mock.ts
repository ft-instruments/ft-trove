// Demo shelf for running the page in a browser tab. Mirrors the shape the
// plugin sends, powering the layout and the preset player but nothing here
// talks to an audio engine.
import type { LibEntry, ParamDef, UiSpec } from './bridge';

export const mockParams: ParamDef[] = [
  { id: 'gain', name: 'Level', min: -36, max: 18, def: 0, skew: 1, unit: 'dB' },
  { id: 'tone', name: 'Tone', min: 0, max: 1, def: 1, skew: 1, unit: '' },
  { id: 'reverb', name: 'Space', min: 0, max: 1, def: 0.12, skew: 1, unit: '' },
  { id: 'attack', name: 'Attack', min: 0, max: 12, def: 0, skew: 2.2, unit: 's' },
  { id: 'decay', name: 'Decay', min: 0, max: 12, def: 0.3, skew: 2, unit: 's' },
  { id: 'sustain', name: 'Sustain', min: 0, max: 1, def: 1, skew: 1, unit: '' },
  { id: 'release', name: 'Release', min: 0, max: 20, def: 0.4, skew: 2.2, unit: 's' },
];

const demo = [
  ['Glass Keys', 'folder', '#2f4a44', ['Glass Keys', 'Glass Keys Soft', 'Glass Keys Wide']],
  ['Brass Cabinet', 'archive', '#58402c', ['Cabinet', 'Muted Cabinet', 'Sforzando']],
  ['Felt Piano', 'folder', '#4a3f4f', ['Felt Piano', 'Felt Piano Close', 'Felt Piano Room']],
  ['Cathedral Organ', 'folder', '#2c3d55', ['Great', 'Swell', 'Pedal', 'Full Organ']],
  ['Paper Drums', 'archive', '#553229', ['Kit', 'Brushed Kit']],
  ['Cassette Strings', 'folder', '#3c4a33', ['Ensemble', 'Solo']],
  ['Toy Glockenspiel', 'folder', '#4d4732', ['Glock']],
  ['Iron Choir', 'archive', '#333b40', ['Aahs', 'Oohs', 'Hum']],
];

export const mockLibs: LibEntry[] = demo.map((d, i) => {
  const names = d[3] as string[];
  return {
    id: `demo-${i}`,
    name: String(d[0]),
    kind: d[1] as 'folder' | 'bundle' | 'archive',
    source: `/Users/you/Documents/Samplers/${d[0]}`,
    tint: String(d[2]),
    zones: 14 * names.length,
    low: 36,
    high: 84,
    count: names.length,
    presets: names.map((n, j) => ({
      id: `demo-${i}-${j}`,
      name: n,
      preset: `/Users/you/Documents/Samplers/${d[0]}/${n}.dspreset`,
      zones: 14,
      groups: 1,
      low: 36,
      high: 84,
      ok: true,
    })),
  };
});

export const mockRoots = ['/Users/you/Documents/Samplers', '/Users/you/Music/Libraries'];

export const mockUi: UiSpec = {
  width: 812,
  height: 375,
  bgColor: '#101815',
  hasKeyboard: true,
  tabs: ['main'],
  controls: [
    { type: 'label', x: 40, y: 32, w: 300, h: 30, text: 'Glass Keys', textColor: '#e8dfc9', textSize: 22, min: 0, max: 0, value: 0 },
    { type: 'knob', x: 70, y: 120, w: 110, h: 110, text: 'Filter', fg: '#c8a55c', bg: '#3a4741', textColor: '#cfd7cd', textSize: 14, min: 20, max: 20000, value: 6000, style: 'rotary_vertical_drag', bindings: [{ type: 'effect', parameter: 'FX_FILTER_FREQUENCY', position: 0 }] },
    { type: 'knob', x: 260, y: 120, w: 110, h: 110, text: 'Reverb', fg: '#c8a55c', bg: '#3a4741', textColor: '#cfd7cd', textSize: 14, min: 0, max: 100, value: 18, valueType: 'percent', style: 'rotary_vertical_drag', bindings: [{ type: 'effect', parameter: 'FX_REVERB_WET_LEVEL', position: 1, translation: 'linear', outMin: 0, outMax: 1 }] },
    { type: 'knob', x: 450, y: 120, w: 110, h: 110, text: 'Attack', fg: '#c8a55c', bg: '#3a4741', textColor: '#cfd7cd', textSize: 14, min: 0, max: 10, value: 0.4, style: 'rotary_vertical_drag', bindings: [{ type: 'amp', parameter: 'ENV_ATTACK', position: 0 }] },
    { type: 'knob', x: 640, y: 120, w: 110, h: 110, text: 'Release', fg: '#c8a55c', bg: '#3a4741', textColor: '#cfd7cd', textSize: 14, min: 0, max: 10, value: 2.2, style: 'rotary_vertical_drag', bindings: [{ type: 'amp', parameter: 'ENV_RELEASE', position: 3 }] },
  ],
};
