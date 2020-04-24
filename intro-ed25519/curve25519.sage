fp25519 = FiniteField(2^255 - 19)
A, B = fp25519(486662), fp25519(1)
curve25519 = EllipticCurve(fp25519, [0, A, 0, B, 0])
ell = curve25519.cardinality() / 8

mont_G = curve25519.point((0x9,
0x5f51e65e475f794b1fe122d388b72eb36dc2b28192839e4dd6163a5d81312c14))

def curve25519_8_torsion():
    point = curve25519.random_element()
    while true:
        point = int(ell) * point;
        if point.order() == 8:
            break
        point = curve25519.random_element()

    torsion8 = [(i * point) for i in range(1, 9)]

    return torsion8

def print_point(point):
    print "order = %d" % point.order()
    if point.order() == 1:
        print "Inf"
    else:
        u, v = point.xy()
        print "(%064x,\n %064x)" % (u, v)

def mont_to_edwards25519(point):
    if point.order() == 1:
        return (fp25519(0), fp25519(1))

    u, v = point.xy()
    if u == 0 and v == 0:
        return (fp25519(0), fp25519(-1))
    
    x = sqrt(fp25519(-486664)) * u / v
    y = (u - 1) / (u + 1)
    return (x, y)

edwards_G = mont_to_edwards25519(mont_G)
print "basepoint for curve25519\n%064x\n%064x" % (mont_G.xy())
print "basepoint for edwards25519\n%064x\n%064x" % (edwards_G)

mont_torsion8 = curve25519_8_torsion()

print "8-torsion of mont curve: curve25519"
for point in mont_torsion8:
    print_point(point)

ed_torsion8 = [mont_to_edwards25519(p) for p in mont_torsion8]
ed_torsion8_order = [p.order() for p in mont_torsion8]

print "8-torsion of edwards curve: edwards25519"
for i in range(0, len(ed_torsion8)):
    print "order = %d" % ed_torsion8_order[i]
    (x, y) = ed_torsion8[i]
    print "(%064x,\n %064x)" % (x, y)

