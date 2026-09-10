# A brush, not a font.
#
# The 2009 cards' kanji are painted: every stroke has a blunt entry, a belly,
# and a tapered exit. No installed CJK face has that -- Yu Gothic is a uniform
# slab, SimSun only has small wedge serifs -- which is why re-weighting the
# dusk card changed everything about it EXCEPT the glyph.
#
# So the glyphs are drawn here instead: each stroke is a centreline (Catmull-Rom
# through a few control points) plus a WIDTH AT EACH POINT, swept into a filled
# ribbon. Thick where the brush presses, nothing where it lifts.
Add-Type -AssemblyName System.Drawing

function CatmullSample([single[]]$xs, [single[]]$ys, [int]$steps) {
  $n = $xs.Length
  $pts = New-Object System.Collections.ArrayList
  for ($i = 0; $i -lt $n - 1; $i++) {
    $p0 = $i - 1; if ($p0 -lt 0) { $p0 = 0 }
    $p3 = $i + 2; if ($p3 -gt $n - 1) { $p3 = $n - 1 }
    for ($s = 0; $s -lt $steps; $s++) {
      $t = $s / [single]$steps
      $t2 = $t * $t; $t3 = $t2 * $t
      $x = 0.5 * ((2*$xs[$i]) + (-$xs[$p0] + $xs[$i+1])*$t + (2*$xs[$p0] - 5*$xs[$i] + 4*$xs[$i+1] - $xs[$p3])*$t2 + (-$xs[$p0] + 3*$xs[$i] - 3*$xs[$i+1] + $xs[$p3])*$t3)
      $y = 0.5 * ((2*$ys[$i]) + (-$ys[$p0] + $ys[$i+1])*$t + (2*$ys[$p0] - 5*$ys[$i] + 4*$ys[$i+1] - $ys[$p3])*$t2 + (-$ys[$p0] + 3*$ys[$i] - 3*$ys[$i+1] + $ys[$p3])*$t3)
      [void]$pts.Add(@([single]$x, [single]$y, [single](($i + $t) / ($n - 1))))
    }
  }
  [void]$pts.Add(@([single]$xs[$n-1], [single]$ys[$n-1], [single]1.0))
  return $pts
}

# Width at parameter u in [0,1], linearly across the per-point widths.
function WidthAt([single[]]$ws, [single]$u) {
  $n = $ws.Length
  $f = $u * ($n - 1)
  $i = [Math]::Floor($f); if ($i -gt $n - 2) { $i = $n - 2 }; if ($i -lt 0) { $i = 0 }
  $t = $f - $i
  return $ws[$i] + ($ws[$i+1] - $ws[$i]) * $t
}

# One stroke -> a closed GraphicsPath, in the 0..100 box.
function StrokePath([single[]]$xs, [single[]]$ys, [single[]]$ws) {
  $samples = CatmullSample $xs $ys 14
  $left = New-Object System.Collections.ArrayList
  $right = New-Object System.Collections.ArrayList
  for ($k = 0; $k -lt $samples.Count; $k++) {
    $p = $samples[$k]
    $q = $samples[[Math]::Min($k+1, $samples.Count-1)]
    $r = $samples[[Math]::Max($k-1, 0)]
    $dx = $q[0] - $r[0]; $dy = $q[1] - $r[1]
    $len = [Math]::Sqrt($dx*$dx + $dy*$dy); if ($len -lt 1e-4) { $len = 1 }
    $nx = -$dy / $len; $ny = $dx / $len
    $w = (WidthAt $ws $p[2]) / 2.0
    [void]$left.Add((New-Object System.Drawing.PointF(($p[0] + $nx*$w), ($p[1] + $ny*$w))))
    [void]$right.Insert(0, (New-Object System.Drawing.PointF(($p[0] - $nx*$w), ($p[1] - $ny*$w))))
  }
  $poly = New-Object System.Collections.ArrayList
  [void]$poly.AddRange($left); [void]$poly.AddRange($right)
  $path = New-Object System.Drawing.Drawing2D.GraphicsPath
  $path.AddPolygon([System.Drawing.PointF[]]$poly)
  return $path
}

