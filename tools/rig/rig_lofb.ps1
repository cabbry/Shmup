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
  [double]$Cut = 5.5,
  [double]$PivotX = 5.5,
  [double]$PivotY = 0.44,
  [double]$PivotZ = 1.16,
  # Round 83: the CLAW bones, children of the arm bones, hinged at the neck --
  # the thin section between the shoulder block and the claw (|X| 15.4..17.3,
  # z -3..1.5, y 1.5..5.1; measured in round 76).
  [double]$NeckX = 16.3,
  [double]$NeckY = 3.3,
  [double]$NeckZ = -0.7
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
#
# Round 78: the plane, MINUS the antennas and the rear legs. Both reach beyond
# |X| = Cut, and a plane cut handed them to the arm bones -- they moved with
# the arms and showed the same seam the arms had shown before (the tester's
# screenshot on 263; his call: leave them fixed). The mesh is a pile of
# disconnected shells (an .obj conversion), so connectivity cannot tell the
# parts apart; their PLACE can: the antennas are the top fins, screen-up and
# raised toward the camera (z < -8, y > 5, out to |X| ~14); the rear legs
# hang at the bottom, away from the camera, close to the body (z > 6, y < -3,
# |X| < 10 -- the claws share their z and y but sit beyond 17). Everything
# else beyond the plane is the arm: the tube, the shoulder block, the claw.
$side = New-Object int[] $verts.Count
$pos  = @{}
$countAntenna = 0; $countLeg = 0
foreach ($vertex in $verts) {
  $src = $weights[$vertex.start]
  $pos[$vertex.id] = $src
  $ax = [Math]::Abs($src.x)
  if ($ax -lt $Cut) { $side[$vertex.id] = 0; continue }
  # Round 82: only the antenna fins stay with the body (z < -8, raised toward
  # the camera). Round 79 had also fixed a "bracket" at z -8..-5 beyond |X|
  # 7.5 -- it was the TOP OF THE SHOULDER BLOCK, and the tester's screenshot
  # on 266 showed the block sawn in two. The whole block moves with the arm;
  # the stray pass below removes the true orphans.
  # Round 83: every raised fin above the block -- the antenna fins and the
  # middle fin under them -- stays with the body (z < -3, y > 4.5): the block
  # itself never rises above y 5.1. The block will only breathe a few degrees
  # from now on; the big motions belong to the CLAW bone, hinged at the neck.
  if ($src.z -lt -3.0 -and $src.y -gt 4.5) { $side[$vertex.id] = 0; $countAntenna++; continue }
  if ($src.z -gt 6.0 -and $src.y -lt -3.0 -and $ax -lt 10.0) { $side[$vertex.id] = 0; $countLeg++; continue }
  $side[$vertex.id] = if ($src.x -lt 0) { 1 } else { 2 }
}
"fixed parts beyond the plane: $countAntenna antenna vertices, $countLeg rear-leg vertices stay with the body"

# Round 80: no orphans, no shards. A vertex that the bands left with the arm
# but whose triangle neighbours are almost all body is a stray -- alone it is
# invisible, in twos or threes it becomes a floating splinter that moves with
# the arm. Two passes: an arm vertex with fewer than two arm neighbours joins
# the body.
$adjacency = @{}
foreach ($tri in $tris) {
  foreach ($pair in @(@($tri.a,$tri.b), @($tri.b,$tri.c), @($tri.c,$tri.a))) {
    if (-not $adjacency.ContainsKey($pair[0])) { $adjacency[$pair[0]] = New-Object System.Collections.Generic.HashSet[int] }
    if (-not $adjacency.ContainsKey($pair[1])) { $adjacency[$pair[1]] = New-Object System.Collections.Generic.HashSet[int] }
    [void]$adjacency[$pair[0]].Add($pair[1]); [void]$adjacency[$pair[1]].Add($pair[0])
  }
}
$strays = 0
for ($pass = 0; $pass -lt 2; $pass++) {
  foreach ($vertex in $verts) {
    $id = $vertex.id
    if ($side[$id] -eq 0 -or -not $adjacency.ContainsKey($id)) { continue }
    $armNeighbours = 0
    foreach ($next in $adjacency[$id]) { if ($side[$next] -eq $side[$id]) { $armNeighbours++ } }
    if ($armNeighbours -lt 2) { $side[$id] = 0; $strays++ }
  }
}
"strays: $strays arm vertices with fewer than two arm neighbours rejoined the body"

