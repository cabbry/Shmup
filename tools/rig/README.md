# tools/rig — the boss's skeleton

Fabien's last note on the boss (2026-09-17): animate its arms. The 2010 mesh,
`lofb.obj.md5mesh`, has one joint because it was converted from an `.obj` —
but the MD5 loader in `engine/src/md5.c` was written for the real thing: it
reads any number of joints (parent, position, orientation), several weights
per vertex, and `MD5_GenerateSkin(mesh, bones)` re-skins every vertex and
normal from whatever bone array it is handed. So the arms need a **rig**, not
a new model, and no Blender: the boss is symmetric, 45.7 units wide, and the
cut is a plane.

**Where the cut is (round 77).** The tester saw the seam run through the
shoulder block on device and named the joint: the small tube that joins the
body to the arm. The mesh agrees — the |X| 4.5..6.5 band is the sparsest of
the hull — so the cut is at |X| = 5.5 and the hinge at the tube's centroid
(5.5, 0.44, 1.16). The arm is the whole shoulder block plus the claw.

**How the cut is made (round 76): hard, with the seam duplicated.** Every
vertex has one bone; every triangle that straddled the cut is made
single-sided by duplicating its minority vertex onto the majority side (72
duplicates, 116 seam triangles), so body and arms are three shells that
coincide at rest and a torn-off arm pulls no body triangle. The first rig's
two-weight blend is gone. Bones are written at identity, so a weight's
bone-space position is simply vertex − pivot.

## `rig_lofb.ps1`

Reads the one-joint mesh and writes `lofb_rigged.md5mesh` next to it:

| bone | parent | pivot | vertices |
|---|---|---|---|
| 0 `origin` | — | (0, 0, 0) | Corps, Antennes, Épaulettes, Pattes arrière — 840 (72 seam copies included) |
| 1 `armL` / 2 `armR` | 0 | (∓5.5, 0.44, 1.16), the Tube | the Bloc — 97 each |
| 3 `clawL` / 4 `clawR` | 1 / 2 | (∓16.3, 3.3, −0.7), the Cou | the Pince — 118 each |

(Rounds 76–82 had three bones; the five-bone rig and the names of the parts are in the last section.)

One weight per vertex. The source's vertex order is kept and the seam
duplicates are appended; triangles are rewritten only where they crossed.
The tool refuses a source that is not one-joint, is culture-invariant (French
Windows), and writes LF so the committed file is byte-identical to what CI
regenerates — CI runs it with its defaults, so the defaults *are* the rig.

## `rig_check.c`

The proof, with the engine's own code: it compiles `md5.c`, `lexer.c`,
`quaternion.c`, `math.c` and the filesystem as they are, loads both meshes
through `MD5_LoadMesh`, and asserts

1. at rest, the rigged mesh skins to the original: every triangle corner has
   the original's position and UV (seam copies included), positions within
   1e-4; the normals may differ only at the seam (a lighting crease, counted);
2. with `armR` swung 30° about Y and `MD5_GenerateSkin` re-run, every
   right-arm vertex moves and nothing else does;
3. the swung arm's normals stay unit length (away from the seam);
4. the idle pose keeps the boss mirror-symmetric to a unit or so;
5. the solids leave the crook room for a ship, and the laser's clamped cone
   stays at least 38° wide;
6. `RIG_DUMP=<file>` writes four posed vertex clouds for the schematic renders.

Measured: 1.9e-6 units at rest, 205 vertices moved by the swing, the farthest
9.4 units, 28 vertices in the crease. `netrig.yml` regenerates the rig from the source on every
push, refuses a committed mesh that differs from the tool's output, and runs
the harness. Build it with `-fno-sanitize=undefined`: the 2010 lighting pass in
`md5.c` increments a NULL pointer it never dereferences, which zig's debug
build traps on.

## In the engine (round 74)

`ENT_DYNAMIC_DRAW` keeps a mesh's `vertexArray` in RAM (the Metal renderer
draws RAM meshes through its ring buffer); the boss loads that way from both
precache and spawn (`ENE_ModelUsage`). `LOFB_PoseArms` in `lofb.c` poses the
two arm bones every frame — idle swing, recoil on the big shot, flinch on a
hit, tremor at half HP, a folded limp wreck when destroyed — from arm HP, the
last shot, the last hit and `simulationTime`, then calls `MD5_GenerateSkin`.
Both lockstep sims skin the same boss.

## Solid arms and the crook (round 75)

`LOFB_BuildArmSolids` bins each arm's rest vertices in bone space on a 2×3-unit
grid and turns every occupied cell into a circle; the circles ride the posed
bone, and `LOFB_PlayerHitsArm` kills a ship that rams a live arm (a wreck does
not block). The crook — the ship's refuge from the laser, *under* the arm
between the body and the claw (round 77, where the tester actually hides) —
is **carved**: cells in the pocket bone x 3.5..10.5, z 0.84..7.84 are left
out, and so is the notch above the arm. The laser sweeps ±50° (round 78) but
`LOFB_ClampSweep` trims each beam to a live arm's crook, about 42° at the
nominal depth; the full sweep returns once both arms are torn off.
`rig_check` replays the solids and asserts the pocket admits the ship (2.47
units of room for a radius of 1.82) and that the clamped cone stays ≥ 38°.

## The parts that stay with the body (round 78)

The antenna fins (top, z < −8, raised toward the camera) and the rear legs
(bottom, z > 6, y < −3, |X| < 10) reach beyond the tube plane but are not arm: the tool keeps
them on bone 0, so the seam is no longer a plane and the arm is the plane
minus those two regions. Connectivity could not do it: the mesh is a pile of
disconnected shells. A last pass over the mesh's adjacency (round 80) sends
any arm vertex with fewer than two arm neighbours back to the body — no
orphans, no splinters — which leaves 205 vertices a side. (Round 82: the "bracket" fixed in round 79 was the top of the shoulder block -- back to fins and legs only.)

## Five bones, and the names of the parts (round 83)

| French name (the tester's) | what it is | bone |
|---|---|---|
| Corps | the hull, \|X\| < 5.5 — the ship is drawn HEAD-DOWN: head at the bottom of the screen | 0 `origin` |
| Ailes | the two big top fins, screen-up, raised toward the camera | 0 |
| Pattes arrière | the pieces above the wings, in the top corners | 0 |
| Queue | the two vertical tubes at the top centre, between the score and the lives | 0 |
| Épaulette | the middle fin under each wing | 0 |
| Antennes | the two small bottom fins, at the head | 0 |
| Tube | the hinge joining the hull to the arm, (±5.5, 0.44, 1.16) | pivot of 1/2 |
| Bloc | the shoulder block with the lights, 97 vertices | 1 `armL` / 2 `armR` |
| Cou | the 2-unit neck between block and claw, (±16.3, 3.3, −0.7) | pivot of 3/4 |
| Pince | the claw, 118 vertices | 3 `clawL` / 4 `clawR`, children of 1/2 |
| Creux | the refuge under the Bloc, between Corps and Pince | (a carved pocket in Bloc space) |

The Bloc only breathes (±3°): anything more tears the fins it sits under. The
Pince does the pincer, the recoil, the flinch and the tremor. The Pince's bone
transform is composed from the Bloc's every frame in `lofb.c` (MD5 skins with
absolute bones), so it rides the block and a torn arm falls in one piece.
