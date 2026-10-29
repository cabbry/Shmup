// sf2.js -- a small SoundFont 2 player: parse a bank, render notes into a buffer.
// Enough of the spec for a General MIDI bank: zones with key/velocity ranges,
// global zones, preset generators added to instrument ones, sample loops,
// the volume envelope, the vibrato LFO, the static lowpass and tuning.
'use strict';
const fs = require('fs');

function readSF2(path) {
  const b = fs.readFileSync(path);
  const chunks = {};
  function walk(off, end) {
    while (off + 8 <= end) {
      const id = b.toString('latin1', off, off + 4);
      const size = b.readUInt32LE(off + 4);
      if (id === 'RIFF' || id === 'LIST') walk(off + 12, off + 8 + size);
      else chunks[id] = { off: off + 8, size };
      off += 8 + size + (size & 1);
    }
  }
  walk(0, b.length);
  const smpl = chunks.smpl;
  const data = new Int16Array(b.buffer.slice(b.byteOffset + smpl.off, b.byteOffset + smpl.off + smpl.size));
  const name = (o, n) => b.toString('latin1', o, o + n).replace(/\0.*$/, '');
  const recs = (id, sz, f) => { const c = chunks[id]; const r = []; for (let o = c.off; o + sz <= c.off + c.size; o += sz) r.push(f(o)); return r; };
  const phdr = recs('phdr', 38, o => ({ name: name(o, 20), preset: b.readUInt16LE(o + 20), bank: b.readUInt16LE(o + 22), bag: b.readUInt16LE(o + 24) }));
  const pbag = recs('pbag', 4, o => b.readUInt16LE(o));
  const pgen = recs('pgen', 4, o => ({ op: b.readUInt16LE(o), s: b.readInt16LE(o + 2), lo: b[o + 2], hi: b[o + 3] }));
  const inst = recs('inst', 22, o => ({ name: name(o, 20), bag: b.readUInt16LE(o + 20) }));
  const ibag = recs('ibag', 4, o => b.readUInt16LE(o));
  const igen = recs('igen', 4, o => ({ op: b.readUInt16LE(o), s: b.readInt16LE(o + 2), lo: b[o + 2], hi: b[o + 3] }));
  const shdr = recs('shdr', 46, o => ({ name: name(o, 20), start: b.readUInt32LE(o + 20), end: b.readUInt32LE(o + 24),
    loopStart: b.readUInt32LE(o + 28), loopEnd: b.readUInt32LE(o + 32), rate: b.readUInt32LE(o + 36),
    pitch: b[o + 40], corr: b.readInt8(o + 41), type: b.readUInt16LE(o + 44) }));

  // zones: arrays of {gens: Map op->gen}
  function zones(bags, gens, from, to) {
    const z = [];
    for (let i = from; i < to; i++) {
      const m = new Map();
      for (let g = bags[i]; g < bags[i + 1]; g++) m.set(gens[g].op, gens[g]);
      z.push(m);
    }
    return z;
  }
  const instruments = inst.slice(0, -1).map((ins, i) => {
    const z = zones(ibag, igen, ins.bag, inst[i + 1].bag);
    let glob = null;
    if (z.length && !z[0].has(53)) glob = z.shift();
    return { name: ins.name, glob, zones: z };
  });
  const presets = {};
  phdr.slice(0, -1).forEach((p, i) => {
    const z = zones(pbag, pgen, p.bag, phdr[i + 1].bag);
    let glob = null;
    if (z.length && !z[0].has(41)) glob = z.shift();
    presets[p.bank + ':' + p.preset] = { name: p.name, glob, zones: z };
  });
  return { data, presets, instruments, shdr };
}

const DEF = { 8: 13500, 9: 0, 21: -12000, 22: 0, 23: -12000, 24: 0, 33: -12000, 34: -12000, 35: -12000, 36: -12000, 37: 0, 38: -12000,
  48: 0, 51: 0, 52: 0, 54: 0, 56: 100, 58: -1, 6: 0, 17: 0, 0: 0, 1: 0, 2: 0, 3: 0, 4: 0, 12: 0, 45: 0, 50: 0 };
