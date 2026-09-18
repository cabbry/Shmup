# Regenerate the menu font atlas -- data/menu/font.png -- at four times its
# 2009 resolution, in the 2009 face.
#
# THE ATLAS. A 16x16 grid indexed by the raw byte of the character
# (renderer.c: col = byte & 15, row = byte >> 4), so the layout is cp1252:
# rows 2-7 are ASCII, rows 8-15 the Latin-1 half the French menu needs. The
# renderer's UVs are per CELL (SHRT_MAX/16), never per pixel, so the cell size
# is free -- it shipped at 32 px, drawn 96 px tall on a 3x device: the blur
# Fabien pointed at next to the vector "Reborn" sign (2026-09-17). 128 px now.
#
# THE FACE. Measured against the shipped cells glyph by glyph (a candidate
# sheet, tools/cards scratch): Century Gothic Bold is the 2009 skeleton -- the
# geometric S and C, the straight-legged R, the equal-bowled B, the one-storey
# g with the same hook. Two things the face does not have and the atlas did:
# a SLASHED ZERO (the HUD's "x0" reads as "x�", and it should keep to) and a
# hard black outline, A=240, two pixels of a 32-px cell. Both are added here.
#
# THE METRICS, from the shipped 'B' / 'x' / 'g' cells: cap height 15/32 of the
# cell, baseline at 23/32, x-height 10/32, descender to 26/32, ink centred.
#
# THE KANJI, cells 0x01-0x06, are as in round 60: 明 希 望 水 lifted from the
# painted 2009 title cards (at 128 px they upscale 2x from the card, against
# the 3x the whole atlas used to get), 暮 and 雨 drawn from brush.ps1 strokes.
# 0x09, 0x0A, 0x0D stay empty: tab, newline and CR can end up inside a string.
Add-Type -AssemblyName System.Drawing
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
. "$sp\brush.ps1"

$TITLES   = "E:\Projects\Shmup\data\data\titles"
$FONTPATH = "E:\Projects\Shmup\data\data\menu\font.png"
$CELLPX   = 128
$ATLAS    = $CELLPX * 16
$INK      = [int]($CELLPX * 0.90)         # the kanji box inside the cell

$FACE     = "Century Gothic"
$STYLE    = [System.Drawing.FontStyle]::Bold
$CAP      = $CELLPX * 15 / 32              # cap height, from the 2009 'B'
$BASE     = $CELLPX * 23 / 32              # baseline
$OUTLINE  = $CELLPX * 2.2 / 32             # the black rim
$OUTCOL   = [System.Drawing.Color]::FromArgb(240, 0, 0, 0)

$cp1252 = [System.Text.Encoding]::GetEncoding(1252)
$fam = New-Object System.Drawing.FontFamily($FACE)

# em size that puts THIS face's cap height on the 2009 one: measure 'H'
function Measure-CapHeight([single]$em) {
  $p = New-Object System.Drawing.Drawing2D.GraphicsPath
  $p.AddString("H", $fam, [int]$STYLE, $em, (New-Object System.Drawing.PointF(0,0)), (New-Object System.Drawing.StringFormat))
  $h = $p.GetBounds().Height; $p.Dispose(); return $h
}
$EM = 100.0 * $CAP / (Measure-CapHeight 100.0)

# One Latin glyph, outlined then filled, its baseline on BASE and its ink
# centred in the cell. The engine advances one cell per glyph but draws each
# in a quad TWO cells wide, so a glyph that hugs its cell's edges collides
# with its neighbours -- the 2009 ink stays inside the middle ~45 %.
function Draw-Glyph($g, [string]$s, [single]$ox, [single]$oy) {
  $p = New-Object System.Drawing.Drawing2D.GraphicsPath
  $p.AddString($s, $fam, [int]$STYLE, $EM, (New-Object System.Drawing.PointF(0,0)), (New-Object System.Drawing.StringFormat))
  $b = $p.GetBounds()
  if ($b.Width -le 0) { $p.Dispose(); return }
  # baseline: AddString's origin is the em box top; find where the baseline
  # falls by measuring a flat-bottomed glyph once
  $m = New-Object System.Drawing.Drawing2D.Matrix
  $m.Translate(($ox + $CELLPX/2 - $b.Width/2 - $b.X), ($oy + $BASE - $script:BaselineY))
  $p.Transform($m); $m.Dispose()
  $pen = New-Object System.Drawing.Pen($OUTCOL, [single]$OUTLINE); $pen.LineJoin = 'Round'
  $g.DrawPath($pen, $p); $pen.Dispose()
  $g.FillPath([System.Drawing.Brushes]::White, $p)
  if ($s -eq "0") {
    # THE SLASHED ZERO. The 2009 atlas had one and the HUD's lives counter
    # reads "xØ" because of it; the face does not, so it is stroked on: a
    # white bar a fifth of the glyph wide, running from stem to stem. No rim: a rimmed bar reads as a
    # dark cut across the stems, and the 2009 slash is plain white.
    $bb = $p.GetBounds(); $w = [single]($bb.Width * 0.20)
    $x0 = [single]($bb.X + $bb.Width * 0.26); $y0 = [single]($bb.Bottom - $bb.Height * 0.10)
    $x1 = [single]($bb.X + $bb.Width * 0.74); $y1 = [single]($bb.Y + $bb.Height * 0.10)
    $bar = New-Object System.Drawing.Pen([System.Drawing.Color]::White, $w); $bar.StartCap = 'Round'; $bar.EndCap = 'Round'
    $g.DrawLine($bar, $x0, $y0, $x1, $y1); $bar.Dispose()
  }
  $p.Dispose()
}

