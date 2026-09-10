# The act title cards

The five cards the acts show at their prolog (`data/data/titles/*.png`,
256×128, white ink on transparent, a dark halo down-right):

| card | file | drawn by |
|---|---|---|
| 明 – Dawn / Act I | `dawnTitle.png` | painted, 2009 |
| 希望 – Hope / Act II | `hopeTitle.png` | painted, 2009 |
| 暮 – Dusk / Act III | `duskTitle.png` | here |
| 雨 – Rain / Act IV | `rainTitle.png` | here |
| 水 – Water / Final | `boss.png` | painted 2009, with only the line beneath redrawn |

Run it from this directory (Windows, System.Drawing):

    powershell -File make_cards.ps1

It overwrites the three cards it owns. `boss.png` is treated as source *and*
destination on purpose and the operation is idempotent: it copies the rows
above the numeral band from the existing file — the 2009 水, the word, the
rule, all untouched — and repaints only "Final" beneath. Nothing hand-painted
is ever regenerated.

## Why the kanji are drawn and not set

The 2009 cards are painted: every stroke has a blunt entry, a belly and a
tapered exit. No installed CJK face has that. Yu Gothic is a uniform slab;
SimSun only has small wedge serifs at the stroke ends. Round 59 first
rebuilt the dusk card with a widened SimSun and the tester spotted it
immediately — the weight, the Latin hand and the rule had all changed, and
the glyph had not.

So `brush.ps1` draws them. A stroke is a centreline (Catmull-Rom through a few
control points) plus a **width at every control point**, swept into a filled
ribbon: thick where the brush presses, nothing where it lifts. A glyph is a
list of strokes in a 0–100 box, unioned with `FillMode.Winding` — with the
default alternate rule every crossing punches a hole, which is what the first
render did.

Adding a character means adding a stroke list. The fastest way to place the
control points is to render the font glyph large under a grid, read the
skeleton off it, then iterate: the shapes here took three passes.

`$KANJI_YUU` (夕, four strokes) is kept even though the dusk card no longer
uses it — the tester picked 暮 over it. Both say dusk; 夕 is the evening
itself and 暮 is nightfall, the sun going under the grass.