# A whole glyph: many strokes, unioned into one path in a 0..100 box, then
# scaled/translated to where the card wants it.
function GlyphPath($strokes, [single]$x, [single]$y, [single]$size) {
  $g = New-Object System.Drawing.Drawing2D.GraphicsPath
  $g.FillMode = [System.Drawing.Drawing2D.FillMode]::Winding   # strokes OVERLAP; alternate fill would punch holes at every crossing
  foreach ($s in $strokes) {
    $p = StrokePath $s.x $s.y $s.w
    $g.AddPath($p, $false)
    $p.Dispose()
  }
  $m = New-Object System.Drawing.Drawing2D.Matrix
  $m.Translate($x, $y); $m.Scale(($size/100.0), ($size/100.0))
  $g.Transform($m); $m.Dispose()
  return $g
}

# 夕 -- yuu, evening. Three strokes: the short left-falling one, the
# horizontal that folds into a long sweep down to the left, and the dot inside.
$KANJI_YUU = @(
  @{ x=@(55,46,31,16);    y=@(3,17,37,57);     w=@(6.5,5,3,1.0) },
  @{ x=@(26,52,79);       y=@(21,18,13);       w=@(4.5,5.5,10) },
  @{ x=@(80,71,55,35,11); y=@(12,34,55,74,89); w=@(10.5,8.5,6.5,4,1.0) },
  @{ x=@(38,47,57);       y=@(31,45,61);       w=@(2,5.5,1.2) }
)

# 暮 -- kure, nightfall. 莫 over 日: the grass radical, a sun, 大, and a
# second sun beneath. Fourteen strokes (sixteen ribbons -- each 横折 corner is
# drawn as a horizontal and a vertical that overlap), against 夕's four.
$KANJI_KURE = @(
  @{ x=@(3,50,97);      y=@(12,11,10);      w=@(4,4.5,8) },
  @{ x=@(35,34,33);     y=@(1,10,20);       w=@(6,5.5,5) },
  @{ x=@(63,61,60);     y=@(0,10,20);       w=@(6,5.5,5) },
  @{ x=@(23,23,23);     y=@(24,37,49);      w=@(6,5.5,5) },
  @{ x=@(23,52,81);     y=@(25,24,24);      w=@(4,4,7) },
  @{ x=@(81,80,80);     y=@(24,37,49);      w=@(7,6,6) },
  @{ x=@(25,52,79);     y=@(37,37,37);      w=@(3.5,3.5,4) },
  @{ x=@(23,52,81);     y=@(49,49,49);      w=@(4,4,5) },
  @{ x=@(2,50,98);      y=@(55,54,52);      w=@(4.5,5,9) },
  @{ x=@(58,42,20,2);   y=@(48,60,71,79);   w=@(8,6.5,4.5,1.2) },
  @{ x=@(47,66,84,98);  y=@(54,62,69,74);   w=@(3,5.5,7.5,2) },
  @{ x=@(29,29,29);     y=@(68,83,97);      w=@(6,5.5,5) },
  @{ x=@(29,50,71);     y=@(68,68,68);      w=@(4,4,7) },
  @{ x=@(71,71,70);     y=@(68,83,97);      w=@(7,6,6) },
  @{ x=@(31,50,69);     y=@(83,83,83);      w=@(3.5,3.5,4) },
  @{ x=@(29,50,71);     y=@(97,97,97);      w=@(4,4,5) }
)

# 雨 -- ame, rain. The bar, the frame, the spine, and the four drops.
$KANJI_AME = @(
  @{ x=@(6,50,95);        y=@(15,12,9);        w=@(5,6,12) },
  @{ x=@(23,21,20);       y=@(16,58,93);       w=@(8,7.5,5.5) },
  @{ x=@(24,55,82);       y=@(30,28,26);       w=@(4,4.5,9) },
  @{ x=@(82,81,79,70);    y=@(26,55,82,92);    w=@(9,8,6.5,1.2) },
  @{ x=@(52,52,52);       y=@(13,55,95);       w=@(6.5,7,5) },
  @{ x=@(33,38,43);       y=@(40,47,56);       w=@(2,5.5,1.2) },
  @{ x=@(33,38,43);       y=@(63,70,79);       w=@(2,5.5,1.2) },
  @{ x=@(62,67,72);       y=@(40,47,56);       w=@(2,5.5,1.2) },
  @{ x=@(62,67,72);       y=@(63,70,79);       w=@(2,5.5,1.2) }
)
