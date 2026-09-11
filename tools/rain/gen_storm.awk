# The storm: eight bands of speed, CONVERGING on the player, and the peak they
# add up to.
#
# A rule carries ONE ttl -- setttl is read by the rules block's outer loop, not
# inside a rule -- so a band of speed is a rule. The vertical distance is ~2.7
# screen units, so the ttl IS the speed: 9000 is slower than anything else in
# the act, 3200 is nearly three times that.
#
# CONVERGING (round 63, "il faut que tout nous fonce dessus"): every hull's end
# X is pulled toward the centre of the screen's floor, where the player lives,
# so the outer lanes angle IN instead of falling in parallel. A hedgehog cannot
# aim -- only the seekers home -- but a formation can be aimed, and this one is.
#
# The peak is COMPUTED, not estimated. MAX_NUM_ENEMIES is 64 and the pool does
# not grow: past it ENE_Get hands out a shared dummy and the spawn quietly stops
# existing. The multiplayer squall is 4 hulls, so the storm may peak at 60.

function hog(x0, y0, x1, y1, st,    cx, cy) {
	cx = (x0 + x1) / 2; cy = (y0 + y1) / 2
	printf "\t        spawnEnemy mouvement 1 enemyType 1 startPos %.2f %.2f endPos %.2f %.2f controlPoint %.2f %.2f zAxisRot 0 xAxisRot 0 yAxisRot 0 subType %d\n", x0, y0, x1, y1, cx, cy, st
}
# a hull that starts at x and ends pulled toward the centre by factor c
function dive(x, y0, c, st) { hog(x, y0, x * c, -1.40, st) }
function seeker(x) {
	printf "\t        spawnEnemy mouvement 1 enemyType 6 startPos %.2f 1.25 endPos %.2f -1.30 controlPoint %.2f 0 zAxisRot 0 xAxisRot 0 yAxisRot 0 subType 0\n", x, x, x
}
function band(delay, ttl, n) { B_delay[nb] = delay; B_ttl[nb] = ttl; B_n[nb] = n; nb++ }

BEGIN {
	nb = 0

	# slow and first: still falling when three fast bands have come and gone
	print "@@S1@@";  band(500, 7000, 10)
	for (k = 0; k < 10; k++) dive(-1.10 + 0.244 * k, 1.30, 0.35, 0)

	# quick, from wide left, angling in hard
	print "@@S2@@";  band(1500, 4500, 12)
	for (k = 0; k < 12; k++) dive(-1.60 + 0.16 * k, 1.30, 0.20, 0)

	# the red rain over the first gap
	print "@@SM1@@"; band(2400, 5000, 8)
	for (k = 0; k < 8; k++) seeker(-0.96 + 0.274 * k)

	# BLITZ from wide right, three times the act's usual speed
	print "@@S3@@";  band(3300, 3200, 14)
	for (k = 0; k < 14; k++) dive(1.60 - 0.16 * k, 1.30, 0.20, 0)

	# fast, the mirror, from wide left
	print "@@S4@@";  band(4600, 4000, 16)
	for (k = 0; k < 16; k++) dive(-1.65 + 0.145 * k, 1.30, 0.25, 0)

	# quick, straight down the middle band, hardened
	print "@@S5@@";  band(6200, 4500, 12)
	for (k = 0; k < 12; k++) dive(-0.95 + 0.173 * k, 1.30, 0.60, 1)

	# the second rain
	print "@@SM2@@"; band(7400, 5000, 7)
	for (k = 0; k < 7; k++) seeker(-0.80 + 0.32 * k)

	# a slow rank to close, so the storm's tail is not empty
	print "@@S6@@";  band(8600, 7000, 10)
	for (k = 0; k < 10; k++) dive(-1.05 + 0.233 * k, 1.40, 0.40, 1)

	# ---- the arithmetic -------------------------------------------------
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
	printf "@@PEAK@@ %d spawned, peak %d alive at +%d ms (cap 64, squall 4)\n", total, peak, peakAt
}
