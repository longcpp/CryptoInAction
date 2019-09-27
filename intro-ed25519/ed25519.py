import hashlib

def sha512(m):
    return hashlib.sha512(m).digest()

p = 2**255 - 19 # characteristic of base field

def fp_inv(x):
    return pow(x, p-2, p)

d = -121665 * fp_inv(121666) % p # Edwards25519 parameter

l = 2**252 + 27742317777372353535851937790883648493
def sha512_fp(m):
    return int.from_bytes(sha512(m), "little") % l

# Points are represented as tuples (X, Y, Z, T) of extended coordinates
# with x = X/Z, y = Y / Z, x * y = T/Z

def point_add(P, Q):    
    A = (P[1]-P[0]) * (Q[1]-Q[0]) % p
    B = (P[1]+P[0]) * (Q[1]+Q[0]) % p
    C = 2 * P[3] * Q[3] * d % p
    D = 2 * P[2] * Q[2] % p
    E, F, G, H = (B-A)%p, (D-C)%p, (D+C)%p, (B+A)%p
    return (E*F % p, G*H % p, F*G % p, E*H % p)

def point_double(P):
    return point_add(P, P)

def point_mul(s, P):
    Q = (0, 1, 1, 0) # Netural element
    while s > 0:
        if s & 1:
            Q = point_add(Q, P)
        P = point_double(P)
        s >>= 1
    return Q

def point_equal(P, Q):
    # X1 / Z1 == X2 / Z2 --> X1 * Z2 = X2 * Z1
    # Y1 / Z1 == Y2 / Z2 --> Y1 * Z2 = Y2 * Z1
    if (P[0] * Q[2] - Q[0] * P[2]) % p != 0:
        return False
    if (P[1] * Q[2] - Q[1] * P[2]) % p != 0:
        return False
    return True

# square root of -1
fp_sqrt_m1 = 0x2b8324804fc1df0b2b4d00993dfbd7a72f431806ad2fe478c4ee1b274a0ea0b0

# recover x-coordinate
def recover_x(y, sign):
    if y >= p:
        return None
    x2 = (y*y - 1) * fp_inv(d*y*y + 1)
    if x2 == 0:
        if sign:
            return None
        else:
            return 0
    # Compute square root of x2
    x = pow(x2, (p+3) // 8, p)
    if (x*x - x2) % p != 0:
        x = x * fp_sqrt_m1 % p
    if (x*x - x2) % p != 0:
        return None

    if (x & 1) != sign:
        x = p - x
    return x

# Base point
g_x = 0x216936d3cd6e53fec0a4e231fdd6dc5c692cc7609525a7b2c9562d608f25d51a
g_y = 0x6666666666666666666666666666666666666666666666666666666666666658
G = (g_x, g_y, 1, g_x * g_y % p)

def point_compress(point):
    zinv = fp_inv(point[2])
    x = point[0] * zinv % p
    y = point[1] * zinv % p
    return int.to_bytes(y | ((x & 1) << 255), 32, "little")

def point_decompress(bytes):
    if len(bytes) != 32:
        raise Exception("Invalid input length for decompression")
    y = int.from_bytes(bytes, "little")
    sign = y >> 255 # get the sign bit
    y &= (1 << 255) - 1 # clear the sign bit

    x = recover_x(y, sign)
    if x is None:
        return None
    else:
        return (x, y, 1, x * y % p)

def secret_expand(secret):
    if len(secret) != 32:
        raise Exception("Bad size of private key")
    h = sha512(secret)
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= (1 << 254)
    return (a, h[32:])

def secret_to_pubkey(secret):
    (a, dummy) = secret_expand(secret)
    A = point_mul(a, G)
    return point_compress(A)

def ed25519_sign(secret, msg):
    a, prefix = secret_expand(secret)
    A = point_compress(point_mul(a, G))
    r = sha512_fp(prefix + msg)
    R = point_compress(point_mul(r, G))
    h = sha512_fp(R + A + msg)
    s = (r + h * a) % p
    return R + int.to_bytes(s, 32, "little")

def ed25519_verify(pubkey, msg, signature):
    if len(pubkey) != 32:
        raise Exception("Bad public key length")
    if len(signature) != 64:
        raise Exception("Bad signature length")

    A = point_decompress(pubkey)
    if not A:
        return False

    Rbytes = signature[:32]
    R = point_decompress(Rbytes)
    if not R:
        return False

    s = int.from_bytes(signature[32:], "little")
    if s >= p:
        return False

    k = sha512_fp(Rbytes + pubkey + msg)
    sG = point_mul(8*s, G)
    kA = point_mul(8*k, A)
    return point_equal(sG, point_add(point_mul(8, R), kA))