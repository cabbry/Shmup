# The camera rails, as text

Each act flies a rail: a list of camera frames (time, position, orientation).
The game ships them **baked** -- `data/data/cameraPath/<name>.cp.cp2b`, with the
visibility set of the city precomputed -- and a shipped scene names that file.

These `.cp` files are the text sources of those rails. They are the authoring
format, so they stay out of the shipped data: only a development build reads a
`.cp` (the CI's boss smoke writes its own; `bake-boss-rail.yml` copies
`act3boss.cp` back into the data to bake it). The App Store build carries
neither the text rails nor the code that expands them (`camera.c`,
`CAM_LoadPath`).

| file | baked into | flown by |
|---|---|---|
| `act1.cp` | `act1.cp.cp2b` | Dawn, Dusk, the Demo |
| `act2.cp` | `act2.cp.cp2b` | Hope, Rain, Final |
| `intro.cp` | `intro.cp.cp2b` | the home screen |
| `tutorial.cp` | `tutorial.cp.cp2b` | the tutorial |
| `act3boss.cp` | -- | nothing: a U-turn rail for the boss act that made it black on device (see `final.scene`) |

Keep a rail **space separated**: the lexer does not split on commas, and a
comma rail parses to garbage.
