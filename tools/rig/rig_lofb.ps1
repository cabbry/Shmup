# Rig the boss: cut lofb.obj.md5mesh into three bones, by geometry.
#
# The 2010 mesh has one joint ("origin") and one weight per vertex, because it
# was converted from an .obj. The MD5 loader in md5.c reads any number of
# joints (parent, position, orientation) and several weights per vertex, and
# MD5_GenerateSkin re-skins every vertex from whatever bone array it is handed.
# So the arms only need a RIG, not a new model, and the rig is a cut:
#
#   bone 0  origin  parent -1  at (0,0,0)          the body, |X| < CUT
#   bone 1  armL    parent  0  at (-PIVOT_X, PIVOT_Y, PIVOT_Z)   X <= -CUT
#   bone 2  armR    parent  0  at (+PIVOT_X, PIVOT_Y, PIVOT_Z)   X >= +CUT
#
# The boss is symmetric, 45.7 units wide; its two claw arms are the vertices
# beyond |X| = 8 (189 a side), the shoulder band 7..9 sits around (8.5, 5.3,
# -5.3) -- measured in round 72.
#
# A HARD cut, with the seam DUPLICATED (round 76). The first rig blended the
# shoulder over |X| = 6..10 with two weights per vertex, which bends nicely
# but ties the arm to the body for ever: a torn-off arm would drag the
# shoulder triangles into spikes. So every vertex now belongs to exactly one
# bone, and every triangle that straddled the cut is made single-sided: its
# minority vertex is duplicated onto the majority side (same UV, that side's
# bone). Body and arms become three disconnected shells that coincide at rest
# -- the picture is the 2010 one to the last vertex, rig_check proves it --
# and an arm bone can go anywhere without pulling a single body triangle. The
# joint shows a hairline when the arm swings; a mech's shoulder does.
#
# All bones are written at identity orientation, so a weight's bone-space
# position is simply (vertex - pivot). The source's vertex order is kept, the
# duplicates are appended; triangles are rewritten only where they crossed.
# Culture-invariant number handling: this runs on a French Windows. LF line
# endings so the committed file is byte-identical to what CI regenerates.
#
# Usage: powershell -File tools/rig/rig_lofb.ps1
param(
  [string]$Source = "E:\Projects\Shmup\data\data\models\enemies\lofb.obj.md5mesh",
  [string]$Target = "E:\Projects\Shmup\data\data\models\enemies\lofb_rigged.md5mesh",
  [double]$Cut = 8.0,
  [double]$PivotX = 8.5,
  [double]$PivotY = 5.3,
  [double]$PivotZ = -5.3
)
$inv = [System.Globalization.CultureInfo]::InvariantCulture
function Num([string]$text) { return [double]::Parse($text, $inv) }
function Fmt([double]$value) { return $value.ToString("0.000000", $inv) }

$lines = [System.IO.File]::ReadAllLines($Source)

# --- parse ------------------------------------------------------------------
$numJoints = -1; $shader = ""; $verts = @(); $tris = @(); $weights = @()
foreach ($line in $lines) {
  $trim = $line.Trim()
  if ($trim -match '^numJoints\s+(\d+)') { $numJoints = [int]$Matches[1]; continue }
  if ($trim -match '^shader\s+"([^"]*)"') { $shader = $Matches[1]; continue }
  if ($trim -match '^vert\s+(\d+)\s+\(\s*(\S+)\s+(\S+)\s*\)\s+(\d+)\s+(\d+)') {
    $verts += [pscustomobject]@{ id=[int]$Matches[1]; s=$Matches[2]; t=$Matches[3]; start=[int]$Matches[4]; count=[int]$Matches[5] }; continue }
  if ($trim -match '^tri\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)') {
    $tris += [pscustomobject]@{ id=[int]$Matches[1]; a=[int]$Matches[2]; b=[int]$Matches[3]; c=[int]$Matches[4] }; continue }
  if ($trim -match '^weight\s+(\d+)\s+(\d+)\s+(\S+)\s+\(\s*(\S+)\s+(\S+)\s+(\S+)\s*\)') {
    $weights += [pscustomobject]@{ id=[int]$Matches[1]; bone=[int]$Matches[2]; bias=(Num $Matches[3]); x=(Num $Matches[4]); y=(Num $Matches[5]); z=(Num $Matches[6]) }; continue }
}
if ($numJoints -ne 1) { throw "rig_lofb: $Source has numJoints $numJoints, expected the one-joint 2010 mesh (already rigged?)" }
foreach ($vertex in $verts) { if ($vertex.count -ne 1) { throw "rig_lofb: vertex $($vertex.id) has $($vertex.count) weights, expected 1" } }
"source: $($verts.Count) verts, $($tris.Count) tris, $($weights.Count) weights, shader '$shader'"

# --- sides ------------------------------------------------------------------
# side 0 = body, 1 = armL, 2 = armR (== the bone index)
$side = New-Object int[] $verts.Count
$pos  = @{}
foreach ($vertex in $verts) {
  $src = $weights[$vertex.start]
  $pos[$vertex.id] = $src
  if ($src.x -le -$Cut) { $side[$vertex.id] = 1 } elseif ($src.x -ge $Cut) { $side[$vertex.id] = 2 } else { $side[$vertex.id] = 0 }
}

