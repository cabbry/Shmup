// mixer.js -- shared by the pieces: parts on a step grid, a reverb send,
// a bus compressor, a soft limiter, a seamless loop, a mono 16-bit WAV.
'use strict';
const fs = require('fs');
const { renderNote } = require('./sf2');

// gapAt/gap: from bar `gapAt` on, everything comes `gap` seconds later -- a
// stop in the music, so a section can start on an exact second.
// loop false: the piece ends and its tail rings out after it (pieces played one
// after the other) instead of folding back onto the start.
function song(sf, { bpm, bars, tail = 4, SR = 44100, gapAt = Infinity, gap = 0, loop = true }) {
  const STEP = 60 / bpm / 4;
  const N = Math.round((bars * 16 * STEP + (gapAt < bars ? gap : 0)) * SR), NT = N + Math.round(tail * SR);
  const dry = new Float32Array(NT), send = new Float32Array(NT), tmp = new Float32Array(NT);
  const bt = (bar, step) => (bar * 16 + step) * STEP + (bar >= gapAt ? gap : 0);

  // A part: its notes go to a scratch buffer, optionally driven (distortion),
  // optionally brought to a target loudness (rms over the samples where it
  // plays), then mixed once into the dry bus and the reverb send.
  // opts.humanize {t: seconds, v: velocity}: a player's small, repeatable
  // imprecision (seeded per part, so a render is always the same).
  function part(vol, fx, fn, opts = {}) {
    tmp.fill(0);
    const h = opts.humanize;
    let seed = opts.seed ?? 12345;
    const rnd = () => { seed = (seed * 1664525 + 1013904223) >>> 0; return seed / 4294967296 * 2 - 1; };
    fn((prog, key, vel, bar, step, len, o) => {
      const bank = typeof prog === 'string' ? 128 : 0;
      const p = typeof prog === 'string' ? +prog.split(':')[1] : prog;
      if (vel <= 0) return;
      const t = Math.max(0, bt(bar, step) + (h ? rnd() * h.t : 0));
      const v = h ? vel + rnd() * h.v : vel;
      renderNote(sf, tmp, SR, bank, p, key, Math.max(1, Math.min(127, v)), t, len * STEP, o);
    });
    if (opts.drive) {
      let pk = 1e-9; for (let i = 0; i < NT; i++) pk = Math.max(pk, Math.abs(tmp[i]));
      const d = opts.drive, norm = Math.tanh(d);
      for (let i = 0; i < NT; i++) tmp[i] = Math.tanh(tmp[i] / pk * d) / norm * pk;
    }
    if (opts.target) {
      let pk = 0; for (let i = 0; i < NT; i++) pk = Math.max(pk, Math.abs(tmp[i]));
      if (pk > 0) {
        let e2 = 0, n2 = 0; const th = pk * 0.01;
        for (let i = 0; i < NT; i++) { const v = tmp[i]; if (v > th || v < -th) { e2 += v * v; n2++; } }
        vol *= opts.target / Math.sqrt(e2 / n2);
      }
    }
    let e = 0, n = 0;
    for (let i = 0; i < NT; i++) { const v = tmp[i]; if (v) { dry[i] += v * vol; send[i] += v * vol * fx; e += v * v * vol * vol; n++; } }
    if (opts.name) console.log(`  ${opts.name.padEnd(10)} rms while playing ${n ? Math.sqrt(e / n).toFixed(4) : '-'}`);
  }

  function finish(path, { room = 0.84, damp = 0.25, wetGain = 1, comp = 0 } = {}) {
    // mono Freeverb: 8 combs, 4 allpasses
    const wet = new Float32Array(NT);
    const combs = [1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617].map(n => ({ b: new Float32Array(n), w: 0, lp: 0 }));
    const aps = [556, 441, 341, 225].map(n => ({ b: new Float32Array(n), w: 0 }));
    for (let i = 0; i < NT; i++) {
      const x = send[i] * 0.015;
      let s = 0;
      for (const c of combs) { const y = c.b[c.w]; c.lp = y * (1 - damp) + c.lp * damp; c.b[c.w] = x + c.lp * room; c.w = (c.w + 1) % c.b.length; s += y; }
      for (const a of aps) { const y = a.b[a.w]; a.b[a.w] = s + y * 0.5; a.w = (a.w + 1) % a.b.length; s = y - s; }
      wet[i] = s;
    }
    // fold the tail onto the start: the file loops without a seam -- or keep
    // it after the end when the piece is followed by another
    const M = loop ? N : NT;
    const mix = new Float32Array(M);
    for (let i = 0; i < NT; i++) mix[i % M] += dry[i] + wet[i] * wetGain;
    // DC blocker, run twice round the loop so it starts settled
    { let x1 = 0, y1 = 0; for (let p = 0; p < 2; p++) for (let i = 0; i < N; i++) { const y = mix[i] - x1 + 0.9995 * y1; x1 = mix[i]; y1 = y; if (p) mix[i] = y; } }
    let rms = 0; for (let i = 0; i < N; i++) rms += mix[i] * mix[i]; rms = Math.sqrt(rms / N);
    for (let i = 0; i < N; i++) mix[i] *= 0.12 / rms;
    // bus compressor (feed-forward, 5 ms / 120 ms), two passes round the loop
    if (comp) {
      const atk = Math.exp(-1 / (0.005 * SR)), rel = Math.exp(-1 / (0.12 * SR)), thr = 0.12, ratio = comp;
      let env = 0;
      for (let p = 0; p < 2; p++) for (let i = 0; i < N; i++) {
        const a = Math.abs(mix[i]);
        env = a > env ? atk * env + (1 - atk) * a : rel * env + (1 - rel) * a;
        if (p) { const g = env > thr ? Math.pow(thr / env, 1 - 1 / ratio) : 1; mix[i] *= g; }
      }
      let r2 = 0; for (let i = 0; i < N; i++) r2 += mix[i] * mix[i]; r2 = Math.sqrt(r2 / N);
      for (let i = 0; i < N; i++) mix[i] *= 0.12 / r2;
    }
    let peak = 0;
    for (let i = 0; i < N; i++) { mix[i] = Math.tanh(mix[i]); peak = Math.max(peak, Math.abs(mix[i])); }
    const gain = 0.89 / peak;
    const out = Buffer.alloc(44 + N * 2);
    out.write('RIFF', 0); out.writeUInt32LE(36 + N * 2, 4); out.write('WAVE', 8); out.write('fmt ', 12);
    out.writeUInt32LE(16, 16); out.writeUInt16LE(1, 20); out.writeUInt16LE(1, 22); out.writeUInt32LE(SR, 24);
    out.writeUInt32LE(SR * 2, 28); out.writeUInt16LE(2, 32); out.writeUInt16LE(16, 34); out.write('data', 36); out.writeUInt32LE(N * 2, 40);
    for (let i = 0; i < N; i++) {
      const v = mix[i] * gain;
      if (!Number.isFinite(v)) throw new Error('NaN at ' + i);
      out.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(v * 32767))), 44 + i * 2);
    }
    fs.writeFileSync(path, out);
    console.log(`wrote ${path}: ${(N / SR).toFixed(1)} s, ${bars} bars at ${bpm} BPM, peak after limiter ${peak.toFixed(3)}`);
  }
  return { part, finish, bt, STEP };
}

module.exports = { song };
