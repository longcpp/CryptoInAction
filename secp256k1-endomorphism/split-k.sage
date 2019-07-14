def split_k(a, b, k):
	s, old_s = 0, 1
	t, old_t = 1, 0
	r, old_r = b, a

	print "k", hex(int(k))
	
	# invariant r = sa + tb
	flag = false
	rt = []
	while r != 0:
		if old_r >= sqrt(a) and r < sqrt(a):
			rt.append((old_r, old_t))
			rt.append((r,t))
			flag = true

		q = floor(old_r / r)

		old_r, r = r, old_r - q * r
		old_s, s = s, old_s - q * s
		old_t, t = t, old_t - q * t

		if flag == true:
			rt.append((r,t))
			flag = false

	(r0, t0) = rt[0]
	(r1, t1) = rt[1]
	(r2, t2) = rt[2]
	
	a1, b1 = r1, -t1
	print "a1, b1", hex(int(a1)), hex(int(b1))

	if r0 * r0 + t0 * t0 <= r2 * r2 + t2 * t2:
		a2, b2 = r0, -t0
	else:
		a2, b2 = r2, -t2
	print "a2, b2", hex(int(a2)), hex(int(b2))

	c1, c2 = round(b2 * k / a), round(-b1 * k / a)
	print "c1, c2", hex(int(c1)), hex(int(c2))

	k1, k2 = k - c1 * a1 - c2 * a2, -c1 * b1 - c2 * b2
	print "k1, k2", hex(int(k1)), hex(int(k2))

	return k1, k2