# --- the seam: duplicate the minority vertex of every straddling triangle ----
$outVertexList = New-Object System.Collections.Generic.List[object]   # {s, t, bone, x, y, z} in output order
foreach ($vertex in $verts) {
  $src = $pos[$vertex.id]
  $outVertexList.Add([pscustomobject]@{ s=$vertex.s; t=$vertex.t; bone=$side[$vertex.id]; x=$src.x; y=$src.y; z=$src.z })
}
$dupIndex = @{}    # "vertexId|bone" -> output index of the duplicate
$seamTris = 0
$outTris = New-Object System.Collections.Generic.List[object]
foreach ($tri in $tris) {
  $ids = @($tri.a, $tri.b, $tri.c)
  $sides = @($side[$tri.a], $side[$tri.b], $side[$tri.c])
  if (($sides[0] -eq $sides[1]) -and ($sides[1] -eq $sides[2])) { $outTris.Add(@($tri.a, $tri.b, $tri.c)); continue }
  $seamTris++
  # majority side: the one that appears twice
  $majority = if ($sides[0] -eq $sides[1]) { $sides[0] } elseif ($sides[0] -eq $sides[2]) { $sides[0] } else { $sides[1] }
  $newIds = @(0, 0, 0)
  for ($corner = 0; $corner -lt 3; $corner++) {
    if ($sides[$corner] -eq $majority) { $newIds[$corner] = $ids[$corner]; continue }
    $key = "$($ids[$corner])|$majority"
    if (-not $dupIndex.ContainsKey($key)) {
      $srcVertex = $verts[$ids[$corner]]; $src = $pos[$ids[$corner]]
      $outVertexList.Add([pscustomobject]@{ s=$srcVertex.s; t=$srcVertex.t; bone=$majority; x=$src.x; y=$src.y; z=$src.z })
      $dupIndex[$key] = $outVertexList.Count - 1
    }
    $newIds[$corner] = $dupIndex[$key]
  }
  $outTris.Add($newIds)
}

# --- write --------------------------------------------------------------------
$out = New-Object System.Collections.Generic.List[string]
$out.Add("MD5Version 10")
$out.Add("commandline `"tools/rig/rig_lofb.ps1 -- hard cut $Cut, seam duplicated, pivot ($PivotX $PivotY $PivotZ)`"")
$out.Add("")
$out.Add("numJoints 3")
$out.Add("numMeshes 1")
$out.Add("")
$out.Add("joints { ")
$out.Add("`t`"origin`" -1 ( 0 0 0 ) ( 0 0 0 )")
$out.Add("`t`"armL`" 0 ( $(Fmt (-$PivotX)) $(Fmt $PivotY) $(Fmt $PivotZ) ) ( 0 0 0 )")
$out.Add("`t`"armR`" 0 ( $(Fmt $PivotX) $(Fmt $PivotY) $(Fmt $PivotZ) ) ( 0 0 0 )")
$out.Add("}")
$out.Add("")
$out.Add("mesh {")
$out.Add("`tshader `"$shader`"")
$out.Add("`tnumverts $($outVertexList.Count) ")
for ($index = 0; $index -lt $outVertexList.Count; $index++) {
  $entry = $outVertexList[$index]
  $out.Add(" vert $index ( $($entry.s) $($entry.t) ) $index 1 ")
}
$out.Add("`tnumtris $($outTris.Count)")
for ($index = 0; $index -lt $outTris.Count; $index++) {
  $entry = $outTris[$index]
  $out.Add(" tri $index $($entry[0]) $($entry[1]) $($entry[2]) ")
}
$out.Add("`tnumweights $($outVertexList.Count)")
$countBody = 0; $countArmL = 0; $countArmR = 0
for ($index = 0; $index -lt $outVertexList.Count; $index++) {
  $entry = $outVertexList[$index]
  $pivX = 0.0; $pivY = 0.0; $pivZ = 0.0
  if ($entry.bone -eq 1) { $pivX = -$PivotX; $pivY = $PivotY; $pivZ = $PivotZ; $countArmL++ }
  elseif ($entry.bone -eq 2) { $pivX = $PivotX; $pivY = $PivotY; $pivZ = $PivotZ; $countArmR++ }
  else { $countBody++ }
  $out.Add(" weight $index $($entry.bone) 1.000000 ( $(Fmt ($entry.x - $pivX)) $(Fmt ($entry.y - $pivY)) $(Fmt ($entry.z - $pivZ)) ) ")
}
$out.Add("}")
[System.IO.File]::WriteAllText($Target, ($out -join "`n") + "`n", (New-Object System.Text.ASCIIEncoding))
"wrote $Target"
"bones: body $countBody, armL $countArmL, armR $countArmR vertices ($($outVertexList.Count - $verts.Count) duplicated along the seam, $seamTris seam triangles made single-sided)"
