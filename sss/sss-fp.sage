gfp128 = GF(2^128-159)
t, n = 4, 6
secret = gfp128.random_element()
print "secret", hex(int(secret))

poly = [gfp128.random_element() for i in range(1, t)]
poly[0] = secret


print "poly"
for i in range(0, len(poly)):
	print i, ",", hex(int(poly[i]))

def eval_poly(x, poly):
	res = gfp128(0)
	for coeff in reversed(poly):
		res *= x
		res += coeff
	return res

shares = [(gfp128(i), eval_poly(gfp128(i), poly)) for i in range(1, n+1)]

print "shares"
for i in range(0, len(shares)):
	print "(", shares[i][0], ",", hex(int(shares[i][1])), ")"

def recover_secret(t, shares):
	if len(shares) > t:
		shares = shares[:t]

	k = len(shares)

	res = gfp128(0)
	for i in range(k):
		xi, yi = shares[i][0], shares[i][1]
		print hex(int(xi)), hex(int(yi))

		accum = gfp128(1)
		for j in range(k):
			xj = shares[j][0]
			if i != j:
				accum *= xj / (xj-xi)

		res += yi * accum
	return res

print "recover with shares"
print hex(int(recover_secret(t, shares[0:4])))

print "recover with shares"
print hex(int(recover_secret(t, shares[1:5])))

print "recover with shares"
print hex(int(recover_secret(t, shares[2:6])))