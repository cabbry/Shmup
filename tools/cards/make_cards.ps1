# Generate the act title cards in the family of the 2009 painted ones:
#   <kanji> -<Word>   /  rule  /  Act <numeral>
# white ink with a dark halo down-right, on transparent.
#
# AT FOUR TIMES THE 2009 SIZE (round 67). A card is drawn as a band across the
# whole screen width -- 1290 px on a 3x iPhone -- and shipped at 256x128, a
# fivefold blur; the worst in the game, and the one Fabien noticed. The title
# quad's UVs are 0..SHRT_MAX, so the texture can be any size: 1024x512 now.
# Every coordinate below is authored in the 2009 256x128 space and scaled by
# $S on the way out, so the layout is the one the cards always had.
#
# The Latin hand is Viner Hand ITC Bold -- picked against the shipped cards in a
# candidate sheet. The KANJI ARE DRAWN, not set (brush.ps1): no installed CJK
# face is a brush, and the tester saw it the moment one was used.
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
. "$sp\brush.ps1"
. "$sp\sharpen.ps1"

$OUT = "E:\Projects\Shmup\data\data\titles"
$S   = 4                     # 256x128 -> 1024x512
$W   = 256 * $S; $H = 128 * $S

function New-CardGraphics($bmp) {
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.SmoothingMode = 'AntiAlias'
  $g.TextRenderingHint = 'AntiAliasGridFit'
  $g.InterpolationMode = 'HighQualityBicubic'
  return $g
}

# White ink with the family's dark halo down-right, from any path. Offsets and
# alphas are the 2009 ones, scaled.
function InkPath($g, $path) {
  foreach ($o in @(@(3,3,150), @(2,2,110), @(4,4,70))) {
    $sh = $path.Clone()
    $ms = New-Object System.Drawing.Drawing2D.Matrix
    $ms.Translate([single]($o[0]*$S), [single]($o[1]*$S)); $sh.Transform($ms)
    $br = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb([int]$o[2],8,8,12))
    $g.FillPath($br, $sh); $br.Dispose(); $sh.Dispose(); $ms.Dispose()
  }
  $g.FillPath([System.Drawing.Brushes]::White, $path)
}

function Ink($g, [string]$t, [string]$fam, [int]$st, [single]$sz, [single]$x, [single]$top, [string]$al) {
  $ff = New-Object System.Drawing.FontFamily($fam)
  $p  = New-Object System.Drawing.Drawing2D.GraphicsPath
  $p.AddString($t, $ff, $st, ($sz*$S), (New-Object System.Drawing.PointF(0,0)), (New-Object System.Drawing.StringFormat))
  $b  = $p.GetBounds()
  $dx = switch ($al) { 'center' { $x*$S - $b.Width/2 - $b.X } default { $x*$S - $b.X } }
  $m  = New-Object System.Drawing.Drawing2D.Matrix
  $m.Translate($dx, ($top*$S - $b.Y)); $p.Transform($m)
  InkPath $g $p
  $r = $p.GetBounds(); $p.Dispose(); $ff.Dispose(); return $r
}

# A painted kanji, placed by its INK box so it lands where the 2009 ones do.
function InkKanji($g, $strokes, [single]$x, [single]$top, [single]$size) {
  $p = GlyphPath $strokes 0 0 ($size*$S)
  $b = $p.GetBounds()
  $m = New-Object System.Drawing.Drawing2D.Matrix
  $m.Translate(($x*$S - $b.X), ($top*$S - $b.Y)); $p.Transform($m); $m.Dispose()
  InkPath $g $p
  $r = $p.GetBounds(); $p.Dispose(); return $r
}

function Draw-Rule($g, [single]$x0, [single]$x1, [single]$y) {
  $sh = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(150,8,8,12))
  $g.FillRectangle($sh, ($x0+2)*$S, ($y+2)*$S, ($x1-$x0)*$S, 3*$S)
  $g.FillRectangle([System.Drawing.Brushes]::White, $x0*$S, $y*$S, ($x1-$x0)*$S, 3*$S)
  $sh.Dispose()
}

function Make-Card($strokes, [string]$word, [string]$sub, [single]$kanjiSize, [single]$wordSize, [single]$wordX, [string]$path) {
  $bmp = New-Object System.Drawing.Bitmap($W, $H, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = New-CardGraphics $bmp
  $k = InkKanji $g $strokes 22 12 $kanjiSize
  Ink $g "-$word" "Viner Hand ITC" 1 $wordSize $wordX 30 'left' | Out-Null
  Draw-Rule $g ($wordX - 2) 238 79
  Ink $g $sub "Viner Hand ITC" 1 26 111 94 'center' | Out-Null
  $g.Dispose()
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
  "wrote $path  ${W}x${H} (kanji ink {0:n0}x{1:n0})" -f $k.Width, $k.Height
}

# The finale keeps its 2009 painted 水 -Water and rule: the rows above the
# numeral band are lifted from the 256-px card and SHARPENED to size
# (sharpen.ps1), and only "Final" beneath is drawn. Nothing hand-painted is
# repainted. Idempotent on a 256 source; refuses a source already at size.
function Make-FinalCard([string]$srcPath, [string]$path) {
  $src = New-Object System.Drawing.Bitmap($srcPath)
  if ($src.Width -ne 256) { $src.Dispose(); throw "Make-FinalCard wants the 256-px boss.png as its source (git show v4.2.9:data/data/titles/boss.png)" }
  $top = Sharpen-Up $src 0 0 256 86 $S 16.0
  $src.Dispose()
  $bmp = New-Object System.Drawing.Bitmap($W, $H, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = New-CardGraphics $bmp
  $g.DrawImage($top, 0, 0, $top.Width, $top.Height); $top.Dispose()
  Ink $g "Final" "Viner Hand ITC" 1 26 111 90 'center' | Out-Null
  $g.Dispose()
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
  "wrote $path  ${W}x${H}"
}

Make-Card $KANJI_AME  "Rain" "Act IV"  70 44 102 "$OUT\rainTitle.png"
Make-Card $KANJI_KURE "Dusk" "Act III" 66 44 102 "$OUT\duskTitle.png"
Make-FinalCard "$OUT\boss.png" "$OUT\boss.png"
