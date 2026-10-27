# Regenerate the menu font atlas -- data/menu/font.png -- at four times its
# 2009 resolution, in a free face cut to the 2009 metrics.
#
# THE ATLAS. A 16x16 grid indexed by the raw byte of the character
# (renderer.c: col = byte & 15, row = byte >> 4), so the layout is cp1252:
# rows 2-7 are ASCII, rows 8-15 the Latin-1 half the French menu needs. The
# renderer's UVs are per CELL (SHRT_MAX/16), never per pixel, so the cell size
# is free -- it shipped at 32 px, drawn 96 px tall on a 3x device: the blur
# Fabien pointed at next to the vector "Reborn" sign (2026-09-17). 128 px now.
#
# THE FACE. The 2009 atlas is Century Gothic Bold (measured glyph by glyph
# against a candidate sheet), and the first 128-px atlas was drawn in it. But
# Century Gothic is Monotype's, on a PC through the Windows/Office licence,
# which covers documents and images -- not a whole alphabet shipped inside an
# app as a working font. So the atlas is drawn in POPPINS BOLD, SIL Open Font
# License 1.1 (fonts\, with its licence), chosen by the tester on 2026-10-27
# from a sheet against Urbanist, Jost and League Spartan. It is loaded from
# that file, not from the machine's fonts, so any PC draws the same atlas.
# Two things the 2009 atlas had and the face does not: a SLASHED ZERO (the
# HUD's "x0" has always read "xØ") and a hard black outline, A=240, two pixels
# of a 32-px cell. Both are added here.
#
# THE WIDTH. Poppins is about a tenth wider than Century Gothic, and the
# engine draws each glyph in a quad two cells wide with a one-cell advance, so
# the widest glyphs (m, w, W, M, æ, @) ran into their neighbours: "commence"
# read "comnence". A glyph wider than Century Gothic's widest letter is
# narrowed to it; the others keep their drawing.
#
# THE METRICS, from the shipped 'B' / 'x' / 'g' cells: cap height 15/32 of the
# cell, baseline at 23/32, x-height 10/32, descender to 26/32, ink centred.
#
# THE KANJI, cells 0x01-0x06 (明 希 望 暮 雨 水), are carried over from the
# shipped atlas as they are. They were lifted from the 2009 title cards and
# drawn from brush.ps1 strokes in round 60, but the cards have since been
# redrawn at four times their size (make_cards.ps1, round 67), so the crop
# rectangles that found them no longer do.
# 0x09, 0x0A, 0x0D stay empty: tab, newline and CR can end up inside a string;
# so do the five codes cp1252 leaves undefined (0x81, 0x8D, 0x8F, 0x90, 0x9D).
Add-Type -AssemblyName System.Drawing
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path

$FONTPATH = "E:\Projects\Shmup\data\data\menu\font.png"
$CELLPX   = 128
$ATLAS    = $CELLPX * 16

$FACEFILE = "$sp\fonts\Poppins-Bold.ttf"   # SIL OFL 1.1, fonts\Poppins-OFL.txt
$STYLE    = [System.Drawing.FontStyle]::Bold
$CAP      = $CELLPX * 15 / 32              # cap height, from the 2009 'B'
$BASE     = $CELLPX * 23 / 32              # baseline
$OUTLINE  = $CELLPX * 2.2 / 32             # the black rim
$OUTCOL   = [System.Drawing.Color]::FromArgb(240, 0, 0, 0)
$MAXW     = $CELLPX * 70 / 128             # Century Gothic's widest letter, W, without its rim

$cp1252 = [System.Text.Encoding]::GetEncoding(1252)
$faces = New-Object System.Drawing.Text.PrivateFontCollection
$faces.AddFontFile($FACEFILE)
$fam = $faces.Families[0]
$FACE = $fam.Name

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
  # Winding is TrueType's own fill rule. With the default (alternate) rule,
  # contours a face leaves overlapping -- a crossbar over a stem -- are
  # punched out as holes: in the candidate sheet Jost's t drew as a dagger.
  $p = New-Object System.Drawing.Drawing2D.GraphicsPath([System.Drawing.Drawing2D.FillMode]::Winding)
  $p.AddString($s, $fam, [int]$STYLE, $EM, (New-Object System.Drawing.PointF(0,0)), (New-Object System.Drawing.StringFormat))
  $b = $p.GetBounds()
  if ($b.Width -le 0) { $p.Dispose(); return }
  if ($b.Width -gt $MAXW) {
    # THE WIDTH (header): narrowed about its own centre, height untouched
    $sq = New-Object System.Drawing.Drawing2D.Matrix
    $sq.Translate(($b.X + $b.Width/2), 0); $sq.Scale(($MAXW / $b.Width), 1); $sq.Translate(-($b.X + $b.Width/2), 0)
    $p.Transform($sq); $sq.Dispose()
    $b = $p.GetBounds()
  }
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
  if ([char]::IsControl($s[0])) { continue }                           # ...or mapped to a C1 control
  if ([char]::IsWhiteSpace($s[0])) { continue }
  $col = $b -band 15; $row = [Math]::Floor($b / 16)
  Draw-Glyph $g $s ($col * $CELLPX) ($row * $CELLPX)
  $drawn++
}

$g.Dispose(); $faces.Dispose()

# THE KANJI (header): the shipped atlas's cells 0x01-0x06, byte for byte --
# a DrawImage goes through premultiplied alpha and moves the soft edges. Read
# from bytes, so the file is not locked when it is written back below.
$shippedBytes = New-Object System.IO.MemoryStream(,[System.IO.File]::ReadAllBytes($FONTPATH))
$shipped = [System.Drawing.Bitmap]::FromStream($shippedBytes)   # not New-Object Bitmap(image): that redraws it
if ($shipped.Width -ne $ATLAS) { throw "the shipped atlas is $($shipped.Width) px wide; its kanji cells are copied 1:1 into a $ATLAS-px one" }
$band = New-Object System.Drawing.Rectangle(0, 0, $ATLAS, $CELLPX)
$src = $shipped.LockBits($band, 'ReadOnly', 'Format32bppArgb')
$dst = $out.LockBits($band, 'ReadWrite', 'Format32bppArgb')
$row = New-Object byte[] (6 * $CELLPX * 4)
for ($y = 0; $y -lt $CELLPX; $y++) {
  [Runtime.InteropServices.Marshal]::Copy([IntPtr]($src.Scan0.ToInt64() + $y * $src.Stride + $CELLPX * 4), $row, 0, $row.Length)
  [Runtime.InteropServices.Marshal]::Copy($row, 0, [IntPtr]($dst.Scan0.ToInt64() + $y * $dst.Stride + $CELLPX * 4), $row.Length)
}
$out.UnlockBits($dst); $shipped.UnlockBits($src); $shipped.Dispose(); $shippedBytes.Dispose()
$out.Save($FONTPATH, [System.Drawing.Imaging.ImageFormat]::Png); $out.Dispose()
"wrote $FONTPATH : ${ATLAS}x${ATLAS}, $drawn Latin glyphs in $FACE Bold at em $([Math]::Round($EM,1)), 6 kanji cells kept"
