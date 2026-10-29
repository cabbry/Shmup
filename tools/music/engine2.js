// engine2.js -- the second round: one song per act, recorded instruments only.
//
// Same composing as engine.js (a short hand-written spec, phrases built from
// 2-bar motifs, every part voiced here), with what the first round lacked:
//   - one piece, not two halves: intro, riff, A, B, a bridge in another key,
//     a half-time break, A and B a step up, an outro that leads back to the
//     intro -- it loops for as long as the act lasts;
//   - no synth waves: the melody is a trumpet, horns, strings, a piano, a
//     guitar or a choir; the arpeggios are a string, piano or harp ostinato;
//   - played, not sequenced: every part has a small seeded imprecision of time
//     and velocity, the rhythm guitar is two takes a few cents apart (a
//     studio double), muted notes are filtered like a palm mute.
'use strict';
const { readSF2 } = require('./sf2');
const { song } = require('./mixer');

const MODES = {
  aeolian:  [0, 2, 3, 5, 7, 8, 10],
  phrygian: [0, 1, 3, 5, 7, 8, 10],
  dorian:   [0, 2, 3, 5, 7, 9, 10],
  harmonic: [0, 2, 3, 5, 7, 8, 11],
  phrygdom: [0, 1, 4, 5, 7, 8, 10],
};
const PC = { C: 0, 'C#': 1, D: 2, 'D#': 3, Eb: 3, E: 4, F: 5, 'F#': 6, G: 7, 'G#': 8, Ab: 8, A: 9, Bb: 10, B: 11 };
const GALLOP = [0, 2, 3, 4, 6, 7, 8, 10, 11, 12, 14, 15];

function semi(scale, deg) {
  const base = Math.floor(deg), sharp = deg - base >= 0.5 ? 1 : 0;
  const o = Math.floor(base / 7);
  return o * 12 + scale[base - o * 7] + sharp;
}
const parseChord = c => typeof c === 'number' ? { d: c, maj: false } : { d: parseInt(c, 10), maj: c.endsWith('^') };
const chordDegs = ch => [ch.d, ch.d + 2 + (ch.maj ? 0.5 : 0), ch.d + 4];
const mod7 = x => ((Math.floor(x) % 7) + 7) % 7;

function nearestTone(deg, ch) {
  let best = deg, bd = 1e9;
  for (const t of chordDegs(ch)) for (let k = -3; k <= 3; k++) {
    const c = t + 7 * k, dd = Math.abs(Math.floor(c) - Math.floor(deg));
    if (dd < bd) { bd = dd; best = c; }
  }
  return best;
}

// 8 bars of melody from a 2-bar motif (see engine.js)
function phrase(motif, prog) {
  const ch = prog.map(parseChord), cd = ch.map(c => c.d);
  const wrap = x => { x = ((x % 7) + 7) % 7; return x > 3 ? x - 7 : x; };
  const m0 = motif.filter(n => n[1] < 16), m1 = motif.filter(n => n[1] >= 16);
  const out = [];
  const put = (notes, shift, barOff) => notes.forEach(([d, s, l]) => out.push([d + shift, barOff * 16 + s, l]));
  put(motif, 0, 0);
  put(motif, wrap(cd[2] - cd[0]), 2);
  const s4 = wrap(cd[4] - cd[0]);
  put(m0, s4, 4);
  const pivot = (m1[0] || m0[0])[0];
  put(m1.map(([d, s, l]) => [2 * pivot - d, s, l]), s4, 4);
  put(m0, wrap(cd[6] - cd[0]), 6);
  const last = out[out.length - 1][0];
  const target = nearestTone(last, ch[7]);
  const dir = motif[0][0] > target ? 1 : -1;
  out.push([target, 112, 12], [target + dir, 124, 2], [target + 2 * dir, 126, 2]);
  for (const n of out) {
    const bar = Math.min(7, Math.floor(n[1] / 16)), st = n[1] % 16;
    if (st === 0 || st === 8) n[0] = nearestTone(n[0], ch[bar]);
  }
  return out;
}

