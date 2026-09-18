# The bullet atlas at four times its size, with a DEDICATED sprite for the
# boss's big energy shot.
#
# spritesBullets.png (2009) is a 128x128 sheet of 16x32 cells: the player's
# capsules, the round shots, the muzzle flashes. The boss's big shot borrowed
# the 16 px SHAB orb at (80,0) and drew it at 0.22 screen units -- thirteen
# times its size on a 3x phone. Bilinear filtering turned it into a blur, and
# the half-texel bleed from the neighbouring cells (the white ball on one side,
# the blue orb on the other) framed it in a faint square: Fabien's "projectile
# bugge" (2026-09-17). This paints it a sprite of its own.
#
# Layout (native 128 coordinates; the engine's UVs are atlas FRACTIONS, so the
# sheet can be any size -- the Metal loader takes any size and mipmaps it):
#   - everything that exists is upscaled x4, bicubic, untouched otherwise (a
#     glow sprite gains nothing from a re-thresholded edge, unlike a brush
#     stroke -- see sharpen.ps1);
#   - the empty 48x48 region at (32,32) -- columns 2-4, rows 1 and half of 2,
#     never used -- receives the orb, painted natively at 192 px with an 8 px
#     transparent gutter so nothing can bleed into it.
# lofb.c reads the orb at UV (32,32) size (48,48) / 128 (LOFB_TEXT_BULLET_*).
#
# The orb: a white-hot core, a magenta body (the boss's own bullet colour, the
# laser sparks' 255/90/220), a deep violet shell with a brighter plasma ring,
# and a soft alpha fall-off over the outer fifth. Six faint filaments ripple the
# body so it is not a flat disc. No randomness: the same file every run.
#
# Usage:  powershell -File tools/cards/make_bullets.ps1
# Writes data/data/textures/object/spritesBullets.png (refuses a source that is
# not 128 px, so the sheet is never upscaled twice) and a before/after mock in
# the scratchpad (or next to the script when no scratchpad is given).
param(
  [string]$Atlas = "E:\Projects\Shmup\data\data\textures\object\spritesBullets.png",
  [string]$MockDir = ""
)
Add-Type -AssemblyName System.Drawing

$SCALE = 4
$ORB_X0 = 32; $ORB_Y0 = 32; $ORB_CELL = 48     # native atlas coordinates
$GUTTER = 8                                     # px, in the upscaled sheet

$source = New-Object System.Drawing.Bitmap -ArgumentList $Atlas
if ($source.Width -ne 128 -or $source.Height -ne 128) {
  $source.Dispose()
  throw "make_bullets: $Atlas is $($source.Width)x$($source.Height), expected the 128 px sheet (already upscaled?)"
}

# --- 1. the sheet, x4 bicubic -------------------------------------------------
$sheet = New-Object System.Drawing.Bitmap -ArgumentList (128*$SCALE), (128*$SCALE), ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$gfx = [System.Drawing.Graphics]::FromImage($sheet)
$gfx.InterpolationMode = 'HighQualityBicubic'; $gfx.CompositingMode = 'SourceCopy'; $gfx.PixelOffsetMode = 'Half'
$destRect = New-Object System.Drawing.Rectangle -ArgumentList 0, 0, (128*$SCALE), (128*$SCALE)
$gfx.DrawImage($source, $destRect, 0, 0, 128, 128, [System.Drawing.GraphicsUnit]::Pixel)
$gfx.Dispose()

# --- 2. the orb, painted per pixel -------------------------------------------
function Lerp([double]$from, [double]$to, [double]$k) { return $from + ($to - $from) * $k }
function Smooth([double]$edge0, [double]$edge1, [double]$value) {
  $k = ($value - $edge0) / ($edge1 - $edge0)
  if ($k -lt 0) { $k = 0 }; if ($k -gt 1) { $k = 1 }
  return $k * $k * (3.0 - 2.0 * $k)
}

$regionX = $ORB_X0 * $SCALE; $regionY = $ORB_Y0 * $SCALE; $regionSize = $ORB_CELL * $SCALE   # 128,128,192
$radius = ($regionSize / 2.0) - $GUTTER                                                    # 88
$centre = $regionSize / 2.0

