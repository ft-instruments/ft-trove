// The page's handle on the plugin. Inside the WebView every call goes over
// JUCE's event bridge to plugin/Source/PluginEditor.cpp; in a browser tab the
// shelf runs on the demo data in mock.ts so the layout can be worked on.

export interface ParamDef {
  id: string;
  name: string;
  min: number;
  max: number;
  def: number;
  skew: number;
  unit: string;
}

export interface PresetEntry {
  id: string;
  name: string;
  preset: string;
  zones: number;
  groups: number;
  low: number;
  high: number;
  ok: boolean;
  error?: string;
}

export interface LibEntry {
  id: string;
  name: string;
  kind: 'folder' | 'bundle' | 'archive';
  source: string;
  tint: string;
  zones: number;
  low: number;
  high: number;
  count: number;
  art?: string;
  error?: string;
  presets: PresetEntry[];
}

export interface UiBinding {
  type?: string;
  parameter?: string;
  level?: string;
  position?: number;
  translation?: string;
  table?: string;
  outMin?: number;
  outMax?: number;
  identifier?: string;      // tag name, for level "tag"
  tags?: string;            // UI tags it targets, for level "ui"
  groupIndex?: number;
  effectIndex?: number;
  modulatorIndex?: number;
  bindingIndex?: number;
  fixed?: string;           // translationValue
  factor?: number;
  triggerOnLoad?: boolean;
}

export interface UiState {
  name: string;
  main: string;
  click: string;
  hover: string;
  bindings?: UiBinding[];
}

export interface UiControl {
  type: 'knob' | 'bar' | 'skin' | 'button' | 'menu' | 'label' | 'image' | 'strip';
  tab?: string;
  tags?: string;
  labeled?: boolean;
  uiIndex?: number;
  x: number;
  y: number;
  w: number;
  h: number;
  text?: string;
  style?: string;
  source?: string;
  fg?: string;
  bg?: string;
  textColor?: string;
  textSize?: number;
  min: number;
  max: number;
  value: number;
  defaultValue?: number;
  valueType?: string;
  showLabel?: boolean;
  visible?: boolean;
  vertical?: boolean;
  dragVertical?: boolean;
  stretch?: boolean;
  frames?: number;
  dragPixels?: number;
  frameRate?: number;
  opacity?: number;
  align?: string;
  placeholder?: string;
  options?: string[];
  optionBindings?: UiBinding[][];
  states?: UiState[];
  bindings?: UiBinding[];
}

export interface UiSpec {
  width: number;
  height: number;
  bgColor?: string;
  bgImage?: string;
  bgTopLeft?: boolean;
  hasKeyboard?: boolean;
  keyRanges?: KeyRange[];
  tabs?: string[];
  controls: UiControl[];
}

export interface KeyRange {
  lo: number;
  hi: number;
  color: string;
}

export interface LoadedMsg {
  id: string;
  name: string;
  preset?: string;
  dir?: string;
  ui?: UiSpec | null;
  values?: Record<string, number>;
  zones?: number;
  groups?: number;
  attack?: number;
  decay?: number;
  sustain?: number;
  release?: number;
  gainDb?: number;
  error?: string;
}

interface Backend {
  emitEvent(id: string, payload: unknown): void;
  addEventListener(id: string, fn: (payload: unknown) => void): unknown;
}

const backend = (): Backend | undefined =>
  (window as unknown as { __JUCE__?: { backend?: Backend } }).__JUCE__?.backend;

export const inPlugin = (): boolean => backend() !== undefined;

export function send(msg: object): void {
  backend()?.emitEvent('trove', msg);
}

export function onMessage(fn: (msg: any) => void): void {
  backend()?.addEventListener('trove', fn);
}