const inRange = (g, v) => !g || (v >= g.lo && v <= g.hi);

// The zones (with merged generator values) that sound for this key/velocity.
function voicesFor(sf, bank, program, key, vel) {
  const p = sf.presets[bank + ':' + program] || sf.presets['0:' + program];
  if (!p) throw new Error('no preset ' + bank + ':' + program);
  const out = [];
  for (const pz of p.zones) {
    if (!inRange(pz.get(43), key) || !inRange(pz.get(44), vel)) continue;
    const ins = sf.instruments[pz.get(41).s];
    for (const iz of ins.zones) {
      const kr = iz.get(43) || ins.glob?.get(43), vr = iz.get(44) || ins.glob?.get(44);
      if (!inRange(kr, key) || !inRange(vr, vel)) continue;
      const g = {};
      for (const k in DEF) g[k] = DEF[k];
      if (ins.glob) for (const [op, v] of ins.glob) g[op] = v.s;
      for (const [op, v] of iz) g[op] = v.s;
      const add = (m) => { if (m) for (const [op, v] of m) if (op !== 41 && op !== 43 && op !== 44 && op !== 53 && op !== 54 && op !== 58) g[op] = (g[op] || 0) + v.s; };
      add(p.glob); add(pz);
      g.sample = sf.shdr[iz.get(53).s];
      g.mode = (iz.get(54) || ins.glob?.get(54))?.s ?? 0;
      out.push(g);
    }
  }
  return out;
}

const tc = t => Math.pow(2, t / 1200);   // timecents -> seconds

