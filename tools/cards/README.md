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

## The kanji in the menu buttons

`make_font.ps1` puts six of them into `data/menu/font.png`, which is a 16×16
grid of 32×32 cells indexed by the character's own byte. Rows 0 and 1 are the
control codes and held nothing but placeholder boxes:

| cell | glyph | act | comes from |
|---|---|---|---|
| `0x01` | 明 | Dawn | the 2009 card, lifted by alpha box |
| `0x02` | 希 | (unused) | the 2009 card |
| `0x03` | 望 | Hope | the 2009 card |
| `0x04` | 暮 | Dusk | `brush.ps1` |
| `0x05` | 雨 | Rain | `brush.ps1` |
| `0x06` | 水 | Final | the 2009 card |

The four that exist as painted ink are **lifted from the title cards**, not
redrawn: the button shows you the same brush the act shows you two seconds
later. They are dilated by a pixel on the way down to 29, because a 60-pixel
brush glyph reduced to a third loses its thin strokes and the atlas's Latin
letters are bold.

A pack asks for a cell with `~<hex>` in its manifest `name` (see
`docs/level-pack.md` §4). Two full-width kanji cannot sit side by side: the
renderer's glyph quad is two cells wide while the pen advances one, so 希望
came out a single blot and Hope shows 望 alone.

`menu_mock.ps1` renders the act-select screen from the real atlas, the real
button sprite and the geometry copied out of `menu.c` — the cheapest way to
see a menu change without a Simulator.
