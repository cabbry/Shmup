# An honest mock of the act-select screen: the shipped button sprite from
# homeAtlas.png, the shipped 16x16 font atlas, and the exact geometry menu.c
# computes -- screen space is x -320..320, y +480..-480, so the image is 640x960
# at one pixel per unit.
Add-Type -AssemblyName System.Drawing
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
. "$sp\brush.ps1"

$DATA  = "E:\Projects\Shmup\data\data"
$atlas = New-Object System.Drawing.Bitmap("$DATA\menu\homeAtlas.png")
$font  = New-Object System.Drawing.Bitmap("$DATA\menu\font.png")

function SSx([single]$x) { return $x + 320 }
function SSy([single]$y) { return 480 - $y }

# menu.c: the button quad is pos +/- dimensions/2, textured from
# homeAtlas (0,104)-(159,168) -- 159x64 px drawn at 318x128 units, so 2x.
function Draw-Button($g, [single]$cx, [single]$cy, [single]$w, [single]$h) {
  $dst = New-Object System.Drawing.Rectangle((SSx ($cx-$w/2)), (SSy ($cy+$h/2)), $w, $h)
  $g.DrawImage($atlas, $dst, 0, 104, 159, 64, [System.Drawing.GraphicsUnit]::Pixel)
}

# renderer.c SCR_ConvertTextToVertices, verbatim: charWidth = size*SS_W/40,
# the glyph QUAD is +/- charWidth (48 wide) but the pen advances charWidth (24),
# so glyphs overlap by half. Centering subtracts (n-1)*charSpace/2.
function Draw-Text($g, [string]$s, [single]$size, [single]$cx, [single]$cy, [bool]$centered) {
  $charWidth = $size * 320 / 40
  $charHeight = $charWidth
  $n = $s.Length
  $x = $cx
  if ($centered) { $x -= ($n - 1) * $charWidth / 2 }
  for ($i = 0; $i -lt $n; $i++) {
    $b = [int][char]$s[$i]
    $sx = ($b -band 15) * 32; $sy = [Math]::Floor($b / 16) * 32
    $dst = New-Object System.Drawing.Rectangle((SSx ($x-$charWidth)), (SSy ($cy+$charHeight)), ($charWidth*2), ($charHeight*2))
    $g.DrawImage($font, $dst, $sx, $sy, 32, 32, [System.Drawing.GraphicsUnit]::Pixel)
    $x += $charWidth
  }
}

# ...and the same, with a painted kanji standing in for one glyph cell: this is
# what putting a kanji in the atlas's free control-code rows would look like.
function Draw-TextKanji($g, $strokes, [string]$s, [single]$size, [single]$cx, [single]$cy) {
  $charWidth = $size * 320 / 40
  $n = $s.Length + 1
  $x = $cx - ($n - 1) * $charWidth / 2
  # the kanji, rendered into a 32x32 cell the way the atlas would hold it,
  # then blitted through the same quad as any other glyph
  $cell = New-Object System.Drawing.Bitmap(32,32,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $cg = [System.Drawing.Graphics]::FromImage($cell); $cg.SmoothingMode='AntiAlias'
  $kp = GlyphPath $strokes 0 0 26
  $kb = $kp.GetBounds()
  $m = New-Object System.Drawing.Drawing2D.Matrix
  $m.Translate((16 - $kb.Width/2 - $kb.X), (16 - $kb.Height/2 - $kb.Y)); $kp.Transform($m)
  $cg.FillPath([System.Drawing.Brushes]::White, $kp); $kp.Dispose(); $cg.Dispose()
  $dst = New-Object System.Drawing.Rectangle((SSx ($x-$charWidth)), (SSy ($cy+$charWidth)), ($charWidth*2), ($charWidth*2))
  $g.DrawImage($cell, $dst, 0, 0, 32, 32, [System.Drawing.GraphicsUnit]::Pixel)
  $cell.Dispose()
  $x += $charWidth
  for ($i = 0; $i -lt $s.Length; $i++) {
    $b = [int][char]$s[$i]
    $sx = ($b -band 15) * 32; $sy = [Math]::Floor($b / 16) * 32
    $d2 = New-Object System.Drawing.Rectangle((SSx ($x-$charWidth)), (SSy ($cy+$charWidth)), ($charWidth*2), ($charWidth*2))
    $g.DrawImage($font, $d2, $sx, $sy, 32, 32, [System.Drawing.GraphicsUnit]::Pixel)
    $x += $charWidth
  }
}

function Screen([string]$out, [bool]$withKanji) {
  $bmp = New-Object System.Drawing.Bitmap(640, 960)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.Clear([System.Drawing.Color]::FromArgb(255, 10, 12, 20))
  $g.InterpolationMode = 'HighQualityBicubic'
  $g.SmoothingMode = 'AntiAlias'

  Draw-Text $g "SELECT ACT" 3.0 0 340 $true
  Draw-Text $g "Unlocked up to Final" 2.0 0 250 $true

  # menu.c: two columns at x -160/+160, rows 150 apart from y = SS_H-360 = 120,
  # and a LAST button with no partner sits centred.
  # the SHIPPED names, expanded the way dEngine_ReadPackName does it: the
  # escape ~N becomes the raw byte N, which indexes the atlas cell.
  $plain  = @("Dawn","Hope","Dusk","Rain","Final")
  $withK  = @(([char]1 + " Dawn"), ([char]3 + " Hope"), ([char]4 + " Dusk"), ([char]5 + " Rain"), ([char]6 + " Final"))
  $names = if ($withKanji) { $withK } else { $plain }
  $kanji = @($null, $null, $KANJI_KURE, $KANJI_AME, $null)
  for ($a = 0; $a -lt 5; $a++) {
    $alone = ($a -eq 4)
    $x = if ($alone) { 0 } else { if ($a % 2 -eq 1) { 160 } else { -160 } }
    $y = 120 - [Math]::Floor($a / 2) * 150
    Draw-Button $g $x $y 318 128
    Draw-Text $g $names[$a] 3.0 $x $y $true
  }
  Draw-Button $g 0 -360 318 128
  Draw-Text $g "Back" 3.0 0 -360 $true
  $g.Dispose(); $bmp.Save($out); $bmp.Dispose()
  "wrote $out"
}

Screen "$sp\menu_names.png" $false
Screen "$sp\menu_kanji.png" $true
$atlas.Dispose(); $font.Dispose()
