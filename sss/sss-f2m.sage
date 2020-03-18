gfp128 = GF(2^128, 'x', modulus = x^128 + x^7 + x^2 + x + 1)
t, n = 4, 6
secret = gfp128.random_element().integer_representation()
print "secret", hex(int(secret))

poly = [gfp128.random_element().integer_representation() for i in range(1, t)]
poly[0] = secret


print "poly"
for i in range(0, len(poly)):
	print i, ",", hex(int(poly[i]))

def eval_poly(x, poly):
	res = gfp128.fetch_int(0)
	for coeff in reversed(poly):
		res *= gfp128.fetch_int(x)
		res += gfp128.fetch_int(coeff)
	return res.integer_representation()

shares = [(i, eval_poly(i, poly)) for i in range(1, n+1)]

print "shares"
for i in range(0, len(shares)):
	print "(", shares[i][0], ",", hex(int(shares[i][1])), ")"


def recover_secret(t, shares):
	if len(shares) > t:
		shares = shares[:t]

	k = len(shares)

	res = gfp128.fetch_int(0)
	for i in range(k):
		xi, yi = shares[i][0], shares[i][1]
		print hex(int(xi)), hex(int(yi))

		accum = gfp128.fetch_int(1)
		for j in range(k):
			xj = shares[j][0]
			if i != j:
				accum *= gfp128.fetch_int(xj) / (gfp128.fetch_int(xj)-gfp128.fetch_int(xi))

		res += gfp128.fetch_int(yi) * accum
	return res.integer_representation()

print "recover with shares"
print hex(int(recover_secret(t, shares[0:4])))

print "recover with shares"
print hex(int(recover_secret(t, shares[1:5])))

print "recover with shares"
print hex(int(recover_secret(t, shares[2:6])))