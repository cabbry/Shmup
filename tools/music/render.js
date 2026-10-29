// render.js -- renders the game's songs into data/data/music: menu.mp3 and one
// per act, mono 128 kbit/s MP3, at the loudness of the 2009 files they
// replaced (-19.3 LUFS for the menu, -17 for the acts; see docs/music.md).
// Usage: node render.js <FluidR3_GM.sf2> <scratch dir> [menu dawn hope dusk rain final]
// Needs ffmpeg on the PATH.
'use strict';
const fs = require('fs');
const path = require('path');
const { execFileSync, spawnSync } = require('child_process');
const { render, readSF2 } = require('./engine2');
const tracks = require('./tracks2');

const [bankPath, scratch, ...names] = process.argv.slice(2);
const outDir = path.join(__dirname, '..', '..', 'data', 'data', 'music');
const sf = readSF2(bankPath);
for (const t of tracks) {
  const name = t.act.toLowerCase();
  if (names.length && !names.includes(name)) continue;
  const wav = path.join(scratch, `${name}.wav`), mp3 = path.join(outDir, `${name}.mp3`);
  const info = render(t, wav, sf);
  const res = spawnSync('ffmpeg', ['-hide_banner', '-nostats', '-i', wav, '-af', 'ebur128=framelog=quiet', '-f', 'null', '-'], { encoding: 'utf8' });
  const I = parseFloat(/I:\s+(-?[\d.]+) LUFS/.exec(res.stderr)[1]);
  execFileSync('ffmpeg', ['-v', 'error', '-y', '-i', wav, '-af', `volume=${((t.lufs ?? -17) - I).toFixed(2)}dB`, '-ac', '1', '-ar', '44100', '-b:a', '128k',
    '-metadata', `title=${t.act}`, '-metadata', 'artist=SHMUP Reborn', mp3]);
  fs.unlinkSync(wav);
  console.log(`${name}: ${(info.seconds / 60 | 0)}:${String(Math.round(info.seconds % 60)).padStart(2, '0')}, ${info.bars} bars`);
}