function render(spec, outPath, sf) {
  const scale = MODES[spec.mode], pc = PC[spec.key];
  const V = spec.V ?? '4^';

  // ---- the form: one piece that loops ----
  // Each song opens its own way (spec.intro): a trumpet fanfare, a solo piano,
  // war drums, the rain on a piano, an organ. The outro either rings out on
  // the dominant (back to a quiet opening) or stabs straight back in.
  const introType = spec.intro, IB = spec.introBars ?? 4;
  const secs = (spec.form || ['riff:8', 'A:16', 'B:16', 'C:16', 'break:8', 'Aup:16', 'Bup:16']).map(s => { const [n, l] = s.split(':'); return [n, +l]; });
  secs.unshift(['intro', IB]);
  secs.push(['outro', 4]);
  const introProg = { fanfare: [0, 0, 5, V], piano: spec.progA.slice(0, 4), wardrums: [0, 0, 5, 6], rain: [0, 0, 5, 5, 3, 3, 4, 4], organ: [0, 0, 5, 5, 3, 3, 4, 4], clean: [0, 5, 2, V] };
  const progOf = { intro: spec.progIntro || introProg[introType], A: spec.progA, Aup: spec.progA, B: spec.progB, Bup: spec.progB,
    C: spec.progC, break: spec.progBrk, outro: spec.progOutro || [5, 6, V, V] };
  const ringOut = spec.outro !== 'stabs';
  const tpOf = { C: spec.cShift, Aup: spec.mod, Bup: spec.mod };
  const classOf = { riff: 'riff', A: 'A', Aup: 'up', B: 'B', Bup: 'climax', C: 'C', break: 'break' };
  const bars = [];
  for (const [sec, n] of secs) {
    const start = bars.length;
    for (let i = 0; i < n; i++) {
      const p = progOf[sec];
      bars.push({ sec, i, n, start, cls: classOf[sec], tp: tpOf[sec] || 0, ch: parseChord(p ? p[i % p.length] : 0) });
    }
  }
  const BARS = bars.length;
  const { part, finish } = song(sf, { bpm: spec.bpm, bars: BARS, tail: 4, loop: true });

  const gT = 40 + ((pc - 4 + 12) % 12);
  const bT = gT - 12;
  const fold = (k, lo) => { while (k < lo) k += 12; while (k >= lo + 12) k -= 12; return k; };
  const rootSemi = b => b.tp + semi(scale, b.ch.d);
  const gRoot = b => fold(gT + rootSemi(b), 38);
  const bRoot = b => fold(bT + rootSemi(b), 28);
  const triad = b => chordDegs(b.ch).map(d => fold(60 + pc + b.tp + semi(scale, d), 57));
  const melodic = b => ['A', 'B', 'C', 'Aup', 'Bup'].includes(b.sec);
  const isRiff = b => b.cls === 'riff';
  const style = (map, b, dflt) => (map && map[b.cls]) || dflt;

  // ---- melody ----
  const progKey = { A: 'progA', Aup: 'progA', B: 'progB', Bup: 'progB', C: 'progC' };
  const phrases = {}, centre = {};
  for (const k of ['A', 'B', 'C']) {
    const ph = phrase(spec['motif' + k], spec['prog' + k]);
    phrases[k] = ph;
    const avg = ph.reduce((a, n) => a + semi(scale, n[0]), 0) / ph.length + 60 + pc;
    centre[k] = 12 * Math.round(((spec.leadCentre ?? 76) - avg) / 12);
  }
  const melody = [];
  for (const b of bars) {
    if (!melodic(b) || b.i % 8) continue;
    const key = b.sec[0];
    const prog = spec[progKey[b.sec]].map(parseChord);
    let up = b.sec === 'Bup' && b.i >= 8 ? 12 : 0;      // the last 8 bars soar an octave
    const ph = phrases[key];
    const top = Math.max(...ph.map(n => semi(scale, n[0]))) + 60 + pc + centre[key] + b.tp + up;
    if (top > (spec.leadTop ?? 96)) up -= 12;
    for (const [deg, s, l] of ph) {
      const bar = b.start + b.i + Math.floor(s / 16);
      const chord = prog[Math.floor(s / 16) % 8];
      const raise = d => (chord.maj && d === Math.floor(d) && mod7(d) === mod7(chord.d + 2)) ? d + 0.5 : d;
      const m = semi(scale, raise(deg)) + 60 + pc + centre[key] + b.tp + up;
      const h = semi(scale, raise(deg - 2)) + 60 + pc + centre[key] + b.tp + up;
      melody.push({ bar, step: s % 16, len: l, midi: m, harm: h, second: b.i >= 8 });
    }
  }

  // ---- the openings' own lines, on the melody instrument ----
  // degrees in the melody's register, every note pulled onto its bar's chord
  const introNotes = [];
  const introLine = (bar, notes) => {
    const b = bars[bar];
    for (const [deg, s, l] of notes) {
      let d = nearestTone(deg, b.ch);
      if (b.ch.maj && d === Math.floor(d) && mod7(d) === mod7(b.ch.d + 2)) d += 0.5;
      introNotes.push({ bar, step: s, len: l, midi: semi(scale, d) + 60 + pc + centre.A, vel: 112 });
    }
  };
  if (introType === 'fanfare') {
    const call = [[0, 0, 1], [0, 1, 1], [0, 2, 2], [4, 4, 4], [2, 8, 2], [4, 10, 2], [7, 12, 4]];
    introLine(0, call); introLine(1, [[7, 0, 2], [6, 2, 2], [7, 4, 4], [4, 8, 8]]);
    introLine(2, call.map(([d, s, l]) => [d + 2, s, l])); introLine(3, [[6, 0, 4], [4, 4, 4], [1, 8, 8]]);
  } else if (introType === 'wardrums') {
    introLine(IB - 2, [[0, 0, 6], [4, 6, 2], [7, 8, 8]]); introLine(IB - 1, [[6, 0, 8], [4, 8, 8]]);
  } else if (introType === 'piano') {
    for (let x = 0; x < IB; x++) {
      const t = triad(bars[x]).map(k => k + 12), lh = fold(48 + pc + rootSemi(bars[x]), 43);
      const seq = [t[0], t[2], t[1], t[2], t[0] + 12, t[2], t[1], t[2]];
      for (let s = 0; s < 16; s += 2) introNotes.push({ bar: x, step: s, len: 2, midi: seq[s / 2], vel: s % 4 ? 82 : 96 });
      for (const s of [0, 8]) introNotes.push({ bar: x, step: s, len: 8, midi: lh, vel: 90 });
    }
  } else if (introType === 'rain') {
    for (let x = IB / 2; x < IB; x++) {
      const t = triad(bars[x]);
      for (let s = 0; s < 16; s++) introNotes.push({ bar: x, step: s, len: 1, midi: t[2] + 12, vel: 50 + ((x - IB / 2) * 16 + s) * 0.8 });
    }
  }

  const RIFF_END = spec.riffEnd || [[0, 0, 4, 1], [4, -2, 4, 1], [8, -4, 4, 1], [12, -5, 4, 1]];
  const riffEvents = b => (b.i === b.n - 1 ? RIFF_END : spec.riff[b.i % 2]);

  // ---- rhythm guitar: two takes, a few cents apart ----
  const G = spec.guitar;
  part(1, 0.1, note => {
    for (const bend of [-6, 6]) {
      const open = { bend }, muted = { bend, cut: spec.muteCut ?? 1500 };
      const pc5 = (r, v, b, s, l) => { note(G, r, v, b, s, l, open); note(G, r + 7, v, b, s, l, open); note(G, r + 12, v - 10, b, s, l, open); };
      const mute = (r, v, b, s) => { note(G, r, v, b, s, 0.8, muted); note(G, r + 7, v - 4, b, s, 0.8, muted); };
      bars.forEach((b, x) => {
        if (isRiff(b)) {
          for (const [s, o, l, acc] of riffEvents(b)) { const r = fold(gT + b.tp + o, 33); acc ? pc5(r, 120, x, s, l) : mute(r, 100, x, s); }
          return;
        }
        const r = gRoot(b);
        if (b.sec === 'intro') {
          // the fanfare's guitars come in held under the second call; the organ's, as one ominous chord
          if ((introType === 'fanfare' || introType === 'organ') && b.i === IB - 2) pc5(r, introType === 'organ' ? 120 : 96, x, 0, 32);
          return;
        }
        if (b.sec === 'outro') {
          if (ringOut) { if (b.i < 2) for (const s of [0, 3, 6, 10]) pc5(r, 122, x, s, 2); else if (b.i === 2) pc5(r, 122, x, 0, 30); }
          else for (const s of (b.i < 3 ? [0, 3, 6, 10] : [0])) pc5(r, 122, x, s, b.i < 3 ? 2 : 14);
          return;
        }
        if (b.sec === 'break') { if (b.i % 4 === 0) pc5(r, 100, x, 0, 60); return; }
        const st = style(spec.gtr, b, 'gallop');
        if (st === 'gallop') for (const s of GALLOP) (s === 0 || s === 8) ? pc5(r, 118, x, s, 1.5) : mute(r, 98, x, s);
        else if (st === 'chug8') for (let s = 0; s < 16; s += 2) s % 8 === 0 ? pc5(r, 118, x, s, 1.5) : mute(r, 100, x, s);
        else if (st === 'chug16') for (let s = 0; s < 16; s++) s % 4 === 0 ? pc5(r, 116, x, s, 1) : mute(r, 92, x, s);
        else if (st === 'chords8') for (let s = 0; s < 16; s += 2) pc5(r, s % 4 ? 105 : 118, x, s, 1.7);
        else if (st === 'ring') { if (b.i === 0 || b.ch.d !== bars[x - 1].ch.d) pc5(r, 108, x, 0, 15.5); }
        else if (st === 'stabs') for (const s of [0, 3, 6, 10]) pc5(r, 115, x, s, 1.5);
        else if (st === 'offbeat') for (const s of [2, 6, 10, 14]) pc5(r, 112, x, s, 1.5);
      });
    }
  }, { drive: spec.gDrive ?? 2.2, target: 0.30, humanize: { t: 0.008, v: 7 }, seed: 11 });

  // ---- bass ----
  part(1, 0.05, note => {
    const B = spec.bass;
    bars.forEach((b, x) => {
      if (isRiff(b)) { for (const [s, o, l] of riffEvents(b)) note(B, fold(bT + b.tp + o, 28), 115, x, s, Math.max(l, 0.9)); return; }
      const r = bRoot(b);
      if (b.sec === 'intro') {
        if (introType === 'fanfare' && b.i === IB - 2) note(B, r, 110, x, 0, 32);
        if (introType === 'piano' && b.i >= IB - 2) for (const [s, o] of [[0, 0], [3, 0], [6, 12], [8, 0], [10, 0], [11, 12], [14, 7]]) note(B, r + o, b.i === IB - 2 ? 92 : 108, x, s, 1.2);
        if ((introType === 'rain' || introType === 'organ') && b.i >= IB / 2 && b.i % 2 === 0) note(B, r, 105, x, 0, 32);
        if (introType === 'clean' && b.i === IB - 1) note(B, r, 100, x, 0, 16);
        return;
      }
      if (b.sec === 'outro') {
        if (ringOut) { if (b.i < 2) for (const s of [0, 3, 6, 10]) note(B, r, 120, x, s, 2); else if (b.i === 2) note(B, r, 122, x, 0, 30); }
        else for (const s of (b.i < 3 ? [0, 3, 6, 10] : [0])) note(B, r, 120, x, s, b.i < 3 ? 2 : 14);
        return;
      }
      if (b.sec === 'break') { for (let s = 0; s < 16; s += 4) note(B, r, s ? 90 : 115, x, s, 3.5); return; }
      const st = style(spec.bassStyle, b, 'gallop');
      if (st === 'gallop') for (const s of GALLOP) note(B, r, s % 8 ? 100 : 120, x, s, 0.9);
      else if (st === '8ths') for (let s = 0; s < 16; s += 2) note(B, r, s % 8 ? 102 : 120, x, s, 1.7);
      else if (st === 'octaves') for (let s = 0; s < 16; s += 2) note(B, s % 4 ? r + 12 : r, 112, x, s, 1.7);
      else if (st === '16ths') for (let s = 0; s < 16; s++) note(B, r, s % 4 ? 92 : 118, x, s, 0.9);
      else if (st === 'offbeat') for (const s of [0, 2, 6, 10, 14]) note(B, r, s ? 112 : 120, x, s, 1.6);
      else if (st === 'syncop') for (const [s, o] of [[0, 0], [3, 0], [6, 12], [8, 0], [10, 0], [11, 12], [14, 7]]) note(B, r + o, s % 8 ? 104 : 122, x, s, 1.2);
    });
  }, { drive: spec.bDrive ?? 1.2, target: 0.24, humanize: { t: 0.005, v: 6 }, seed: 22 });

  // ---- drums (an acoustic kit) ----
  part(1, 0.15, note => {
    const kit = spec.kit || 'kit:16';
    bars.forEach((b, x) => {
      const v = b.sec === 'break' ? 0.85 : 1;
      const K = (s, w = 120) => note(kit, 36, w * v, x, s, 4), S = (s, w = 118) => note(kit, 38, w * v, x, s, 4);
      const H = (s, w = 80) => note(kit, 42, w * v, x, s, 2), RD = (s, w = 85) => note(kit, 51, w * v, x, s, 3);
      const OH = (s, w = 85) => note(kit, 46, w * v, x, s, 3), CR = (s = 0, w = 115) => note(kit, 49, w, x, s, 24);
      const fill = from => { const toms = [50, 48, 47, 45, 43, 41]; for (let s = from; s < 16; s++) note(kit, toms[Math.min(5, Math.floor((s - from) * 6 / (16 - from)))], 108, x, s, 3); };
      if (b.sec === 'intro') {
        const last = b.i === IB - 1, second = b.i === IB - 2;
        if (introType === 'fanfare') {             // a marching snare brings the band in
          if (second) for (let s = 0; s < 16; s += 2) S(s, 50 + s * 2.5);
          if (last) for (let s = 0; s < 16; s++) { S(s, 70 + s * 3.2); if (s % 4 === 0) K(s, 112); }
        } else if (introType === 'piano') {        // hats under the piano, a short fill
          if (b.i >= IB - 2) for (let s = 0; s < 16; s += 2) H(s, s % 4 ? 55 : 70);
          if (last) { K(0, 105); for (let s = 12; s < 16; s++) S(s, 85 + (s - 12) * 8); }
        } else if (introType === 'wardrums') {     // floor and low toms, a war beat
          for (const s of [0, 3, 6, 8, 11, 14]) { note(kit, 41, s % 8 ? 98 : 118, x, s, 4); if (s % 8 === 0) K(s, 110); }
          for (const s of [4, 12]) note(kit, 45, 108, x, s, 4);
          if (second) CR(0, 105);
          if (last) fill(8);
        } else if (introType === 'rain') {         // cymbal swells, then the double kick climbs
          if (b.i === 2 || b.i === IB - 2) CR(0, 82);
          if (b.i >= IB - 2) for (let s = 0; s < 16; s++) K(s, 50 + ((b.i - IB + 2) * 16 + s) * 2.2);
          if (last) for (let s = 8; s < 16; s++) S(s, 80 + (s - 8) * 5);
        } else if (introType === 'organ') {        // a heartbeat
          if (b.i >= 2) { K(0, 112); K(3, 86); }
          if (last) for (let s = 8; s < 16; s++) S(s, 70 + (s - 8) * 6.5);
        } else if (introType === 'clean') {        // the clean guitar alone, then a tom run in
          if (last) { K(8, 110); fill(8); }
        }
        return;
      }
      if (b.sec === 'outro') {
        if (b.i < 2 || (!ringOut && b.i < 3)) { for (const s of [0, 3, 6, 10]) { K(s); S(s, 112); } CR(0); }
        else if (ringOut) { if (b.i === 2) { K(0, 125); S(0, 120); CR(0, 122); } }
        else { K(0); CR(0); for (let s = 4; s < 16; s++) S(s, 60 + (s - 4) * 5.5); }
        return;
      }
      const st = style(spec.drums, b, 'rock');
      switch (st) {
        case 'rock': K(0); K(8, 112); K(10, 100); S(4); S(12); for (let s = 0; s < 16; s += 2) H(s, s % 4 ? 70 : 90); break;
        case 'rock2': K(0); K(3, 100); K(6, 100); K(8, 115); K(10, 100); K(11, 95); S(4); S(12); for (let s = 0; s < 16; s += 2) H(s, s % 4 ? 72 : 92); break;
        case 'gallop': for (const s of [0, 2, 3, 8, 10, 11]) K(s, s % 8 ? 104 : 120); S(4); S(12); for (let s = 0; s < 16; s += 2) RD(s, s % 4 ? 75 : 95); break;
        case 'double': for (let s = 0; s < 16; s++) K(s, s % 4 ? 88 : 120); S(4); S(12); for (let s = 0; s < 16; s += 2) RD(s, s % 4 ? 78 : 98); if (b.i % 2 === 1) note(kit, 52, 100, x, 8, 12); break;
        case 'four': for (const s of [0, 4, 8, 12]) K(s); S(4, 112); S(12, 112); for (const s of [2, 6, 10, 14]) OH(s, 85); for (let s = 1; s < 16; s += 2) H(s, 55); break;
        case 'dnb': K(0); K(10, 112); S(4); S(12); S(7, 55); S(9, 48); S(15, 60); for (let s = 0; s < 16; s++) H(s, s % 2 ? 58 : 82); break;
        case 'thrash': for (const s of [0, 4, 8, 12]) K(s); for (const s of [2, 6, 10, 14]) S(s, 112); for (let s = 0; s < 16; s += 4) note(kit, 51, 95 * v, x, s, 3); break;
        case 'half': K(0); K(10, 105); S(8); for (let s = 0; s < 16; s += 2) H(s, s % 4 ? 55 : 70); break;
        case 'march': K(0); K(8, 112); S(4); S(12); S(10, 70); S(14, 80); S(15, 92); for (let s = 0; s < 16; s += 2) H(s, 65); break;
      }
      if (b.i % 4 === 0 && b.sec !== 'break') CR(0, b.i === 0 ? 118 : 100);
      if (b.sec === 'break' && b.i >= b.n - 2) for (let s = 0; s < 16; s++) S(s, 55 + ((b.i - b.n + 2) * 16 + s) * 2.2);
      else if (b.i === b.n - 1) fill(8);
      else if (b.i % 4 === 3) fill(13);
    });
  }, { target: 0.38, humanize: { t: 0.004, v: 8 }, seed: 33 });

  // ---- the melody and its double (an octave or a third) ----
  part(1, 0.32, note => {
    for (const n of melody) note(spec.lead, n.midi, 112, n.bar, n.step, n.len * (spec.legato ?? 0.95), { vib: spec.vib ?? 10, vibRate: 5.5 });
    for (const n of introNotes) note(spec.lead, n.midi, n.vel, n.bar, n.step, n.len * 0.95, { vib: spec.vib ?? 10, vibRate: 5.5 });
  }, { drive: spec.leadDrive, target: 0.33, humanize: { t: 0.006, v: 6 }, seed: 44 });
  part(1, 0.32, note => {
    // spec.twin: a second lead a third below all along (two guitars in harmony)
    for (const n of melody) note(spec.lead2, n.second || spec.twin ? n.harm + (spec.harmShift ?? 0) : n.midi + (spec.lead2Shift ?? -12), 104, n.bar, n.step, n.len * 0.95, spec.twin ? { vib: spec.vib ?? 10, vibRate: 5.2 } : undefined);
  }, { target: spec.lead2Level ?? 0.13, humanize: { t: 0.007, v: 6 }, seed: 55 });

  // ---- held chords: strings or organ, choir ----
  const held = (inst, test, vel, octave) => note => {
    bars.forEach((b, x) => {
      if (!test(b)) return;
      if (x > 0 && test(bars[x - 1]) && bars[x - 1].sec === b.sec && bars[x - 1].ch.d === b.ch.d && bars[x - 1].tp === b.tp) return;
      let len = 1; while (x + len < BARS && test(bars[x + len]) && bars[x + len].sec === b.sec && bars[x + len].ch.d === b.ch.d && bars[x + len].tp === b.tp) len++;
      for (const k of triad(b)) note(inst, k + octave, vel(b), x, 0, len * 16 - 0.5);
    });
  };
  part(1, 0.4, note => {
    const inIntro = b => b.sec === 'intro' && introType !== 'piano';
    held(spec.pad, b => melodic(b) || inIntro(b) || ['break', 'outro'].includes(b.sec),
      b => b.sec === 'intro' ? (introType === 'organ' ? 96 : 72) : b.sec === 'break' ? 80 : 98, spec.padShift ?? 0)(note);
  }, { target: spec.padLevel ?? 0.14, humanize: { t: 0.01, v: 4 }, seed: 66 });
  part(1, 0.5, note => {
    const inIntro = b => b.sec === 'intro' && ((introType === 'wardrums' && b.i >= IB - 2) || (introType === 'organ' && b.i >= IB / 2));
    held(spec.choir, b => ['B', 'Bup', 'break'].includes(b.sec) || inIntro(b) || (spec.choirA && ['A', 'Aup', 'C'].includes(b.sec)),
      b => b.sec === 'break' || b.sec === 'intro' ? 84 : 104, 12)(note);
  }, { target: 0.13, seed: 77 });

  // ---- the ostinato: the bridge, the break, and under the last chorus ----
  part(1, 0.4, note => {
    const rate = spec.ostRate ?? 2;
    bars.forEach((b, x) => {
      const rainIntro = b.sec === 'intro' && introType === 'rain', cleanIntro = b.sec === 'intro' && introType === 'clean';
      if (!['C', 'break', 'Bup'].includes(b.sec) && !rainIntro && !cleanIntro) return;
      const t = triad(b), shift = spec.ostShift ?? 0;
      const seq = [t[0], t[1], t[2], t[0] + 12, t[1] + 12, t[2] + 12, t[0] + 12, t[2]];
      const vel = rainIntro ? 72 + b.i * 4 : cleanIntro ? 96 : b.sec === 'Bup' ? 80 : 100;
      for (let s = 0; s < 16; s += rate) note(spec.ost, seq[(s / rate) % 8] + shift, vel, x, s, rate * 0.85);
    });
  }, { target: spec.ostLevel ?? 0.11, humanize: { t: 0.005, v: 8 }, seed: 88 });

  // ---- orchestra hits (only the songs that ask for them) and timpani ----
  if (spec.hits) part(1, 0.3, note => {
    bars.forEach((b, x) => {
      const k = triad(b)[2];
      if (isRiff(b) && b.i % 4 === 0) note(55, fold(60 + pc + b.tp, 57), 112, x, 0, 3);
      else if (melodic(b) && b.i % 8 === 0) note(55, k, 112, x, 0, 3);
      else if (b.sec === 'outro' && b.i < (ringOut ? 2 : 3)) for (const s of [0, 3, 6, 10]) note(55, k, 118, x, s, 3);
    });
  }, { target: 0.08 });
  part(1, 0.3, note => {
    bars.forEach((b, x) => {
      const r = fold(bT + rootSemi(b), 38);
      if (b.sec === 'intro') {
        if (introType === 'wardrums') { note(47, r, 115, x, 0, 6); note(47, r, 100, x, 8, 6); }
        if (introType === 'rain' && (b.i === 1 || b.i === IB / 2 + 1)) for (let s = 0; s < 16; s++) note(47, r, 55 + s * 4, x, s, 1);
        if (introType === 'organ' && b.i >= IB / 2) note(47, r, 110, x, 0, 8);
      }
      if ((b.sec === 'Aup' || b.sec === 'Bup') && b.i % 8 === 0) note(47, r, 122, x, 0, 8);
      if (b.sec === 'break' && b.i === b.n - 1) for (let s = 0; s < 16; s++) note(47, fold(bT + semi(scale, 4), 38), 60 + s * 4, x, s, 1);
      if (spec.thunder && b.sec === 'C' && b.i % 4 === 0) for (let s = 0; s < 8; s++) note(47, r, 70 + s * 7, x, s, 1);
    });
  }, { target: 0.08, humanize: { t: 0.004, v: 5 }, seed: 99 });

  finish(outPath, { room: 0.82, wetGain: 0.85, comp: 3 });
  return { bars: BARS, seconds: BARS * 240 / spec.bpm, sections: secs.map(s => s.join(':')).join(' ') };
}

module.exports = { render, readSF2 };
