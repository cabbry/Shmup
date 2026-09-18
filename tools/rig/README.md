# tools/rig — the boss's skeleton

Fabien's last note on the boss (2026-09-17): animate its arms. The 2010 mesh,
`lofb.obj.md5mesh`, has one joint because it was converted from an `.obj` —
but the MD5 loader in `engine/src/md5.c` was written for the real thing: it
reads any number of joints (parent, position, orientation), several weights
per vertex, and `MD5_GenerateSkin(mesh, bones)` re-skins every vertex and
normal from whatever bone array it is handed. So the arms need a **rig**, not
a new model, and no Blender: the boss is symmetric, 45.7 units wide, and its
two claw arms are the vertices beyond |X| = 8.

## `rig_lofb.ps1`

Reads the one-joint mesh and writes `lofb_rigged.md5mesh` next to it:

| bone | parent | pivot | vertices |
|---|---|---|---|
| 0 `origin` | — | (0, 0, 0) | body, \|X\| < 8 — 670 alone, 204 shared |
| 1 `armL` | 0 | (−8.5, 5.3, −5.3) | X < −8 — 162 alone |
| 2 `armR` | 0 | (8.5, 5.3, −5.3) | X > 8 — 162 alone |

Across |X| = 6..10 a vertex carries **two weights**, body (1 − t) and arm (t),
t linear in |X|: the shoulder bends instead of tearing when the arm bone
rotates. Bones are written at identity, so a weight's bone-space position is
simply vertex − pivot. Vertex order, UVs and triangles are untouched; only the
weights are rewritten. The tool refuses a source that is not one-joint, is
culture-invariant (French Windows), and writes LF so the committed file is
byte-identical to what CI regenerates.

## `rig_check.c`

The proof, with the engine's own code: it compiles `md5.c`, `lexer.c`,
`quaternion.c`, `math.c` and the filesystem as they are, loads both meshes
through `MD5_LoadMesh`, and asserts

1. at rest, the rigged mesh skins to the original (positions within 1e-4,
   normals within 2/32767, UVs and indices identical);
2. with `armR` swung 30° about Y and `MD5_GenerateSkin` re-run, every pure
   right-arm vertex moves, the right-shoulder blend moves partially, and the
   body and left arm do not move at all;
3. the swung arm's normals stay unit length.

Measured: 1.1e-5 units at rest, 162 + 102 vertices moved by the swing, the
farthest 9.9 units. `netrig.yml` regenerates the rig from the source on every
push, refuses a committed mesh that differs from the tool's output, and runs
the harness. Build it with `-fno-sanitize=undefined`: the 2010 lighting pass in
`md5.c` increments a NULL pointer it never dereferences, which zig's debug
build traps on.

## What comes next (the v5 plan in `reborn.md`)

A *dynamic* entity usage that keeps `vertexArray` in RAM instead of freeing
it after the VBO upload — the Metal renderer already draws RAM meshes through
its ring buffer — and `LOFB_PoseBones` in `lofb.c`: the two arm bones posed
every frame from arm HP, the last big shot, the last hit and `simulationTime`,
so both lockstep sims skin the same boss.
