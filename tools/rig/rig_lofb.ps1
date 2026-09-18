# Rig the boss: cut lofb.obj.md5mesh into three bones, by geometry.
#
# The 2010 mesh has one joint ("origin") and one weight per vertex, because it
# was converted from an .obj. The MD5 loader in md5.c reads any number of
# joints (parent, position, orientation) and several weights per vertex, and
# MD5_GenerateSkin re-skins every vertex from whatever bone array it is handed.
# So the arms only need a RIG, not a new model, and the rig is a cut:
#
#   bone 0  origin  parent -1  at (0,0,0)          the body, |X| < CUT
#   bone 1  armL    parent  0  at (-PIVOT_X, PIVOT_Y, PIVOT_Z)   X < -CUT
#   bone 2  armR    parent  0  at (+PIVOT_X, PIVOT_Y, PIVOT_Z)   X > +CUT
#
# The boss is symmetric, 45.7 units wide; its two claw arms are the vertices
# beyond |X| = 8 (189 a side), the shoulder band 7..9 sits around (8.5, 5.3,
# -5.3) -- measured in round 72. Across |X| = CUT-BLEND .. CUT+BLEND a vertex
# carries TWO weights, body (1-t) and arm (t), t linear in |X|, so the shoulder
# bends instead of tearing when the arm bone rotates.
#
# In the rest pose (arm bones at identity) the rigged mesh must skin to the
# original vertices: rig_check.c proves it with the engine's own md5.c, and
# also swings one arm to see that only its vertices move.
#
# All bones are written at identity orientation, so a weight's bone-space
# position is simply (vertex - pivot). Output keeps the vertex and triangle
# order of the source; only the weights are rewritten. Culture-invariant
# number handling: this runs on a French Windows.
#
# Usage: powershell -File tools/rig/rig_lofb.ps1
param(
  [string]$Source = "E:\Projects\Shmup\data\data\models\enemies\lofb.obj.md5mesh",
  [string]$Target = "E:\Projects\Shmup\data\data\models\enemies\lofb_rigged.md5mesh",
  [double]$Cut = 8.0,
  [double]$Blend = 2.0,
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

# --- rewrite the weights ------------------------------------------------------
$outWeights = New-Object System.Collections.Generic.List[string]
$outVerts   = New-Object System.Collections.Generic.List[string]
$countBody = 0; $countArmL = 0; $countArmR = 0; $countBlend = 0
foreach ($vertex in $verts) {
  $src = $weights[$vertex.start]
  $ax = [Math]::Abs($src.x)
  $armBias = ($ax - ($Cut - $Blend)) / (2.0 * $Blend)
  if ($armBias -lt 0) { $armBias = 0 }; if ($armBias -gt 1) { $armBias = 1 }
  $armBone = 0; $pivX = 0.0
  if ($src.x -lt 0) { $armBone = 1; $pivX = -$PivotX } else { $armBone = 2; $pivX = $PivotX }
  $start = $outWeights.Count
  $count = 0
  if ($armBias -lt 1) {
    $outWeights.Add(" weight $($outWeights.Count) 0 $(Fmt (1.0 - $armBias)) ( $(Fmt $src.x) $(Fmt $src.y) $(Fmt $src.z) ) ")
    $count++
  }
  if ($armBias -gt 0) {
    $outWeights.Add(" weight $($outWeights.Count) $armBone $(Fmt $armBias) ( $(Fmt ($src.x - $pivX)) $(Fmt ($src.y - $PivotY)) $(Fmt ($src.z - $PivotZ)) ) ")
    $count++
  }
  if ($armBias -eq 0) { $countBody++ } elseif ($armBias -eq 1) { if ($armBone -eq 1) { $countArmL++ } else { $countArmR++ } } else { $countBlend++ }
  $outVerts.Add(" vert $($vertex.id) ( $($vertex.s) $($vertex.t) ) $start $count ")
}

# --- write --------------------------------------------------------------------
$out = New-Object System.Collections.Generic.List[string]
$out.Add("MD5Version 10")
$out.Add("commandline `"tools/rig/rig_lofb.ps1 -- cut $Cut blend $Blend pivot ($PivotX $PivotY $PivotZ)`"")
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
$out.Add("`tnumverts $($outVerts.Count) ")
foreach ($entry in $outVerts) { $out.Add($entry) }
$out.Add("`tnumtris $($tris.Count)")
foreach ($tri in $tris) { $out.Add(" tri $($tri.id) $($tri.a) $($tri.b) $($tri.c) ") }
$out.Add("`tnumweights $($outWeights.Count)")
foreach ($entry in $outWeights) { $out.Add($entry) }
$out.Add("}")
[System.IO.File]::WriteAllText($Target, ($out -join "`n") + "`n", (New-Object System.Text.ASCIIEncoding))
"wrote $Target"
"bones: body $countBody, armL $countArmL, armR $countArmR, blended $countBlend (two weights each) -> $($outWeights.Count) weights"
