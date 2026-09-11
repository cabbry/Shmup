# Emit the hedgehog formations for Rain.
#
# Every hull in a formation must travel the SAME distance in the same ttl, or
# the ones that start higher move faster and the shape shears apart on the way
# down. So each line is derived, never typed: pick a start, add the same dx/dy
# to get the end, and put the control point on the midpoint so the path is a
# straight line rather than a bulge.
function hog(x0, y0, x1, y1, st,    cx, cy) {
	cx = (x0 + x1) / 2; cy = (y0 + y1) / 2
	printf "\t        spawnEnemy mouvement 1 enemyType 1 startPos %.2f %.2f endPos %.2f %.2f controlPoint %.2f %.2f zAxisRot 0 xAxisRot 0 yAxisRot 0 subType %d\n", x0, y0, x1, y1, cx, cy, st
}
# The act-1 weaving column (mouvement 2). In X_SIN the X is xOffset +
# xWidth*cos(phase + 4*pi*f) -- the startPos X is the PHASE, not a position --
# and the Y is the usual Bezier, so staggering start/control/end Y by the same
# offset gives a rigid column that weaves as one.
function weave(xc, width, phase, y0, dy, st,    ym) {
	ym = y0 - dy / 2
	printf "\t        spawnEnemy mouvement 2 xOffset %.2f xWidth %.2f enemyType 1 startPos %.2f %.2f endPos %.2f %.2f controlPoint %.2f %.2f zAxisRot 0 xAxisRot 0 yAxisRot 0 subType %d\n", xc, width, phase, y0, xc, y0 - dy, xc, ym, st
}

BEGIN {
	# ---- A: the long corridor. Eight a side, a 0.64 lane down the middle,
	# falling 4.70 in 11 s (0.43 u/s): the head reaches the player about 5.5 s
	# after the spawn, and the tail is off the bottom at 11.
	print "@@A@@"
	for (k = 0; k < 8; k++) {
		y = 1.35 + 0.28 * k
		hog(-0.32, y, -0.32, y - 4.70, 2)
		hog( 0.32, y,  0.32, y - 4.70, 2)
	}

	# ---- X: the crossers. Four ordinary hulls cutting straight across the
	# lane at the player's height while the corridor is on him -- "comme pour
	# nous couper la route". They come from alternate sides and pass through
	# the black walls (nothing tests enemy against enemy), which reads as
	# emerging from them.
	print "@@X@@"
	hog(-1.55, -0.55,  1.55, -0.55, 0)
	hog( 1.55, -0.80, -1.55, -0.80, 0)
	hog(-1.55, -0.30,  1.55, -0.30, 0)
	hog( 1.55, -0.95, -1.55, -0.95, 0)

	# ---- B: the DIAGONAL corridor. Not a straight lane translating sideways
	# (round 63's mistake, "un couloir droit en quinconce") but a lane whose
	# SHAPE runs bottom-left to top-right: each row sits 0.17 further right
	# than the one below it, and every hull falls straight down. As the rows
	# pass the player, the safe x slides from -0.56 to +0.63 -- he has to
	# drift left to right the whole way, and the lane is only 0.64 wide.
	# Eight a side, as asked. The right column's top row ends at 0.95, so it
	# fits with the hull's radius to spare.
	print "@@B@@"
	for (k = 0; k < 8; k++) {
		y  = 1.35 + 0.28 * k
		xl = -0.88 + 0.17 * k
		hog(xl,        y, xl,        y - 4.70, 2)
		hog(xl + 0.64, y, xl + 0.64, y - 4.70, 2)
	}

	# ---- D: the chicane. Bank 1 fills the RIGHT half so you go left; bank 2
	# is 0.85 behind and fills the LEFT, and that gap is the time you have to
	# cross. Two rows deep each, or it reads as a line, not a wall.
	print "@@D@@"
	for (r = 0; r < 2; r++)
		for (k = 0; k < 4; k++) {
			x = 0.13 + 0.27 * k; y = 1.35 + 0.28 * r
			hog(x, y, x, y - 3.80, 2)
		}
	for (r = 0; r < 2; r++)
		for (k = 0; k < 4; k++) {
			x = -0.95 + 0.27 * k; y = 2.20 + 0.28 * r
			hog(x, y, x, y - 3.80, 2)
		}

	# ---- P: the pillar. Two straight columns side by side down the middle,
	# six tall: no lane between them, you pick a SIDE, and the fan turrets on
	# the flanks are aimed at exactly the sides you can pick.
	print "@@P@@"
	for (k = 0; k < 6; k++) {
		y = 1.35 + 0.28 * k
		hog(-0.14, y, -0.14, y - 4.10, 2)
		hog( 0.14, y,  0.14, y - 4.10, 2)
	}

	# ---- H: act I's two fast weaving columns, both flanks, for the Devils'
	# wave. Seven a column, stacked 0.26 apart, weaving as one; ordinary hulls
	# that die to a bullet, because they are the pressure and the Devils are
	# the target.
	print "@@H@@"
	for (k = 0; k < 7; k++) {
		y = 1.25 + 0.26 * k
		weave(-0.72, 0.22, -0.5, y, 2.80, 0)
		weave( 0.72, 0.22,  0.5, y, 2.80, 0)
	}

	# ---- E: six weavers down the long corridor's own lane, so the safe lane
	# is never empty for long.
	print "@@E@@"
	for (k = 0; k < 6; k++) {
		y = 1.60 + 0.42 * k
		weave(0.0, 0.26, (k % 2) ? 0.5 : -0.5, y, 3.10, 0)
	}
}
