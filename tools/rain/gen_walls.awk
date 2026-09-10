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

BEGIN {
	# ---- A: the long corridor. Eight a side, a 0.64 lane down the middle.
	print "@@A@@"
	for (k = 0; k < 8; k++) {
		y = 1.35 + 0.28 * k
		hog(-0.32, y, -0.32, y - 4.70, 2)
		hog( 0.32, y,  0.32, y - 4.70, 2)
	}

	# ---- B: the same corridor, sliding right by 1.20 as it falls.
	print "@@B@@"
	for (k = 0; k < 5; k++) {
		y = 1.35 + 0.28 * k
		hog(-0.32, y, 0.88, y - 4.00, 2)
		hog( 0.32, y, 1.52, y - 4.00, 2)
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

	# ---- THE FINALE. Four beats of hedgehogs CHARGING -- ttl 5000 against
	# the usual 8000+, so they cross at nearly twice the speed of anything
	# else in the act, from straight down to hard diagonals both ways. They
	# are ordinary hulls (subType 0 dies to one bullet, 1 takes five): the
	# biggest wave in the game has to be something you can shoot through, not
	# another wall. The budget is the reason for the speed as much as the
	# feel -- MAX_NUM_ENEMIES is 64 and the beats overlap.
	print "@@F1@@"
	for (k = 0; k < 7; k++) { x = -0.95 + 0.317 * k; hog(x, 1.30, x, -1.40, 0) }
	for (k = 0; k < 7; k++) { x = -1.35 + 0.30 * k; hog(x, 1.30, x + 0.80, -1.40, 0) }

	print "@@F2@@"
	for (k = 0; k < 7; k++) { x = 1.35 - 0.30 * k; hog(x, 1.30, x - 0.80, -1.40, 0) }
	for (k = 0; k < 7; k++) { x = -0.80 + 0.317 * k; hog(x, 1.55, x, -1.40, 1) }

	print "@@F3@@"
	for (k = 0; k < 6; k++) { x = -1.50 + 0.34 * k; hog(x, 1.30, x + 2.00, -1.40, 0) }
	for (k = 0; k < 6; k++) { x = 1.50 - 0.34 * k; hog(x, 1.30, x - 2.00, -1.40, 1) }
}
