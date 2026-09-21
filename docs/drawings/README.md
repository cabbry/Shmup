# Drawings

The vector drawings made along the way to show the tester and Fabien what a
change does, gathered here from the working folders so they survive. Every
one was generated from the real data (the meshes, the camera, the engine's
own arithmetic), never drawn by hand. Open the SVGs in a browser.

## The boss (v5, rounds 72-86)

| file | what it shows |
|---|---|
| `boss_anatomy.svg` | the boss from above with the tester's names for every part: Ailes, Pattes arrière, Queue, Épaulettes, Antennes, Tube, Bloc, Cou, Pince, Creux (round 83, validated) |
| `boss_skeleton.svg` | the five bones and their pivots: origin, Bloc at the Tube, Pince at the Cou |
| `boss_pipeline.svg` | how a frame is made: pose the bones, re-skin in RAM, draw through the Metal ring buffer |
| `boss_rig.svg` | the hard cut of the mesh into body, Bloc and Pince, seam duplicated (the final rig, |X| = 5.5 and 16.3) |
| `boss_poses.svg` | the four poses the harness dumps: rest, breathing, pincer shut, torn off |
| `boss_solids.svg` | the collision circles each bone carries (21 per Bloc, 20 per Pince) and the two carved refuges (round 86, for Fabien) |

## Act III (v1.5-v1.7, the side-view beat)

| file | what it shows |
|---|---|
| `act3_devils_trombinoscope.pdf` | the three Devils in their costumes, with their weapons |
| `act3_devil_weapons.svg` | trident, lasso, waves: the three weapons by costume |
| `act3_fht_spin.svg` | the hedgehog's roll: the five axis configurations tried on device and the one that holds (0° drift) |
| `act3_tha_before_after.svg` | the THA drops laid on their side for the side view |
| `act3_boss_cameo_lightning.svg` | the boss cameo above the true horizon under the lightning train |
| `act3_cameo_probe.svg` | the probe that proved the cameo visible on both passes of the replay smoke |

## v2 (four players)

| file | what it shows |
|---|---|
| `v2_ships.svg` | the three ships, the third resurrected from the 2009 model |
| `v2_custom_menu.svg` | the Custom menu layout: ship, bullet colour, party size |
| `v2_hud_lives.svg` | the shared pool of lives on the HUD |
| `v2_menu_nav.svg` | the menu tree, solo / Multi / Others |

## v4 (levels as data)

| file | what it shows |
|---|---|
| `v4_menu_names.png` | the level menu reading the pack manifests |
