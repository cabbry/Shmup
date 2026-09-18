# Re-vectorise a painted asset: upscale it and re-threshold its alpha, so a soft
# 2009 edge becomes a crisp edge at four times the size -- without redrawing a
# stroke of it.
#
# WHY NOT A FONT. homeAtlas.png (SHMUP, Difficulty, Multiplayer, Others, Game
# Over, Credits, 激怒, 谢谢) and the act cards' 明 -Dawn, 希望 -Hope, 水 -Water
# are Fabien's brush calligraphy, painted at 512 and 256 px in 2009 and drawn
# up to five times larger on a modern iPhone: "les fonts sont flou par rapport
# au nouveau sign Reborn" (2026-09-17). Replacing them with a typeface would
# lose his hand. This keeps it: bicubic upscale of the whole pixel, then the
# ALPHA alone pushed through a steep sigmoid around one half, which turns the
# interpolated ramp back into an edge one pixel wide at the new size. RGB is
# left as interpolated -- the grey shadow under the strokes is meant to be soft.
#
# What it cannot do: add detail that was never in the 512 -- a stroke's texture
# is what it was. What it does do: stop the strokes being blurry.
#
# Every consumer's UVs are normalised (SHRT_MAX per atlas, SHRT_MAX/16 per
# font cell, /512 fractions for the menu sprites), and the Metal loader takes
# any size and mipmaps it. No C changes for any of this.
Add-Type -AssemblyName System.Drawing

function Sharpen-Up($src, [int]$sx, [int]$sy, [int]$sw, [int]$sh, [int]$k, [double]$steep) {
  $big = New-Object System.Drawing.Bitmap(($sw*$k), ($sh*$k), [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = [System.Drawing.Graphics]::FromImage($big)
  $g.InterpolationMode = 'HighQualityBicubic'; $g.CompositingMode = 'SourceCopy'
  $g.DrawImage($src, (New-Object System.Drawing.Rectangle -ArgumentList 0, 0, ($sw*$k), ($sh*$k)), $sx, $sy, $sw, $sh, [System.Drawing.GraphicsUnit]::Pixel)
  $g.Dispose()
  $rect = New-Object System.Drawing.Rectangle -ArgumentList 0, 0, $big.Width, $big.Height
  $d = $big.LockBits($rect, 'ReadWrite', [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $n = $d.Stride * $big.Height; $buf = New-Object byte[] $n
  [System.Runtime.InteropServices.Marshal]::Copy($d.Scan0, $buf, 0, $n)
  for ($i = 3; $i -lt $n; $i += 4) {
    $a = $buf[$i] / 255.0
    if ($a -gt 0.02 -and $a -lt 0.98) {
      $a = 1.0 / (1.0 + [Math]::Exp(-$steep * ($a - 0.5)))
      $buf[$i] = [byte][Math]::Round($a * 255)
    }
  }
  [System.Runtime.InteropServices.Marshal]::Copy($buf, 0, $d.Scan0, $n); $big.UnlockBits($d)
  return $big
}

# Sharpen a whole file in place, k times its size.
function Sharpen-File([string]$path, [int]$k, [double]$steep) {
  $src = New-Object System.Drawing.Bitmap($path)
  $w = $src.Width; $h = $src.Height
  $big = Sharpen-Up $src 0 0 $w $h $k $steep
  $src.Dispose()
  $big.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $big.Dispose()
  "sharpened $path : ${w}x${h} -> $($w*$k)x$($h*$k)"
}

if ($MyInvocation.InvocationName -ne '.') {
  # run directly: the menu atlas and the three painted cards
  $DATA = "E:\Projects\Shmup\data\data"
  Sharpen-File "$DATA\menu\homeAtlas.png"   4 16.0
  Sharpen-File "$DATA\titles\dawnTitle.png" 4 16.0
  Sharpen-File "$DATA\titles\hopeTitle.png" 4 16.0
}
