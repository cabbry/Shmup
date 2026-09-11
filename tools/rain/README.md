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
