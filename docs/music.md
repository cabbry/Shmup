# The soundtrack

| file | plays | length |
|---|---|---|
| `menu.mp3` | the home screen and the tutorials | 2:00 |
| `dawn.mp3` | act I, Dawn, and the Demo (Dawn playing itself) | 2:46 |
| `hope.mp3` | act II, Hope | 2:44 |
| `dusk.mp3` | act III, Dusk | 2:58 |
| `rain.mp3` | act IV, Rain | 2:53 |
| `final.mp3` | the Final | 3:11 |

**One song per act.** A scene's track starts when it differs from the one
playing (`dEngine_LoadScene`), so reaching the next act starts its song from
its opening, and replaying the same act lets the song play on. Every song
**loops**: no scene declares an `alternate`, and with none the player loops
forever (`music_av.m`), so an act cannot outlast its music.

## Where they come from

They replace the two 2009 tracks, `UNREALPM.mp3` and `UNREALTH.mp3`: Future
Crew's music, whose rights for this release could not be confirmed. The new
songs were composed for it, note by note, in the scripts of
[`tools/music`](../tools/music):

- a hand-written sheet per song in `tracks2.js`: key, tempo, chords, riff,
  melodic motifs, instruments, drum grooves, its own opening;
- `engine2.js` composes the form and every part from it;
- `sf2.js` plays the notes on the recorded instruments of the FluidR3 GM
  SoundFont (Frank Wen, MIT licence, shipped by Debian as
  `fluid-soundfont-gm`);
- `render2.js` writes each song as a mono 128 kbit/s MP3.

No sample of any other music is used. Every random choice in the scripts is
seeded, so the same scripts and bank render the same audio again.

## Loudness

The songs are brought to the loudness of the files they replace, so the sound
effects keep their balance against the music: -19 LUFS for the menu song
(UNREALTH's), -17 LUFS for the acts (UNREALPM's).

## The old tracks' silence (round 60, for the record)

UNREALPM and UNREALTH shipped in 2009 with digital silence at their end (37 s
and 9 s); once set to loop, that tail read as "the music stopped". They were
cut with a stream copy in round 60. The new songs end on their last bar: the
renderer folds the reverb tail back onto the opening, so the loop has no gap.