// Render one note into `buf` (Float32Array at SR). Returns nothing.
// opts: {vol, bend (cents, constant), vib (extra cents), vibRate, glideFrom}
function renderNote(sf, buf, SR, bank, program, key, vel, t0, dur, opts = {}) {
  const zones = voicesFor(sf, bank, program, key, vel);
  const velGain = Math.pow(vel / 127, 1.6);
  for (const g of zones) {
    const s = g.sample;
    const root = g[58] >= 0 ? g[58] : s.pitch;
    const cents = (key - root) * g[56] + g[51] * 100 + g[52] + s.corr + (opts.bend || 0);
    const ratio = Math.pow(2, cents / 1200) * s.rate / SR;
    const start = s.start + g[0] + g[4] * 32768, end = s.end + g[1] + g[12] * 32768;
    const ls = s.loopStart + g[2] + g[45] * 32768, le = s.loopEnd + g[3] + g[50] * 32768;
    const loop = (g.mode === 1 || g.mode === 3) && le > ls + 4;
    const atten = Math.pow(10, -(g[48] * 0.4) / 200);
    const delay = tc(g[33]), att = tc(g[34]), hold = tc(g[35]), dec = tc(g[36]);
    const sus = Math.pow(10, -Math.min(1440, Math.max(0, g[37])) / 200);
    const rel = Math.min(6, tc(g[38]));
    const vibDelay = tc(g[23]), vibF = 8.176 * Math.pow(2, g[24] / 1200), vibAmt = g[6];
    const fc = 8.176 * Math.pow(2, g[8] / 1200);
    const useLP = g[8] < 13000;
    let lp = null;
    if (useLP) {
      const Q = Math.pow(10, Math.max(0, g[9]) / 200) * 0.707;
      const w = 2 * Math.PI * Math.min(fc, SR * 0.45) / SR, al = Math.sin(w) / (2 * Q), cw = Math.cos(w);
      const a0 = 1 + al;
      lp = { b0: (1 - cw) / 2 / a0, b1: (1 - cw) / a0, b2: (1 - cw) / 2 / a0, a1: -2 * cw / a0, a2: (1 - al) / a0, x1: 0, x2: 0, y1: 0, y2: 0 };
    }
    // opts.cut: an extra lowpass for this note (a palm-muted guitar)
    let lp2 = null;
    if (opts.cut) {
      const w = 2 * Math.PI * Math.min(opts.cut, SR * 0.45) / SR, al = Math.sin(w) / (2 * 0.8), cw = Math.cos(w);
      const a0 = 1 + al;
      lp2 = { b0: (1 - cw) / 2 / a0, b1: (1 - cw) / a0, b2: (1 - cw) / 2 / a0, a1: -2 * cw / a0, a2: (1 - al) / a0, x1: 0, x2: 0, y1: 0, y2: 0 };
    }
    // pan -> mono: keep both sides of a stereo pair at full level, halve each
    const stereo = s.type === 2 || s.type === 4;
    const gain = (opts.vol ?? 1) * atten * velGain * (stereo ? 0.6 : 1);

    const i0 = Math.round(t0 * SR);
    const holdN = Math.round(dur * SR);
    const total = holdN + Math.round(rel * SR * 1.2);
    let pos = start, envAtRelease = 0, env = 0, r = ratio;
    for (let n = 0; n < total; n++) {
      const i = i0 + n;
      if (i >= buf.length) break;
      if ((n & 15) === 0) {           // control rate: envelope and pitch every 16 samples
        const t = n / SR;
        // volume envelope (attack linear, decay/release exponential in dB)
        const te = Math.min(t, dur);
        if (te < delay) env = 0;
        else if (te < delay + att) env = (te - delay) / att;
        else if (te < delay + att + hold) env = 1;
        else { const x = (te - delay - att - hold) / dec; env = x >= 1 ? sus : Math.pow(10, Math.log10(sus) * x); }
        if (n < holdN) envAtRelease = env;
        else { const x = (t - dur) / rel; env = envAtRelease * Math.pow(10, -4.8 * x); if (env < 1e-4) break; }
        // pitch with vibrato
        r = ratio;
        const va = vibAmt + (opts.vib || 0) * Math.min(1, Math.max(0, (t - 0.15) / 0.3));
        if (va && t > vibDelay) r *= Math.pow(2, va * Math.sin(2 * Math.PI * (opts.vibRate || vibF) * (t - vibDelay)) / 1200);
        if (opts.glideFrom !== undefined) r *= Math.pow(2, (opts.glideFrom - key) * 100 * Math.max(0, 1 - t / 0.06) / 1200);
      }
      // 4-point Hermite interpolation
      const ip = Math.floor(pos), fr = pos - ip;
      const xm1 = sf.data[ip - 1] || 0, x0 = sf.data[ip], x1 = sf.data[ip + 1] || 0, x2 = sf.data[ip + 2] || 0;
      const c1 = 0.5 * (x1 - xm1), c2 = xm1 - 2.5 * x0 + 2 * x1 - 0.5 * x2, c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
      let v = (((c3 * fr + c2) * fr + c1) * fr + x0) / 32768;
      if (lp) { const y = lp.b0 * v + lp.b1 * lp.x1 + lp.b2 * lp.x2 - lp.a1 * lp.y1 - lp.a2 * lp.y2; lp.x2 = lp.x1; lp.x1 = v; lp.y2 = lp.y1; lp.y1 = y; v = y; }
      if (lp2) { const y = lp2.b0 * v + lp2.b1 * lp2.x1 + lp2.b2 * lp2.x2 - lp2.a1 * lp2.y1 - lp2.a2 * lp2.y2; lp2.x2 = lp2.x1; lp2.x1 = v; lp2.y2 = lp2.y1; lp2.y1 = y; v = y; }
      buf[i] += v * env * gain;
      pos += r;
      if (loop && (g.mode === 1 || n < holdN)) { while (pos >= le) pos -= (le - ls); }
      else if (pos >= end - 2) break;
    }
  }
}

module.exports = { readSF2, renderNote, voicesFor };
