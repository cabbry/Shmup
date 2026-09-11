# The storm's speed bands, and the peak they add up to.
#
# A rule carries ONE ttl -- setttl is read by the rules block's outer loop, not
# inside a rule -- so a band of speed is a rule. Distance is ~2.7 screen units,
# so ttl IS the speed: 11000 is slower than anything else in the act, 3200 is
# nearly three times that.
#
# The peak is COMPUTED here, not estimated. MAX_NUM_ENEMIES is 64 and the pool
# does not grow: past it ENE_Get hands out a shared dummy and the spawn quietly
# stops existing. Mixing speeds is what fills the screen -- a slow hull is still
# there when three fast ones have come and gone -- and it is also what makes the
# arithmetic non-obvious, which is why it is arithmetic and not a guess.

function hog(x0, y0, x1, y1, st,    cx, cy) {
	cx = (x0 + x1) / 2; cy = (y0 + y1) / 2
	printf "\t        spawnEnemy mouvement 1 enemyType 1 startPos %.2f %.2f endPos %.2f %.2f controlPoint %.2f %.2f zAxisRot 0 xAxisRot 0 yAxisRot 0 subType %d\n", x0, y0, x1, y1, cx, cy, st
}
function seeker(x,    y) {
	printf "\t        spawnEnemy mouvement 1 enemyType 6 startPos %.2f 1.25 endPos %.2f -1.30 controlPoint %.2f 0 zAxisRot 0 xAxisRot 0 yAxisRot 0 subType 0\n", x, x, x
}

# record one band for the peak arithmetic
function band(delay, ttl, n) {
	B_delay[nb] = delay; B_ttl[nb] = ttl; B_n[nb] = n; nb++
}

BEGIN {
	nb = 0

	# --- slow, and first: these are still falling when everything else has
	# been and gone. They are what makes the screen FULL rather than busy.
	print "@@S1@@";  band(500, 9000, 10)
	for (k = 0; k < 10; k++) { x = -0.95 + 0.211 * k; hog(x, 1.30, x, -1.40, 0) }

	# --- quick, shallow diagonals sweeping right
	print "@@S2@@";  band(1500, 4500, 12)
	for (k = 0; k < 12; k++) { x = -1.40 + 0.22 * k; hog(x, 1.30, x + 0.85, -1.40, 0) }

	# --- ordinary speed, straight, offset from the slow rank
	print "@@S3@@";  band(3300, 3200, 14)
	for (k = 0; k < 14; k++) { x = 1.55 - 0.22 * k; hog(x, 1.30, x - 1.90, -1.40, 0) }

	# --- BLITZ: hard diagonals right to left, nearly three times the speed
	# of the act's usual hulls. These are the ones that should be hard to read.
	print "@@S4@@";  band(4600, 4000, 14)
	for (k = 0; k < 14; k++) { x = -1.55 + 0.22 * k; hog(x, 1.30, x + 1.90, -1.40, 0) }

	# --- fast, the mirror
	print "@@S5@@";  band(6200, 4500, 12)
	for (k = 0; k < 12; k++) { x = -0.95 + 0.173 * k; hog(x, 1.30, x, -1.40, 1) }

	# --- one more slow rank, so the tail of the storm is not empty
	print "@@S6@@";  band(8600, 9000, 8)
	for (k = 0; k < 8; k++) { x = -0.82 + 0.235 * k; hog(x, 1.40, x, -1.40, 1) }

	# --- the red rain laid over the gaps
	print "@@SM1@@"; band(2400, 5000, 8)
	for (k = 0; k < 8; k++) seeker(-0.96 + 0.274 * k)
	print "@@SM2@@"; band(7400, 5000, 7)
	for (k = 0; k < 7; k++) seeker(-0.80 + 0.32 * k)

	# ---- the arithmetic -------------------------------------------------
	# Walk every spawn and death in time order and keep a running count.
	ne = 0
	for (i = 0; i < nb; i++) {
		E_t[ne] = B_delay[i];              E_d[ne] =  B_n[i]; ne++
		E_t[ne] = B_delay[i] + B_ttl[i];   E_d[ne] = -B_n[i]; ne++
	}
	for (i = 0; i < ne; i++)
		for (j = i + 1; j < ne; j++)
			if (E_t[j] < E_t[i]) { t = E_t[i]; E_t[i] = E_t[j]; E_t[j] = t
			                       d = E_d[i]; E_d[i] = E_d[j]; E_d[j] = d }
	alive = 0; peak = 0; total = 0
	for (i = 0; i < ne; i++) {
		alive += E_d[i]
		if (E_d[i] > 0) total += E_d[i]
		if (alive > peak) { peak = alive; peakAt = E_t[i] }
	}
	printf "@@PEAK@@ %d spawned, peak %d alive at +%d ms (cap 64)\n", total, peak, peakAt
}
