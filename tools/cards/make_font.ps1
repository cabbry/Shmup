# Put the acts' kanji into the menu font atlas.
#
# data/menu/font.png is a 16x16 grid of 32x32 cells indexed by the raw byte of
# the character (renderer.c: col = byte & 15, row = byte >> 4). Rows 0 and 1 are
# the control codes and hold nothing but placeholder boxes -- 32 free cells.
# Six of them become the acts' kanji, so a button can carry the act's name in
# both scripts.
#
# 0x01 明  0x02 希  0x03 望  0x04 暮  0x05 雨  0x06 水
#
# 0x09, 0x0A and 0x0D are left alone on purpose: tab, newline and carriage
# return can end up inside a string, and a stray kanji would be the tell.
#
# 明, 希, 望 and 水 are LIFTED FROM THE 2009 TITLE CARDS, not redrawn: the same
# ink the act shows you two seconds later. 暮 and 雨 come from brush.ps1, since
# their cards are ours anyway.
Add-Type -AssemblyName System.Drawing
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
. "$sp\brush.ps1"

$TITLES = "E:\Projects\Shmup\data\data\titles"
$FONTPATH   = "E:\Projects\Shmup\data\data\menu\font.png"
$CELLPX = 32
$INK    = 29     # the glyph's box inside the cell, leaving a hair of margin

# Composite src into a CELLxCELL bitmap, scaled to fit INK and centred, then
# dilated by a pixel: a 60-pixel brush glyph reduced to 29 loses its thin
# strokes otherwise, and the atlas's Latin letters are bold.
function Cell-FromImage($src, [int]$sx, [int]$sy, [int]$sw, [int]$sh) {
  $cell = New-Object System.Drawing.Bitmap($CELLPX, $CELLPX, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = [System.Drawing.Graphics]::FromImage($cell)
  $g.InterpolationMode = 'HighQualityBicubic'
  $scale = [Math]::Min($INK / [single]$sw, $INK / [single]$sh)
  $dw = $sw * $scale; $dh = $sh * $scale
  $dx = ($CELLPX - $dw) / 2; $dy = ($CELLPX - $dh) / 2
  foreach ($o in @(@(0,0), @(1,0), @(0,1), @(1,1))) {
    $dst = New-Object System.Drawing.RectangleF(($dx + $o[0] - 0.5), ($dy + $o[1] - 0.5), $dw, $dh)
    $g.DrawImage($src, $dst, (New-Object System.Drawing.RectangleF($sx, $sy, $sw, $sh)), [System.Drawing.GraphicsUnit]::Pixel)
  }
  $g.Dispose()
  return $cell
}

function Cell-FromStrokes($strokes) {
  $cell = New-Object System.Drawing.Bitmap($CELLPX, $CELLPX, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = [System.Drawing.Graphics]::FromImage($cell)
  $g.SmoothingMode = 'AntiAlias'
  $p = GlyphPath $strokes 0 0 $INK
  $b = $p.GetBounds()
  $m = New-Object System.Drawing.Drawing2D.Matrix
  $m.Translate(($CELLPX/2 - $b.Width/2 - $b.X), ($CELLPX/2 - $b.Height/2 - $b.Y))
  $p.Transform($m)
  # the same one-pixel dilation, as a pen, so thin strokes survive the size
  $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::White, 1.1); $pen.LineJoin = 'Round'
  $g.DrawPath($pen, $p); $pen.Dispose()
  $g.FillPath([System.Drawing.Brushes]::White, $p)
  $p.Dispose(); $g.Dispose()
  return $cell
}

# --- the six cells -------------------------------------------------------
$dawn = New-Object System.Drawing.Bitmap("$TITLES\dawnTitle.png")
$hope = New-Object System.Drawing.Bitmap("$TITLES\hopeTitle.png")
$water= New-Object System.Drawing.Bitmap("$TITLES\boss.png")

$cells = @{}
$cells[0x01] = Cell-FromImage $dawn  32 17 61 71          # 明, alpha box of the card
$cells[0x02] = Cell-FromImage $hope  37 15 51 56          # 希, the upper half of the stack
$cells[0x03] = Cell-FromImage $hope  37 72 51 49          # 望, the lower half
$cells[0x04] = Cell-FromStrokes $KANJI_KURE               # 暮
$cells[0x05] = Cell-FromStrokes $KANJI_AME                # 雨
$cells[0x06] = Cell-FromImage $water 22 12 73 77          # 水

$dawn.Dispose(); $hope.Dispose(); $water.Dispose()

# --- patch the atlas -----------------------------------------------------
$font = New-Object System.Drawing.Bitmap($FONTPATH)
$out  = New-Object System.Drawing.Bitmap($font.Width, $font.Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($out)
$g.DrawImage($font, 0, 0, $font.Width, $font.Height)
$font.Dispose()
$g.CompositingMode = 'SourceCopy'          # replace the placeholder box, don't blend with it
foreach ($b in ($cells.Keys | Sort-Object)) {
  $col = $b -band 15; $row = [Math]::Floor($b / 16)
  $g.DrawImage($cells[$b], ($col * $CELLPX), ($row * $CELLPX), $CELLPX, $CELLPX)
  $cells[$b].Dispose()
  "cell 0x{0:x2} at ({1},{2})" -f $b, $col, $row
}
$g.Dispose()
$out.Save($FONTPATH, [System.Drawing.Imaging.ImageFormat]::Png)
$out.Dispose()
"wrote $FONTPATH"
