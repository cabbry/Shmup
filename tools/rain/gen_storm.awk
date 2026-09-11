# The storm, round 65: a SCATTER in three thirds, and the peak it adds up to.
#
# The tester's brief: no more ranks. "Plus de verticalite, avec des petites
# lignes mais pleins de herissons -- 1/3 qui vont ultra vite, 1/3 avec des
# trajectoires improbables (diagonales, sur le cote pour couper la route),
# 1/3 qui descendent normalement mais repartis ni en ligne ni en colonne."
#
# HOW A SINGLE RULE SCATTERS IN TIME. A rule fires all its spawns on one tick,
# but a hull that STARTS HIGHER arrives later -- at the same speed, since every
# hull in a band falls the same distance in the same ttl. So a band is one ttl
# (one speed) and a spread of start heights: the vertical stagger IS the
# temporal stagger, and none of it lines up.
#
# NEITHER ROWS NOR COLUMNS. Positions come from golden-ratio stepping, not a
# grid: x_k = frac(k*0.618..) and y_k = frac(k*0.382..) never repeat a lane or a
# height within a band, and never align across bands either. Deterministic --
# the scene is data and lockstep needs every peer to read the same file -- so
# no rand().
#
# THE PEAK IS COMPUTED. MAX_NUM_ENEMIES is 64 and the pool does not grow; past
# it ENE_Get hands out a shared dummy and the spawn stops existing. The
# multiplayer squall is 4, so the storm may peak at 60. Alive time is delay to
# delay+ttl whatever the start height -- a hull off the top is still a hull --
# so the scatter changes what you SEE, not the arithmetic.

function frac(v) { return v - int(v) }
function gx(k, lo, hi) { return lo + frac(k * 0.6180339887 + 0.17) * (hi - lo) }
function gy(k, lo, hi) { return lo + frac(k * 0.3819660113 + 0.61) * (hi - lo) }

function hog(x0, y0, x1, y1, st,    cx, cy) {
	cx = (x0 + x1) / 2; cy = (y0 + y1) / 2
	printf "\t        spawnEnemy mouvement 1 enemyType 1 startPos %.2f %.2f endPos %.2f %.2f controlPoint %.2f %.2f zAxisRot 0 xAxisRot 0 yAxisRot 0 subType %d\n", x0, y0, x1, y1, cx, cy, st
}
function seeker(x) {
	printf "\t        spawnEnemy mouvement 1 enemyType 6 startPos %.2f 1.25 endPos %.2f -1.30 controlPoint %.2f 0 zAxisRot 0 xAxisRot 0 yAxisRot 0 subType 0\n", x, x, x
}
function band(delay, ttl, n) { B_delay[nb] = delay; B_ttl[nb] = ttl; B_n[nb] = n; nb++ }

# --- the three kinds -------------------------------------------------------
# NORMAL: act I's column speed, scattered across the width and 1.9 units of
# height, so the band arrives over ~4 s instead of at once. A third hardened.
function normal(marker, delay, n, k0,    k, x, y) {
	print marker; band(delay, 6500, n)
	for (k = 0; k < n; k++) {
		x = gx(k0 + k, -0.95, 0.95); y = gy(k0 + k, 1.30, 3.20)
		hog(x, y, x, y - 3.30, (k % 3 == 0) ? 1 : 0)
	}
}
# FAST: 1.4 u/s, three and a half times the columns. Straight down, scattered.
function fast(marker, delay, n, k0,    k, x, y) {
	print marker; band(delay, 2200, n)
	for (k = 0; k < n; k++) {
		x = gx(k0 + k, -0.95, 0.95); y = gy(k0 + k, 1.30, 2.10)
		hog(x, y, x, y - 3.00, 0)
	}
}
# IMPROBABLE: steep diagonals from off both edges, crossing each other.
function diag(marker, delay, n, k0,    k, x, y, s) {
	print marker; band(delay, 3000, n)
	for (k = 0; k < n; k++) {
		s = (k % 2) ? 1 : -1
		x = s * (1.30 + frac(k * 0.618) * 0.40); y = gy(k0 + k, 1.30, 2.00)
		hog(x, y, x - s * 2.10, y - 2.80, 0)
	}
}
# IMPROBABLE: crossers at the player's height, cutting the road from the side.
function cross(marker, delay, n, k0,    k, y, s) {
	print marker; band(delay, 2600, n)
	for (k = 0; k < n; k++) {
		s = (k % 2) ? 1 : -1
		y = gy(k0 + k, -1.00, -0.15)
		hog(s * 1.55, y, -s * 1.55, y - 0.15, 0)
	}
}

BEGIN {
	nb = 0
	normal("@@N1@@",  300, 10,  0)
	fast("@@F1@@",  900, 10, 10)
	cross("@@I1@@", 1600,  6, 20)
	print "@@SM1@@"; band(2200, 5000, 8)
	for (k = 0; k < 8; k++) seeker(-0.96 + 0.274 * k)
	fast("@@F2@@", 2800, 10, 30)
	normal("@@N2@@", 3400, 10, 40)
	diag("@@I2@@", 4000,  8, 50)
	fast("@@F3@@", 4800, 10, 60)
	cross("@@I3@@", 5400,  6, 70)
	diag("@@I4@@", 5400,  4, 80)
	print "@@SM2@@"; band(7000, 5000, 7)
	for (k = 0; k < 7; k++) seeker(-0.80 + 0.32 * k)
	normal("@@N3@@", 6800, 10, 90)
	cross("@@I5@@", 7800,  6, 100)

	# ---- the arithmetic -------------------------------------------------
	ne = 0
	for (i = 0; i < nb; i++) {
		E_t[ne] = B_delay[i];              E_d[ne] =  B_n[i]; ne++
		E_t[ne] = B_delay[i] + B_ttl[i];   E_d[ne] = -B_n[i]; ne++
	}
	for (i = 0; i < ne; i++)
		for (j = i + 1; j < ne; j++)
			if (E_t[j] < E_t[i] || (E_t[j] == E_t[i] && E_d[j] < E_d[i])) {
				t = E_t[i]; E_t[i] = E_t[j]; E_t[j] = t
				d = E_d[i]; E_d[i] = E_d[j]; E_d[j] = d }
	alive = 0; peak = 0; total = 0
	for (i = 0; i < ne; i++) {
		alive += E_d[i]
		if (E_d[i] > 0) total += E_d[i]
		if (alive > peak) { peak = alive; peakAt = E_t[i] }
	}
	printf "@@PEAK@@ %d spawned, peak %d alive at +%d ms (cap 64, squall 4)\n", total, peak, peakAt
}
