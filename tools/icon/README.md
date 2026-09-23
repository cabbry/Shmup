# tools/icon — the app icon, drawn from the ship itself

The 2010 icon was a screenshot: the hero blurred over a blurred city, soft at
every size. This draws the same ship as **vector facets**, from the model the
game actually flies, in the style of the mesh drawings in `docs/drawings`.

    SC=<a folder> node tools/icon/make.js

Six variants land in `$SC/icon/out`, each as a 1024 SVG and a 512 PNG to
judge by eye: night background, none, a blue glow, an ember glow, and the
whole ship rather than the tight crop.

## How it draws

* `mesh.js` reads the MD5 mesh. One joint at identity, so a vertex is simply
  its single weight.
* `png.js` reads the texture. **The hero texture is interlaced** (Adam7), and
  a decoder that ignores that returns confetti -- the seven passes each carry
  their own width and their own filter bytes.
* `render.js` projects, flat-shades one colour per triangle, sorts back to
  front and emits polygons. It also rasterizes the same triangles with a
  depth buffer, so a picture can be looked at before anything ships.
* `stylise` keeps **where** the texture puts its plates, its greebles and its
  red, and remaps **what** they are onto three tones and one accent. The hull
  is dark and busy; an icon has to read at 60 pixels.

The view is the ship from above, nose up, tilted six degrees: the silhouette
is symmetric, the two hull plates are the biggest shapes on screen, and the
red markings fall where the eye lands. Backface culling halves the triangles
(3906 to about 1820) and changes nothing on a closed hull.
