# tools/music -- the soundtrack, as source

The six songs in `data/data/music` are rendered by these scripts; see
[`docs/music.md`](../../docs/music.md) for what plays where.

| file | what it does |
|---|---|
| `tracks2.js` | one hand-written sheet per song: key, mode, tempo, chord progressions, the riff, three melodic motifs, the instruments, the drum grooves, the opening |
| `engine2.js` | composes a song from its sheet: the form, the melody's phrases from the motifs, every part (two takes of rhythm guitar, seeded human timing) |
| `mixer.js` | mixes the parts: reverb, bus compressor, limiter, a seamless loop |
| `sf2.js` | a small SoundFont 2 player: zones, loops, envelopes, vibrato, filters |
| `render.js` | renders the songs to `data/data/music/*.mp3` |

Node.js with no dependencies, and `ffmpeg` on the PATH. The instruments come
from the **FluidR3 GM** SoundFont (Frank Wen, MIT licence), which is not in
the repository (148 MB): Debian ships it as `fluid-soundfont-gm`
(`/usr/share/sounds/sf2/FluidR3_GM.sf2`).

    node render.js /path/to/FluidR3_GM.sf2 /tmp            # all six
    node render.js /path/to/FluidR3_GM.sf2 /tmp rain       # one

Every random choice is seeded: the same sheet renders the same song.
