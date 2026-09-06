# Level packs — the v4 format (draft, stage 1)

*Round 42. This is the inventory the v4 scripting work starts from, and the
first draft of the format a level will ship in. It will change as the stages
land; the changelog in `reborn.md` records why.*

## 1. What a level is today

A level is a **scene file** (`data/scenes/*.scene`, a block-structured text
format read by `world.c` and `event.c`) plus the files it points at:

| block | what it holds | read by |
|---|---|---|
| `map` | the baked city geometry (`data/map/*.map`) | `world.c` |
| `light`, `fog` | one light, linear fog (colour, start, end) | `world.c` |
| `camera` | the rail (`data/cameraPath/*.cp`, compiled to `.cp2b`), fov, near/far, `attachAt` / `detachAt` (ms), `driftAtEnd` | `world.c`, `camera.c` |
| `player` ×N | spawn matrices per seat | `world.c` |
| `playback` | the input record for the demo | `world.c` |
| `music` | `trackname`, `startMusicAt` (s) | `dEngine.c` |
| `title` | the act card (`data/titles/*.png`), `prolog` / `epilog` start + duration, `movePlayersToDefautlSSLocation` | `titles.c` |
| `enemies` | the spawn timeline: `settime` / `addtime` / `setttl` / `at <ms> spawnEnemy …` / `at <ms> spawnEnemyWave circle …` | `event.c` |
| `events` | everything else on a clock: `attachPlayers`, `detachPlayers`, `showProlog`, `showEpilog`, `requestScene`, `requestMenu`, `spawnText`, `stopPlayback`, `autopilot`, `uploadScore`, `limitedEvent`, `clearTitle`, `ttbRoll`, `displayStats`, `maskStats` | `event.c` |
| `model` / `MD5` | the intro's orbiting hull | `world.c` |

