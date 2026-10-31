// Builds two tiny Decent Sampler libraries so the shelf has something to
// show before you own any: one plain folder and one .dslibrary zip. Also
// copies them into ~/Documents/Samplers, the default folder Trove scans.
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import os from 'node:os';
import { fileURLToPath } from 'node:url';

const SR = 44100;

const crcTable = (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    table[n] = c >>> 0;
  }
  return table;
})();

function crc32(buf) {
  let c = 0xffffffff;
  for (const b of buf) c = crcTable[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

function wav(samples) {
  const frames = samples.length;
  const out = Buffer.alloc(44 + frames * 2);
  out.write('RIFF', 0);
  out.writeUInt32LE(36 + frames * 2, 4);
  out.write('WAVE', 8);
  out.write('fmt ', 12);
  out.writeUInt32LE(16, 16);
  out.writeUInt16LE(1, 20);
  out.writeUInt16LE(1, 22);
  out.writeUInt32LE(SR, 24);
  out.writeUInt32LE(SR * 2, 28);
  out.writeUInt16LE(2, 32);
  out.writeUInt16LE(16, 34);
  out.write('data', 36);
  out.writeUInt32LE(frames * 2, 40);
  for (let i = 0; i < frames; i++) {
    const v = Math.max(-1, Math.min(1, samples[i]));
    out.writeInt16LE(Math.round(v * 32767), 44 + i * 2);
  }
  return out;
}

function png(width, height, pixels) {
  const raw = Buffer.alloc((width * 3 + 1) * height);
  for (let y = 0; y < height; y++) {
    raw[y * (width * 3 + 1)] = 0;
    pixels.copy(raw, y * (width * 3 + 1) + 1, y * width * 3, (y + 1) * width * 3);
  }
  const chunk = (type, data) => {
    const head = Buffer.alloc(8);
    head.writeUInt32BE(data.length, 0);
    head.write(type, 4);
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(crc32(Buffer.concat([Buffer.from(type), data])), 0);
    return Buffer.concat([head, data, crc]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8;
  ihdr[9] = 2;
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', zlib.deflateSync(raw)),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

function cover(width, height) {
  const px = Buffer.alloc(width * height * 3);
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const u = x / width, v = y / height;
      const d = Math.hypot(u - 0.72, v - 0.3);
      const glow = Math.max(0, 1 - d * 2.4);
      const r = 14 + glow * 120 + v * 24;
      const g = 22 + glow * 108 + (1 - u) * 40;
      const b = 26 + glow * 74 + u * 46;
      const i = (y * width + x) * 3;
      px[i] = Math.min(255, r);
      px[i + 1] = Math.min(255, g);
      px[i + 2] = Math.min(255, b);
    }
  }
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const d = Math.hypot(x - width * 0.72, y - height * 0.3);
      if (Math.abs(d - height * 0.13) < 2.2) {
        const i = (y * width + x) * 3;
        px[i] = 238;
        px[i + 1] = 206;
        px[i + 2] = 132;
      }
    }
  }
  return png(width, height, px);
}

function tone(note, seconds, bright) {
  const f = 440 * Math.pow(2, (note - 69) / 12);
  const frames = Math.floor(SR * seconds);
  const out = new Float32Array(frames);
  const partials = bright
    ? [[1, 1], [2, 0.55], [3, 0.36], [4, 0.26], [5, 0.16], [6, 0.12], [8, 0.07]]
    : [[1, 1], [2, 0.34], [3, 0.14], [4, 0.06], [5, 0.03]];
  const norm = partials.reduce((s, p) => s + p[1], 0);
  const periodsIn = (t) => Math.round(f * t);
  const loopStart = Math.round((periodsIn(1.0) * SR) / f);
  const loopEnd = Math.round(((periodsIn(1.0) + periodsIn(0.8)) * SR) / f);
  const atk = Math.floor(SR * 0.04);
  const dec = Math.floor(SR * 0.9);
  for (let i = 0; i < frames; i++) {
    let s = 0;
    for (const [h, a] of partials) s += a * Math.sin((2 * Math.PI * f * h * i) / SR);
    s /= norm;
    let env = 1;
    if (i < atk) env = i / atk;
    else if (i < dec) env = 1 - 0.2 * ((i - atk) / (dec - atk));
    else env = 0.8;
    out[i] = s * env * 0.55;
  }
  return { samples: out, loopStart, loopEnd };
}

function zipStore(files) {
  const locals = [];
  const centrals = [];
  let offset = 0;
  for (const [name, data] of files) {
    const nameBuf = Buffer.from(name, 'utf8');
    const crc = crc32(data);
    const local = Buffer.alloc(30 + nameBuf.length);
    local.writeUInt32LE(0x04034b50, 0);
    local.writeUInt16LE(20, 4);
    local.writeUInt16LE(0, 6);
    local.writeUInt16LE(0, 8);
    local.writeUInt16LE(0, 10);
    local.writeUInt16LE(0, 12);
    local.writeUInt32LE(crc, 14);
    local.writeUInt32LE(data.length, 18);
    local.writeUInt32LE(data.length, 22);
    local.writeUInt16LE(nameBuf.length, 26);
    local.writeUInt16LE(0, 28);
    nameBuf.copy(local, 30);

    const central = Buffer.alloc(46 + nameBuf.length);
    central.writeUInt32LE(0x02014b50, 0);
    central.writeUInt16LE(20, 4);
    central.writeUInt16LE(20, 6);
    central.writeUInt16LE(0, 8);
    central.writeUInt16LE(0, 10);
    central.writeUInt16LE(0, 12);
    central.writeUInt16LE(0, 14);
    central.writeUInt32LE(crc, 16);
    central.writeUInt32LE(data.length, 20);
    central.writeUInt32LE(data.length, 24);
    central.writeUInt16LE(nameBuf.length, 28);
    central.writeUInt16LE(0, 30);
    central.writeUInt16LE(0, 32);
    central.writeUInt16LE(0, 34);
    central.writeUInt16LE(0, 36);
    central.writeUInt32LE(0, 38);
    central.writeUInt32LE(offset, 42);
    nameBuf.copy(central, 46);

    locals.push(local, data);
    centrals.push(central);
    offset += local.length + data.length;
  }
  const cd = Buffer.concat(centrals);
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0);
  end.writeUInt16LE(files.length, 8);
  end.writeUInt16LE(files.length, 10);
  end.writeUInt32LE(cd.length, 12);
  end.writeUInt32LE(offset, 16);
  return Buffer.concat([...locals, cd, end]);
}

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, '..');
const examples = path.join(repo, 'examples');