# where the baseline sits below AddString's origin, for this face and em
$probe = New-Object System.Drawing.Drawing2D.GraphicsPath
$probe.AddString("H", $fam, [int]$STYLE, $EM, (New-Object System.Drawing.PointF(0,0)), (New-Object System.Drawing.StringFormat))
$script:BaselineY = $probe.GetBounds().Bottom; $probe.Dispose()

# --- kanji cells, as round 60 but at CELLPX -------------------------------
function Cell-FromImage($src, [int]$sx, [int]$sy, [int]$sw, [int]$sh) {
  $cell = New-Object System.Drawing.Bitmap($CELLPX, $CELLPX, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = [System.Drawing.Graphics]::FromImage($cell); $g.InterpolationMode = 'HighQualityBicubic'
  $scale = [Math]::Min($INK / [single]$sw, $INK / [single]$sh)
  $dw = $sw * $scale; $dh = $sh * $scale; $dx = ($CELLPX - $dw) / 2; $dy = ($CELLPX - $dh) / 2
  $g.DrawImage($src, (New-Object System.Drawing.RectangleF($dx, $dy, $dw, $dh)), (New-Object System.Drawing.RectangleF($sx, $sy, $sw, $sh)), [System.Drawing.GraphicsUnit]::Pixel)
  $g.Dispose(); return $cell
}
function Cell-FromStrokes($strokes) {
  $cell = New-Object System.Drawing.Bitmap($CELLPX, $CELLPX, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = [System.Drawing.Graphics]::FromImage($cell); $g.SmoothingMode = 'AntiAlias'
  $p = GlyphPath $strokes 0 0 $INK
  $b = $p.GetBounds()
  $m = New-Object System.Drawing.Drawing2D.Matrix
  $m.Translate(($CELLPX/2 - $b.Width/2 - $b.X), ($CELLPX/2 - $b.Height/2 - $b.Y)); $p.Transform($m)
  $g.FillPath([System.Drawing.Brushes]::White, $p); $p.Dispose(); $g.Dispose()
  return $cell
}

# --- the atlas -----------------------------------------------------------
$out = New-Object System.Drawing.Bitmap($ATLAS, $ATLAS, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($out)
$g.SmoothingMode = 'AntiAlias'; $g.InterpolationMode = 'HighQualityBicubic'
$g.Clear([System.Drawing.Color]::FromArgb(0, 0, 0, 0))

$drawn = 0
for ($b = 0x20; $b -le 0xFF; $b++) {
  if ($b -eq 0x7F -or $b -eq 0xA0 -or $b -eq 0xAD) { continue }        # DEL, nbsp, soft hyphen
  $s = $cp1252.GetString([byte[]]@($b))
  if ($s -eq "" -or [int][char]$s -eq 0xFFFD) { continue }             # undefined in cp1252
  if ([char]::IsWhiteSpace($s[0])) { continue }
  $col = $b -band 15; $row = [Math]::Floor($b / 16)
  Draw-Glyph $g $s ($col * $CELLPX) ($row * $CELLPX)
  $drawn++
}

$dawn = New-Object System.Drawing.Bitmap("$TITLES\dawnTitle.png")
$hope = New-Object System.Drawing.Bitmap("$TITLES\hopeTitle.png")
$water= New-Object System.Drawing.Bitmap("$TITLES\boss.png")
$cells = @{}
$cells[0x01] = Cell-FromImage $dawn  32 17 61 71          # 明
$cells[0x02] = Cell-FromImage $hope  37 15 51 56          # 希
$cells[0x03] = Cell-FromImage $hope  37 72 51 49          # 望
$cells[0x04] = Cell-FromStrokes $KANJI_KURE               # 暮
$cells[0x05] = Cell-FromStrokes $KANJI_AME                # 雨
$cells[0x06] = Cell-FromImage $water 22 12 73 77          # 水
$dawn.Dispose(); $hope.Dispose(); $water.Dispose()
foreach ($k in ($cells.Keys | Sort-Object)) {
  $g.DrawImage($cells[$k], (($k -band 15) * $CELLPX), ([Math]::Floor($k / 16) * $CELLPX), $CELLPX, $CELLPX)
  $cells[$k].Dispose()
}
$g.Dispose(); $fam.Dispose()
$out.Save($FONTPATH, [System.Drawing.Imaging.ImageFormat]::Png); $out.Dispose()
"wrote $FONTPATH : ${ATLAS}x${ATLAS}, $drawn Latin glyphs in $FACE Bold at em $([Math]::Round($EM,1)), 6 kanji cells"
