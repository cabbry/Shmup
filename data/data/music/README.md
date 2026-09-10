# The two soundtracks

| file | plays | length |
|---|---|---|
| `UNREALPM.mp3` | the whole game -- cued at 0:00 for an odd act, 2:02 for an even one | 6:07 |
| `UNREALTH.mp3` | the home screen and the tutorials, and nowhere else | 1:31 |

Both **loop**: no scene declares an `alternate`, and with none declared the
player is set to loop forever (`music_av.m`), so a run cannot outlast its
music however long it takes.

## Why they were trimmed (round 60)

They shipped in 2009 with **digital silence at the end** -- 37 seconds on
UNREALPM (music to 6:07, file to 6:45) and 9 on UNREALTH. Nothing noticed
while the tracks were played once and the act ended first. Once they were set
to loop, that tail became a 37-second hole before the music came back, which
reads as "the music stopped": *"soit il y a un grand blanc a la fin
UNREALPM, soit elle ne redemarre pas"* (2026-09-10). Measured with
`ffmpeg -af volumedetect` at -91 dB, which is not a fade -- it is nothing.

The tails were cut with a **stream copy**, so nothing was re-encoded and no
quality was lost:

    ffmpeg -i UNREALPM.mp3 -t 367.0 -c copy out.mp3
    ffmpeg -i UNREALTH.mp3 -t 91.2  -c copy -map_metadata -1 -write_xing 0 -id3v2_version 0 out.mp3

The kept audio is **bit-identical** to the original, verified by decoding both
to raw PCM and comparing hashes. (The extra flags on UNREALTH: it shipped as a
bare frame stream with no ID3 tag, and letting ffmpeg add one shifted the
decode by 24 ms. UNREALPM already had a tag, so its default framing matches.)