$lock = $sheet.LockBits((New-Object System.Drawing.Rectangle -ArgumentList $regionX, $regionY, $regionSize, $regionSize), 'ReadWrite', [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$byteCount = $lock.Stride * $regionSize
$pixels = New-Object byte[] $byteCount
[System.Runtime.InteropServices.Marshal]::Copy($lock.Scan0, $pixels, 0, $byteCount)

for ($py = 0; $py -lt $regionSize; $py++) {
  for ($px = 0; $px -lt $regionSize; $px++) {
    $offset = $py * $lock.Stride + $px * 4
    $dx = ($px + 0.5) - $centre; $dy = ($py + 0.5) - $centre
    $dist = [Math]::Sqrt($dx*$dx + $dy*$dy) / $radius
    if ($dist -ge 1.0) {
      $pixels[$offset] = 0; $pixels[$offset+1] = 0; $pixels[$offset+2] = 0; $pixels[$offset+3] = 0
      continue
    }
    $angle = [Math]::Atan2($dy, $dx)
    # body colour by radius: white core -> magenta -> deep violet shell
    if ($dist -lt 0.30) {
      $k = Smooth 0.0 0.30 $dist
      $red = Lerp 255 255 $k; $green = Lerp 255 170 $k; $blue = Lerp 255 245 $k
    } elseif ($dist -lt 0.72) {
      $k = Smooth 0.30 0.72 $dist
      $red = Lerp 255 255 $k; $green = Lerp 170 90 $k; $blue = Lerp 245 220 $k
    } else {
      $k = Smooth 0.72 0.94 $dist
      $red = Lerp 255 150 $k; $green = Lerp 90 20 $k; $blue = Lerp 220 110 $k
    }
    # six filaments, a gentle ripple of the body brightness (not the core)
    $ripple = 1.0 + 0.09 * [Math]::Sin(6.0 * $angle + 4.0 * $dist) * (Smooth 0.25 0.55 $dist) * (1.0 - (Smooth 0.80 0.95 $dist))
    $red *= $ripple; $green *= $ripple; $blue *= $ripple
    # the plasma ring on the shell
    $ring = [Math]::Exp(-[Math]::Pow(($dist - 0.86) / 0.035, 2))
    $red = Lerp $red 255 (0.75 * $ring); $green = Lerp $green 175 (0.75 * $ring); $blue = Lerp $blue 245 (0.75 * $ring)
    # alpha: solid to 0.80, then a soft fall-off to the gutter
    $alpha = 1.0 - (Smooth 0.80 1.0 $dist)
    $alpha = $alpha * $alpha
    $pixels[$offset]   = [byte][Math]::Min(255, [Math]::Round($blue))
    $pixels[$offset+1] = [byte][Math]::Min(255, [Math]::Round($green))
    $pixels[$offset+2] = [byte][Math]::Min(255, [Math]::Round($red))
    $pixels[$offset+3] = [byte][Math]::Round($alpha * 255)
  }
}
[System.Runtime.InteropServices.Marshal]::Copy($pixels, 0, $lock.Scan0, $byteCount)
$sheet.UnlockBits($lock)

# --- 3. before / after mock: the old cell and the new orb, both at 208 px -----
if ($MockDir -eq "") { $MockDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
$shot = 208
$mock = New-Object System.Drawing.Bitmap -ArgumentList (2*$shot + 48), ($shot + 32), ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$mg = [System.Drawing.Graphics]::FromImage($mock)
$mg.Clear([System.Drawing.Color]::FromArgb(255, 18, 22, 40))
$mg.InterpolationMode = 'HighQualityBilinear'; $mg.PixelOffsetMode = 'Half'
$mg.DrawImage($source, (New-Object System.Drawing.Rectangle -ArgumentList 16, 16, $shot, $shot), 80, 0, 16, 16, [System.Drawing.GraphicsUnit]::Pixel)
$mg.DrawImage($sheet,  (New-Object System.Drawing.Rectangle -ArgumentList (32 + $shot), 16, $shot, $shot), $regionX, $regionY, $regionSize, $regionSize, [System.Drawing.GraphicsUnit]::Pixel)
$mg.Dispose()
$mockPath = Join-Path $MockDir "bigshot_before_after.png"
$mock.Save($mockPath, [System.Drawing.Imaging.ImageFormat]::Png); $mock.Dispose()

$source.Dispose()
$sheet.Save($Atlas, [System.Drawing.Imaging.ImageFormat]::Png); $sheet.Dispose()
"wrote $Atlas : 128x128 -> $(128*$SCALE)x$(128*$SCALE), orb at ($regionX,$regionY) size $regionSize"
"mock  $mockPath"
