cp1
num_frames 36

# Act-3 boss rail. Frames 0..140000 are act2.cp VERBATIM (same flight the
# player knows), minus act2's final 142000 outro keyframe (a 2s dive+bank
# meant as the act-2 exit shot -- unusable as a fight backdrop).
# Then: a 180 U-turn written as TWO 90-degree segments (one full slerp of
# 180 would be ambiguous), a straight top-down return flight to the city
# start at cruise speed, a second U-turn, and a last forward leg ending
# mid-corridor in a straight top-down pose (so the engine's end-of-path
# pull-back + hover lands on a clean view). Visibility is baked for the
# whole trip by PREPROC at .cp -> .cp2b conversion: the return leg shows
# the real city, not a frozen set.

time 00000:  position (14,100,-410)		 lookat (0,110,-380)		 upVector (0,1,0)
time 04000:  position (14,115,-1345)		 lookat (0,110,-1345)		 upVector (0,1,0)
time 08000:  position (14,117,-2250)		lookat (60,100,-300000)	 upVector (0,1,0)

time 09000:  position (0,162,-2560)  lookat (0,-110,-2560)	 upVector (0,0,-1)
time 15000:  position (0,162,-4040)  lookat (0,-100,-4040)			upVector (0,-0.173,-0.984)

time 20000:  position (0,162,-5240)  lookat (0,-162,-5240)			upVector (0,-0.173,-0.984)
time 25000:  position (50,162,-6400)  lookat (-100,162,-6400)		upVector (0,-0.173,-0.984)
time 30000:  position (0,162,-7600)  lookat (0,163,-7600)			upVector (0,-0.173,-0.984)
time 35000:  position (-50,162,-8800)  lookat (100,162,-8800)		upVector (0,-0.173,-0.984)

time 40000:  position (0,162,  -10000)  lookat (0,-162,-10000)		upVector (0,-0.173,-0.984)
time 45000:  position (50,162, -11200)  lookat (-100,162,-11200)	upVector (0,-0.173,-0.984)
time 50000:  position (0,162,  -12400)  lookat (0,163,-12400)		upVector (0,-0.173,-0.984)
time 55000:  position (-50,162,-13600)  lookat (100,162,-13600)		upVector (0,-0.173,-0.984)

time 60000:  position (0,162,  -14800)  lookat (0,-162,  -14800)		upVector (0,-0.173,-0.984)
time 65000:  position (50,162, -16000)  lookat (-100,162,-16000)	upVector (0,-0.173,-0.984)
time 70000:  position (0,162,  -17200)  lookat (0,163,   -17200)		upVector (0,-0.173,-0.984)
time 75000:  position (-50,162,-18400)  lookat (100,162, -18400)		upVector (0,-0.173,-0.984)

time 80000:  position (0,162,  -19600)  lookat (0,-162,  -19600)		upVector (0,-0.173,-0.984)
time 85000:  position (50,162, -20800)  lookat (-100,162,-20800)	upVector (0,-0.173,-0.984)
time 90000:  position (0,162,  -22000)  lookat (0,163,   -22000)		upVector (0,-0.173,-0.984)
time 95000:  position (-50,162,-23200)  lookat (100,162, -23200)		upVector (0,-0.173,-0.984)

time 100000:  position (0,162,  -24400)  lookat (0,-162,  -24400)		upVector (0,-0.173,-0.984)
time 105000:  position (50,162, -25600)  lookat (-100,162,-25600)	upVector (0,-0.173,-0.984)
time 110000:  position (0,162,  -27800)  lookat (0,163,   -27800)		upVector (0,-0.173,-0.984)
time 115000:  position (-50,162,-29000)  lookat (100,162, -29000)	upVector (0,-0.173,-0.984)

time 120000:  position (0,162,  -30200)  lookat (0,-162,   -30200)		upVector (0,-0.173,-0.984)
time 125000:  position (50,162, -31400)  lookat (-100,162,-31400)	upVector (0,-0.173,-0.984)
time 130000:  position (0,162,  -32600)  lookat (0,163,    -32600)		upVector (0,-0.173,-0.984)
time 135000:  position (-50,162,-33800)  lookat (100,162,  -33800)	upVector (0,-0.173,-0.984)

time 140000:  position (0,162,  -35000)  lookat (0,-162,-35000)		upVector (0,-0.173,-0.984)

# U-turn (2 x 90 degrees about the vertical, decelerating), then the return.
time 143000:  position (0,162,-35150)  lookat (0,-162,-35150)		upVector (-0.984,-0.173,0)
time 146000:  position (0,162,-35200)  lookat (0,-162,-35200)		upVector (0,-0.173,0.984)

time 282000:  position (0,162,-2650)   lookat (0,-162,-2650)		upVector (0,-0.173,0.984)

# Second U-turn at the city start (same spin direction), then re-advance.
time 285000:  position (0,162,-2620)   lookat (0,-162,-2620)		upVector (0.984,-0.173,0)
time 288000:  position (0,162,-2600)   lookat (0,-162,-2600)		upVector (0,-0.173,-0.984)

time 318000:  position (0,162,-9800)   lookat (0,-162,-9800)		upVector (0,-0.173,-0.984)
