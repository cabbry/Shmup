# Generate the act title cards in the family of the 2009 painted ones:
#   <kanji> -<Word>   /  rule  /  Act <numeral>
# white ink with a dark halo down-right, 256x128 32bpp on transparent.
#
# The Latin hand is Viner Hand ITC Bold -- picked against the shipped cards in a
# candidate sheet (Ink Free / Segoe Script / Bradley Hand / Brush Script MT were
# all thinner or more cursive than the 2009 brush).
#
# The KANJI ARE DRAWN, not set. No installed CJK face is a brush: Yu Gothic is a
# uniform slab and SimSun only has small wedge serifs, so re-weighting a card
# changes everything about it except the glyph -- which is exactly what the
# tester saw. brush.ps1 sweeps each stroke as a ribbon with a width at every
# control point: blunt entry, belly, tapered exit, like the painted 明 希望 水.
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
. "$sp\brush.ps1"

$OUT = "E:\Projects\Shmup\data\data\titles"

function New-CardGraphics($bmp) {
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.SmoothingMode = 'AntiAlias'
  $g.TextRenderingHint = 'AntiAliasGridFit'
  $g.InterpolationMode = 'HighQualityBicubic'
  return $g
}

# White ink with the family's dark halo down-right, from any path.
function InkPath($g, $path) {
  foreach ($o in @(@(3,3,150), @(2,2,110), @(4,4,70))) {
    $sh = $path.Clone()
    $ms = New-Object System.Drawing.Drawing2D.Matrix
    $ms.Translate([single]$o[0], [single]$o[1]); $sh.Transform($ms)
    $br = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb([int]$o[2],8,8,12))
    $g.FillPath($br, $sh); $br.Dispose(); $sh.Dispose(); $ms.Dispose()
  }
  $g.FillPath([System.Drawing.Brushes]::White, $path)
}

function Ink($g, [string]$t, [string]$fam, [int]$st, [single]$sz, [single]$x, [single]$top, [string]$al) {
  $ff = New-Object System.Drawing.FontFamily($fam)
  $p  = New-Object System.Drawing.Drawing2D.GraphicsPath
  $p.AddString($t, $ff, $st, $sz, (New-Object System.Drawing.PointF(0,0)), (New-Object System.Drawing.StringFormat))
  $b  = $p.GetBounds()
  $dx = switch ($al) { 'center' { $x - $b.Width/2 - $b.X } default { $x - $b.X } }
  $m  = New-Object System.Drawing.Drawing2D.Matrix
  $m.Translate($dx, ($top - $b.Y)); $p.Transform($m)
  InkPath $g $p
  $r = $p.GetBounds(); $p.Dispose(); $ff.Dispose(); return $r
}

# A painted kanji, placed by its INK box so it lands where the 2009 ones do.
function InkKanji($g, $strokes, [single]$x, [single]$top, [single]$size) {
  $p = GlyphPath $strokes 0 0 $size
  $b = $p.GetBounds()
  $m = New-Object System.Drawing.Drawing2D.Matrix
  $m.Translate(($x - $b.X), ($top - $b.Y)); $p.Transform($m); $m.Dispose()
  InkPath $g $p
  $r = $p.GetBounds(); $p.Dispose(); return $r
}

function Draw-Rule($g, [single]$x0, [single]$x1, [single]$y) {
  $sh = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(150,8,8,12))
  $g.FillRectangle($sh, $x0+2, $y+2, ($x1-$x0), 3)
  $g.FillRectangle([System.Drawing.Brushes]::White, $x0, $y, ($x1-$x0), 3)
  $sh.Dispose()
}

function Make-Card($strokes, [string]$word, [string]$sub, [single]$kanjiSize, [single]$wordSize, [single]$wordX, [string]$path) {
  $bmp = New-Object System.Drawing.Bitmap(256,128,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = New-CardGraphics $bmp
  $k = InkKanji $g $strokes 22 12 $kanjiSize
  Ink $g "-$word" "Viner Hand ITC" 1 $wordSize $wordX 30 'left' | Out-Null
  Draw-Rule $g ($wordX - 2) 238 79
  Ink $g $sub "Viner Hand ITC" 1 26 111 94 'center' | Out-Null
  $g.Dispose()
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
  "wrote $path  (kanji ink {0:n0}x{1:n0})" -f $k.Width, $k.Height
}

# The finale keeps its 2009 painted 水 -Water and rule: only the line beneath it
# changes, from "Act IV" to "Final" -- the same surgery the card had in round 19
# when it read "Act iii". Nothing hand-painted is redrawn.
function Make-FinalCard([string]$srcPath, [string]$path) {
  $src = New-Object System.Drawing.Bitmap($srcPath)
  $bmp = New-Object System.Drawing.Bitmap(256,128,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = New-CardGraphics $bmp
  $g.DrawImage($src, (New-Object System.Drawing.Rectangle(0,0,256,86)), 0,0,256,86, [System.Drawing.GraphicsUnit]::Pixel)
  $src.Dispose()
  Ink $g "Final" "Viner Hand ITC" 1 26 111 90 'center' | Out-Null
  $g.Dispose()
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
  "wrote $path"
}

Make-Card $KANJI_AME "Rain" "Act IV"  70 44 102 "$OUT\rainTitle.png"
Make-Card $KANJI_YUU "Dusk" "Act III" 70 44 102 "$OUT\duskTitle.png"
Make-FinalCard "$OUT\boss.png" "$OUT\boss.png"