A `spawnEnemy` carries: `mouvement` (pattern id), `xOffset`, `xWidth`,
`enemyType` (HAB 0 · FHT 1 · LEE 2 · SHAB 3 · LOFB 4 · THA 5 · MISSILE 6),
`startPos` / `endPos` / `controlPoint` (screen-space, −1..1), the three axis
rotations, `subType` (normal · hard · impossible · weak), a `ttl`, and
per-type `parameters[]` (the FHT x position, the Devil's costume…).

The **scene registry** is `data/config.cfg`: ids 0 (intro), 1-4 (acts),
13 (demo), 14-15 (tutorials). Several code paths key on those ids.

**Everything in the table is declarative and already data-driven.** What is
not — the *reactive* behaviour Fabien pointed at — lives in C:

## 2. What is hardcoded in C (the scripting target)

| behaviour | where | what a script would need |
|---|---|---|
| **Boss attack ladder** — attacks join at 85 / 75 / 50 / 25 % HP lost, cadences per phase, the frenzy, arm HP (400, ×2 in multiplayer), the mega-laser's own 30-45 s clock, hover/sway, escorts, homing missiles | `lofb.c` | HP thresholds → phases; timers; fire patterns (fan, spray, big shot, seeker); spawn escorts; a "hold fire" state (the laser) |
| **The Devil's three costumes** — costume → weapon (trident / lasso / ripples), barrel-roll in, 4 s parked, roll out, the ghost's shimmer | `enemy.c` (`updateHAB`) | per-instance state, timers, aimed and patterned fire |
| **Per-type enemy behaviour** — movement patterns, fire cadences, HP per subtype, the FHT spin, the LEE aiming, the THA teardrops, the SHAB spray | `fht.c`, `lee.c`, `tha.c`, `shab.c`, `enemy.c` | leave in C (stage 1-3), expose as a catalogue the script can spawn and parametrise |
| **The side-view deployment** — side distance, rise, pitch, roll clamp | `camera.c` constants | the *timing* is already scene-driven (`ttbRoll`); the geometry can become scene parameters |
| **Scene-id gates** — intro = 0 (menu stage), acts 1-4 (progression `gHighestActReached ≤ 4`), demo/tutorials 13-15 (BACK button), licence check on act 1 | `dEngine.c`, `player.c`, `menu.c`, `EAGLView.m`, `netchannel.c` | a pack declares its *kind* (act / intro / tutorial / demo) instead of the code knowing the id |
| **Randomness** — `rand()` in `collisions.c`, `fht.c`, `tha.c`, `event.c` | unseeded C library RNG | scripts must draw from an engine RNG seeded per scene, never from the host's |

## 3. The constraint that shapes everything: lockstep

Multiplayer is deterministic lockstep: every peer runs the same simulation
and only inputs travel. So any scripted behaviour must be a pure function of
the simulation clock, the game state and the engine's RNG — same result on
every device, every frame. This rules out wall-clock, host randomness,
float formatting tricks and anything that depends on frame rate. The netrig
(four real stacks in one process) is the bench that will prove a script
deterministic before a device does.

## 4. The pack (stage 1 proposal)

```
data/levels/<id>/
  pack.cfg          the manifest
  <name>.scene      the scene file, paths relative to the pack
  <name>.cp         the camera rail (compiled to .cp2b at first load)
  title.png         the act card (optional)
  thumb.png         a thumbnail for the level list (optional)
  behaviour.lua     stage 3: scripted behaviour (built-in packs only, see §6)
```

`pack.cfg`:

```
pack
{
    format   1
    id       act1
    name     "明 -Dawn"
    kind     act            # act | intro | demo | tutorial
    author   "Fabien Sanglard"
    version  1
    scene    act1.scene
    music    data/music/UNREALPM.mp3   cue 0
    minPlayers 1   maxPlayers 4
}
```

The four acts, the intro, the demo and the two tutorials become eight packs
that *reference* the existing assets (no 15 MB texture move). `config.cfg`
lists packs instead of scene paths; the code paths that key on scene ids key
on `kind` and on the pack's place in the act order.

**Proof for stage 1:** the four acts loaded from packs produce the same CI
traces as today — the `[cull]` luma trace of the TTB smoke, the sound
signature of the audio smoke, the four-ship smoke.

## 5. Stage 2 — conditional events (the light path)

The `events` block gains **conditions**, **groups** and **phases**, so most
of the boss ladder and all of the Devils' choreography can be written
without a VM:

```
events
{
    group boss
    {
        when hpBelow 85  spawnPattern spray  cadence 900
        when hpBelow 75  every 6000 spawnEnemyWave circle 6 1 …
        when hpBelow 50  spawnPattern bigshot cadence 2400
        when hpBelow 25  set fanCount 5  set fanCadence 1100
        every 35000 for 4000  laser
        when armDestroyed left   damageBoss 120
        when cleared group escort1   at +2000 spawnEnemyWave …
    }
}
```

Conditions: `hpBelow`, `hpAbove`, `cleared <group>`, `armDestroyed`,
`players <n>`, `after <ms>`, `every <ms>`, `for <ms>`. Actions: the spawns
we have, plus fire patterns and parameter sets on a named enemy.
Deterministic by construction (no code runs, only tables), and safe to
download later (§6).

## 6. Stage 3 — a small VM, for what conditions cannot say

Lua 5.4, MIT-licensed (GPL-compatible), ~200 KB, sandboxed (no `io`, `os`,
`require`, no host time), one state per scene, called once per simulation
tick with `dt`. A deliberately small API: spawn, move, fire (fan / spray /
aimed / ring), set HP, timers, read the players, the camera roll, the
engine RNG. The boss ladder rewritten in Lua is the proof, held to the same
CI traces; the netrig at four is the determinism proof.

Downloaded content stays **declarative** (§5). Apple's guideline 2.5.2
forbids an app to download and execute code that changes its features;
scripts in downloaded levels would be exactly that. Lua is for the levels
we ship in the bundle. This decision is what makes a community level list
possible later without a store-review fight.

## 7. Stage 4 — tools

A command-line validator for packs (structure, references, the numbers'
ranges), the CI camera pointed at a pack (`SHMUP_LEVEL=<id>`), and this
document kept true. The exit test of v4's first half: a level written by the
tester from scratch, without touching the C.

## 8. Deferred, on purpose

- **A community level list / store** — after stage 4, once the format has
  been used by someone other than its author. Needs a server, moderation,
  reporting (App Store 1.2). Content declarative only (§6).
- **Skins, shots, cosmetics** — first-party only, if ever, through Apple's
  in-app purchase; the music is Future Crew's and the art Fabien's, so any
  paid cosmetic starts with their agreement.
- **Anything Sorare-like** — much later, if ever; see round 41's discussion
  in `reborn.md`: it is a company-scale door (App Store 3.1.1/3.1.5, MiCA,
  the French JONUM regime), not a feature.
