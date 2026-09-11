# Rain's formations

Act IV is written as a chain of reactions, and its hedgehog formations are
GENERATED rather than typed. `gen_walls.awk` emits the `spawnEnemy` lines for
the three corridors and the storm; splice them into the scene where the
`@@A@@`, `@@B@@`, `@@D@@`, `@@F1@@`, `@@F2@@`, `@@F3@@` markers sit.

    awk -f gen_walls.awk > blocks.txt     # the three corridors
    awk -f gen_storm.awk > storm.txt      # the storm, and its peak

## Why generate them

Every hull in a formation must travel the **same distance in the same ttl**.
The engine interpolates a quadratic Bezier from `startPos` through
`controlPoint` to `endPos` over `ttl` (fht.c, updateStraight), so a hull that
starts higher and ends at the same place simply moves faster -- and the shape
shears apart on the way down. Derived coordinates cannot make that mistake;
typed ones do it silently, and it only shows on a device.

## The shapes

| marker | shape | hulls |
|---|---|---|
| `@@A@@` | the long corridor: eight a side, a 0.64 lane, nearly two units tall | 16 |
| `@@B@@` | the same lane sliding 1.20 across as it falls | 10 |
| `@@D@@` | the chicane: right half, then left half 0.85 behind it | 16 |
| `@@F1@@` `@@F2@@` `@@F3@@` | the storm: straight, shallow diagonals, then hard crossing diagonals | 40 |

The corridors are `subType 2`: energy x40 and painted 0.2 grey. They are not
targets, they are architecture. The storm's hulls are ordinary and CHARGE at
ttl 5000 -- twice the speed of anything else in the act.

## The budget

`MAX_NUM_ENEMIES` is 64 and the pool does not grow: past it `ENE_Get` returns
a shared dummy and logs "Enemy pool exhausted", so an oversized wave does not
crash, it silently stops existing. Because a `cleared` trigger requires the
previous group to have *no* hulls alive and *no* pending spawns, waves never
overlap each other -- the only concurrency is between the beats of one wave.
The storm is the worst case: beats of 14, 14, 8, 12, 8 at +0.9 / +2.2 / +3.5 /
+4.8 / +6.1 s, with the hedgehogs on ttl 5000, which peaks at **48**. The
rules smoke fails the run if that log line ever appears.

## Seeing a shape before shipping it

`corridors.ps1` draws candidate formations in the game's own screen space
(x -1..1, y -1.3..1.3), so the coordinates on the picture are the numbers that
go in the scene. Cheaper than a build, and it is how A, B and D were chosen
out of four candidates.

## The storm, and why its peak is computed

Round 61 shipped the storm as forty hulls all on ttl 5000 and the tester
called it a light breeze -- correctly: hulls falling in step read as one
object, however many of them there are. Round 62 broke it into **eight bands
of speed**, from ttl 9000 (slower than anything else in the act) down to
3200 (nearly three times that). A rule carries ONE ttl -- `setttl` is read by
the rules block's outer loop, never inside a rule -- so a band of speed is a
rule.

Mixing speeds is also what FILLS the screen rather than crossing it: a slow
hull is still falling when three fast ones have come and gone, so the alive
count stacks. That is why `gen_storm.awk` does the arithmetic instead of the
author. It records each band's delay, ttl and size, walks every spawn and
death in time order, and prints the answer:

    @@PEAK@@ 85 spawned, peak 58 alive at +4600 ms (cap 64)

The first attempt at eight bands came out at **74** -- ten over a cap that
does not fail loudly. Six of the remaining margin belong to the multiplayer
squall. Retuning is editing two numbers and reading the line again.

## Round 63 -- the tester's second pass

`tail.template` is the act's enemies + rules blocks with `@@MARKER@@` lines where
the generated formations go; the scene is `head -159 act4.scene` (everything up
to the title block) followed by the template with the markers spliced in.

What changed on the device's word: the seed wave tripled; the long corridor
flanked by two silver Devils and six weavers down its lane, no turrets; the red
rain in four beats of six, 800 ms apart; the diagonal corridor straight after,
with two GHOST Devils parked where you would slip past it; the chicane worked by
three turrets and two falls of seekers, instead of guns standing in the open
before it; three stealth Devils under act I's two weaving columns, at the same
time; the second rain tightened to 800 ms with a THA sweeper high and one low;
a PILLAR -- two straight columns side by side, no lane between -- with SHAB fan
turrets on both flanks (a bounded arc, `rotAngle 0`: a spiral would close the
only two ways past); two stealth and two ghost elites; and the storm now
CONVERGES -- every hull's end X pulled toward the floor's centre -- at a
computed peak of 60 with the multiplayer squall trimmed to 4.

The bounce at the very end was the CAMERA: `driftAtEnd: 1` on act 2's rail is
the case camera.c documents as climb-then-turn, and the act plus its nine
seconds of card ran right up to the rail's 142 s. Removed; the rail holds its
last frame instead, which under a card is invisible.

And the parser: Rain reached 31 of the engine's 32 rules, with the ending as
the 32nd. `RULES_MAX` is 48 now, lives in `rules.h`, and packlint refuses a
scene over it -- the engine only logs and drops the rest, which for a reactive
act means losing `endAct`.

One thing I could not verify from here: the SHAB fan's angle convention.
`firingAngle1 225 firingAngle2 315` is meant to be a downward arc centred on
270. If it fires UP instead, the fans are harmless rather than dangerous -- a
visible failure, not a breaking one, and a two-number fix.

## Round 65 -- the storm becomes a scatter

The eight-band storm still read as ranks. The tester's brief: "plus de
verticalite, avec des petites lignes mais pleins de herissons -- 1/3 qui vont
ultra vite, 1/3 avec des trajectoires improbables, 1/3 qui descendent
normalement mais repartis ni en ligne ni en colonne."

`gen_storm.awk` now has four kinds -- `normal` (act I's column speed, 6500),
`fast` (2200, 1.4 u/s), `diag` (steep, from off both edges, crossing) and
`cross` (horizontal, at the player's height) -- and thirteen bands of them.
Two mechanics carry the brief:

- **Start height is arrival time.** A rule fires everything on one tick, but a
  hull that starts higher arrives later at the same speed, so each band spreads
  its start Y over up to two screen units. That is where the "verticalite"
  comes from without spending a rule per row.
- **Golden-ratio stepping, not a grid.** `x_k = frac(k*0.618..)`,
  `y_k = frac(k*0.382..)`: nothing repeats a lane or a height within a band or
  across bands. Deterministic -- lockstep needs every peer to read the same
  scene -- so no `rand()`.

105 spawned, peak 56 alive against a cap of 64 (squall 4). The first cut was
63: the second rain landed where every band overlapped, and moved 800 ms later.
Worst-case act length, computed from the scene: 117 s to the card.

## Frozen (2026-09-11)

The tester's verdict on 4.2.8: "le niveau est nickel comme cela." Rain is
frozen the way Act III was in round 19 — nothing in `act4.scene` or in this
directory changes without an explicit request. The generators stay so the
act can be regenerated bit-for-bit, and so the next act can borrow them.