# Round 83: the claws. Within each arm, everything beyond the neck plane is
# the CLAW bone (3 = clawL, child of armL; 4 = clawR, child of armR). The seam
# at the neck is thin (the neck is 2 units across), and the claw is a shell
# of its own, so the big motions -- the pincer's snap, the recoil, the tremor
# -- happen there without tearing a fin.
$countClaw = 0
foreach ($vertex in $verts) {
  $id = $vertex.id
  if ($side[$id] -eq 1 -and $pos[$id].x -le -$NeckX) { $side[$id] = 3; $countClaw++ }
  elseif ($side[$id] -eq 2 -and $pos[$id].x -ge $NeckX) { $side[$id] = 4; $countClaw++ }
}
"claws: $countClaw vertices beyond |X| = $NeckX on the claw bones"

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
  # majority side: the one that appears twice (three different sides -- body,
  # arm and claw meeting at one triangle -- fall to the first corner's side)
  $majority = if ($sides[0] -eq $sides[1]) { $sides[0] } elseif ($sides[0] -eq $sides[2]) { $sides[0] } elseif ($sides[1] -eq $sides[2]) { $sides[1] } else { $sides[0] }
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
$out.Add("numJoints 5")
$out.Add("numMeshes 1")
$out.Add("")
$out.Add("joints { ")
$out.Add("`t`"origin`" -1 ( 0 0 0 ) ( 0 0 0 )")
$out.Add("`t`"armL`" 0 ( $(Fmt (-$PivotX)) $(Fmt $PivotY) $(Fmt $PivotZ) ) ( 0 0 0 )")
$out.Add("`t`"armR`" 0 ( $(Fmt $PivotX) $(Fmt $PivotY) $(Fmt $PivotZ) ) ( 0 0 0 )")
$out.Add("`t`"clawL`" 1 ( $(Fmt (-$NeckX)) $(Fmt $NeckY) $(Fmt $NeckZ) ) ( 0 0 0 )")
$out.Add("`t`"clawR`" 2 ( $(Fmt $NeckX) $(Fmt $NeckY) $(Fmt $NeckZ) ) ( 0 0 0 )")
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
$countBody = 0; $countArmL = 0; $countArmR = 0; $countClawL = 0; $countClawR = 0
for ($index = 0; $index -lt $outVertexList.Count; $index++) {
  $entry = $outVertexList[$index]
  $pivX = 0.0; $pivY = 0.0; $pivZ = 0.0
  if ($entry.bone -eq 1) { $pivX = -$PivotX; $pivY = $PivotY; $pivZ = $PivotZ; $countArmL++ }
  elseif ($entry.bone -eq 2) { $pivX = $PivotX; $pivY = $PivotY; $pivZ = $PivotZ; $countArmR++ }
  elseif ($entry.bone -eq 3) { $pivX = -$NeckX; $pivY = $NeckY; $pivZ = $NeckZ; $countClawL++ }
  elseif ($entry.bone -eq 4) { $pivX = $NeckX; $pivY = $NeckY; $pivZ = $NeckZ; $countClawR++ }
  else { $countBody++ }
  $out.Add(" weight $index $($entry.bone) 1.000000 ( $(Fmt ($entry.x - $pivX)) $(Fmt ($entry.y - $pivY)) $(Fmt ($entry.z - $pivZ)) ) ")
}
$out.Add("}")
[System.IO.File]::WriteAllText($Target, ($out -join "`n") + "`n", (New-Object System.Text.ASCIIEncoding))
"wrote $Target"
"bones: body $countBody, armL $countArmL, armR $countArmR, clawL $countClawL, clawR $countClawR vertices ($($outVertexList.Count - $verts.Count) duplicated along the seams, $seamTris seam triangles made single-sided)"