function writeLibrary() {
  const dir = path.join(examples, 'Glass Keys');
  const samplesDir = path.join(dir, 'Samples');
  fs.mkdirSync(samplesDir, { recursive: true });

  const roots = [36, 43, 48, 55, 60, 67, 72];
  const zones = [];
  for (const note of roots) {
    for (const layer of ['soft', 'hard']) {
      const { samples, loopStart, loopEnd } = tone(note, 3.0, layer === 'hard');
      const name = `glass-${note}-${layer}.wav`;
      fs.writeFileSync(path.join(samplesDir, name), wav(samples));
      zones.push(
        `      <sample path="Samples/${name}" rootNote="${note}" loNote="${note - 4}" hiNote="${note + 3}"` +
        ` loVel="${layer === 'soft' ? 1 : 64}" hiVel="${layer === 'soft' ? 63 : 127}"` +
        ` loopStart="${loopStart}" loopEnd="${loopEnd}" />`
      );
    }
  }

  const preset = `<?xml version="1.0" encoding="UTF-8"?>
<DecentSampler minVersion="1.0.0">
  <groups attack="0.012" decay="0.8" sustain="0.75" release="0.7" volume="-4dB" ampVelTrack="0.85" globalPan="0">
    <group name="Glass" volume="1.0">
${zones.join('\n')}
    </group>
  </groups>
</DecentSampler>
`;
  fs.writeFileSync(path.join(dir, 'Glass Keys.dspreset'), preset);
  fs.writeFileSync(path.join(dir, 'cover.png'), cover(900, 560));
  return dir;
}

function writeZipLibrary() {
  const notes = [45, 52, 57, 64];
  const entries = [];
  const zones = [];
  for (const note of notes) {
    const { samples, loopStart, loopEnd } = tone(note, 2.0, note > 55);
    const name = `Brass Cabinet/Samples/brass-${note}.wav`;
    entries.push([name, wav(samples)]);
    zones.push(
      `      <sample path="Samples/brass-${note}.wav" rootNote="${note}" loNote="${note - 5}" hiNote="${note + 4}"` +
      ` loopStart="${loopStart}" loopEnd="${loopEnd}" />`
    );
  }
  const preset = `<?xml version="1.0" encoding="UTF-8"?>
<DecentSampler minVersion="1.0.0" backgroundImage="cover.png">
  <groups attack="0.02" decay="1.2" sustain="0.6" release="1.1" volume="-5dB">
    <group name="Cabinet">
${zones.join('\n')}
    </group>
  </groups>
</DecentSampler>
`;
  entries.push(['Brass Cabinet/Brass Cabinet.dspreset', Buffer.from(preset)]);
  entries.push(['Brass Cabinet/cover.png', cover(900, 560)]);
  const zip = zipStore(entries);
  const file = path.join(examples, 'Brass Cabinet.dslibrary');
  fs.writeFileSync(file, zip);
  return file;
}

function install(folder) {
  const target = path.join(os.homedir(), 'Documents', 'Samplers');
  fs.mkdirSync(target, { recursive: true });
  fs.cpSync(folder, path.join(target, path.basename(folder)), { recursive: true });
  return target;
}

const folder = writeLibrary();
const zipFile = writeZipLibrary();
const root = install(folder);
fs.copyFileSync(zipFile, path.join(root, path.basename(zipFile)));

console.log(`built ${path.relative(repo, folder)} (${12} zones)`);
console.log(`built ${path.relative(repo, zipFile)} (4 zones)`);
console.log(`installed into ${root}`);
