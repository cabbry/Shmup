# Candidate corridor shapes for Rain, drawn in the game's own screen space
# (x -1..1, y -1.3..1.3) so the coordinates on these pictures are the numbers
# that would go in the scene file.
Add-Type -AssemblyName System.Drawing

$PANW = 300; $PANH = 450          # one panel
$PAD = 34

function PX([single]$x) { return $PAD + ($x + 1.0) / 2.0 * ($PANW - 2*$PAD) }
function PY([single]$y) { return $PAD + (1.3 - $y) / 2.6 * ($PANH - 2*$PAD) }

function Draw-Hog($g, [single]$x, [single]$y) {
  $r = 11
  $b = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 38, 38, 44))
  $p = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 150, 155, 170), 1.6)
  $g.FillEllipse($b, (PX $x)-$r, (PY $y)-$r, 2*$r, 2*$r)
  $g.DrawEllipse($p, (PX $x)-$r, (PY $y)-$r, 2*$r, 2*$r)
  # a few spikes, so it reads as a hedgehog and not a hole
  for ($a = 0; $a -lt 8; $a++) {
    $t = $a * [Math]::PI / 4
    $g.DrawLine($p, ((PX $x) + [Math]::Cos($t)*$r), ((PY $y) + [Math]::Sin($t)*$r),
                    ((PX $x) + [Math]::Cos($t)*($r+4)), ((PY $y) + [Math]::Sin($t)*($r+4)))
  }
  $b.Dispose(); $p.Dispose()
}

function Draw-Route($g, $pts) {
  $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 90, 200, 255), 2.4)
  $pen.DashStyle = 'Dash'
  $pen.EndCap = 'ArrowAnchor'
  $a = @()
  foreach ($p in $pts) { $a += (New-Object System.Drawing.PointF((PX $p[0]), (PY $p[1]))) }
  $g.DrawLines($pen, [System.Drawing.PointF[]]$a)
  $pen.Dispose()
}

function Draw-Ship($g, [single]$x, [single]$y) {
  $b = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 90, 200, 255))
  $pts = @(
    (New-Object System.Drawing.PointF((PX $x), ((PY $y) - 11))),
    (New-Object System.Drawing.PointF(((PX $x) - 8), ((PY $y) + 8))),
    (New-Object System.Drawing.PointF(((PX $x) + 8), ((PY $y) + 8))))
  $g.FillPolygon($b, [System.Drawing.PointF[]]$pts); $b.Dispose()
}

function Panel($title, $sub, $hogs, $route, [single]$shipX) {
  $bmp = New-Object System.Drawing.Bitmap($PANW, $PANH)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.SmoothingMode = 'AntiAlias'
  $g.Clear([System.Drawing.Color]::FromArgb(255, 14, 16, 24))
  # the play field
  $fp = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(70, 120, 130, 160), 1)
  $g.DrawRectangle($fp, (PX (-1.0)), (PY 1.3), ((PX 1.0)-(PX (-1.0))), ((PY (-1.3))-(PY 1.3)))
  $fp.Dispose()
  if ($route) { Draw-Route $g $route }
  foreach ($hog in $hogs) { Draw-Hog $g $hog[0] $hog[1] }
  Draw-Ship $g $shipX (-1.05)
  $f1 = New-Object System.Drawing.Font("Consolas", 15, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
  $f2 = New-Object System.Drawing.Font("Consolas", 12, [System.Drawing.GraphicsUnit]::Pixel)
  $g.DrawString($title, $f1, [System.Drawing.Brushes]::White, 8, 6)
  $g.DrawString($sub, $f2, [System.Drawing.Brushes]::Gray, 8, ($PANH - 22))
  $f1.Dispose(); $f2.Dispose(); $g.Dispose()
  return $bmp
}

# ---- A: the straight corridor -------------------------------------------
# two short walls of three, a channel 0.34 wide between them
$A = @()
foreach ($y in 1.05, 0.75, 0.45) { $A += ,@(-0.34, $y); $A += ,@(0.34, $y) }
$panelA = Panel "A  couloir droit" "2 murs de 3, chenal 0.68 de large" $A @(@(0.0,-1.0),@(0.0,0.2),@(0.0,1.2)) 0.0

# ---- B: the sliding corridor --------------------------------------------
# the same channel, but both walls travel down-right: it slides out from under you
$B = @()
$k = 0
foreach ($y in 1.05, 0.75, 0.45) { $B += ,@((-0.55 + 0.22*$k), $y); $B += ,@((0.13 + 0.22*$k), $y); $k++ }
$panelB = Panel "B  couloir qui glisse" "le meme, en diagonale: il fuit" $B @(@(-0.55,-1.0),@(-0.30,0.1),@(0.10,0.9)) (-0.55)

# ---- C: the closing corridor --------------------------------------------
$C = @()
$k = 0
foreach ($y in 1.15, 0.85, 0.55, 0.25) { $C += ,@((-0.62 + 0.13*$k), $y); $C += ,@((0.62 - 0.13*$k), $y); $k++ }
$panelC = Panel "C  couloir qui se ferme" "il se resserre en descendant" $C @(@(0.0,-1.0),@(0.0,1.2)) 0.0

# ---- D: the chicane ------------------------------------------------------
$D = @()
foreach ($y in 1.15, 0.90) { $D += ,@(-0.10, $y); $D += ,@(0.55, $y); $D += ,@(0.95, $y) }
foreach ($y in 0.35, 0.10) { $D += ,@(-0.95, $y); $D += ,@(-0.55, $y); $D += ,@(0.10, $y) }
$panelD = Panel "D  chicane" "sortie a droite, puis a gauche" $D @(@(0.22,-1.0),@(0.22,0.55),@(-0.22,0.75),@(-0.22,1.2)) 0.22

$out = New-Object System.Drawing.Bitmap(($PANW*4 + 30), $PANH)
$g = [System.Drawing.Graphics]::FromImage($out)
$g.Clear([System.Drawing.Color]::FromArgb(255, 8, 9, 14))
$x = 0
foreach ($p in $panelA, $panelB, $panelC, $panelD) { $g.DrawImage($p, $x, 0); $p.Dispose(); $x += $PANW + 10 }
$g.Dispose()
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
$out.Save("$sp\corridors.png"); $out.Dispose()
"wrote $sp\corridors.png"
