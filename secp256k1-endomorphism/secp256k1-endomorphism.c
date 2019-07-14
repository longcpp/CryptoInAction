#include <openssl/crypto.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/engine.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <sys/time.h>

#define REPS 3000

// Split a secp256k1 exponent k into two smaller ones k1 and k2 such that for
// any point Y, k*Y = k1*Y + k2*Y', where Y' = lambda*Y is very fast
static int splitk(BIGNUM *bnk1, BIGNUM *bnk2, const BIGNUM *bnk,
                  const BIGNUM *bnn, BN_CTX *ctx) {
    BN_CTX *bnctx = NULL;
    BIGNUM *bnc1 = NULL, *bnc2 = NULL, *bnt1 = NULL, *bnt2 = NULL, *bnn2 = NULL;
    BIGNUM *bna1b2 = NULL, *bnb1m = NULL, *bna2 = NULL;
    if (!(bnctx = BN_CTX_new())) goto done;
    BN_CTX_start(bnctx);
    bnc1 = BN_CTX_get(bnctx), bnc2 = BN_CTX_get(bnctx);
    bnt1 = BN_CTX_get(bnctx), bnt2 = BN_CTX_get(bnctx);
    bnn2 = BN_CTX_get(bnctx);
    bna1b2 = BN_CTX_get(bnctx);
    bnb1m = BN_CTX_get(bnctx);
    if (!(bna2 = BN_CTX_get(bnctx))) goto done;

    static unsigned char a1b2[] = {
        0x30, 0x86, 0xd2, 0x21, 0xa7, 0xd4, 0x6b, 0xcd,
        0xe8, 0x6c, 0x90, 0xe4, 0x92, 0x84, 0xeb, 0x15,
    };
    static unsigned char b1m[] = {
        0xe4, 0x43, 0x7e, 0xd6, 0x01, 0x0e, 0x88, 0x28,
        0x6f, 0x54, 0x7f, 0xa9, 0x0a, 0xbf, 0xe4, 0xc3,
    };
    static unsigned char a2[] = {
        0x01, 0x14, 0xca, 0x50, 0xf7, 0xa8, 0xe2, 0xf3, 0xf6,
        0x57, 0xc1, 0x10, 0x8d, 0x9d, 0x44, 0xcf, 0xd8,
    };
    BN_bin2bn(a1b2, sizeof(a1b2), bna1b2);
    BN_bin2bn(b1m, sizeof(b1m), bnb1m);
    BN_bin2bn(a2, sizeof(a2), bna2);

    BN_rshift1(bnn2, bnn);               // n2 = n / 2
    BN_mul(bnc1, bnk, bna1b2, ctx);      // c1 = b2 * k
    BN_add(bnc1, bnc1, bnn2);            // c1 = b2 * k + n / 2
    BN_div(bnc1, NULL, bnc1, bnn, ctx);  // c1 = round(b2 * k / n)
    BN_mul(bnc2, bnk, bnb1m, ctx);       // c2 = -b1 * k
    BN_add(bnc2, bnc2, bnn2);            // c2 = -b1 * k + n /2
    BN_div(bnc2, NULL, bnc2, bnn, ctx);  // c2 = round(-b1 * k  / n)

    BN_mul(bnt1, bnc1, bna1b2, ctx);  // t1 = c1 * a1
    BN_mul(bnt2, bnc2, bna2, ctx);    // t2 = c2 * a2
    BN_add(bnt1, bnt1, bnt2);         // t1 = c1 * a1 + c2 * a2
    BN_sub(bnk1, bnk, bnt1);          // k1 = k - (c1 * a1 + c2 * a2)
    BN_mul(bnt1, bnc1, bnb1m, ctx);   // t1 = c1 * (-b1)
    BN_mul(bnt2, bnc2, bna1b2, ctx);  // t2 = c2 * b2
    BN_sub(bnk2, bnt1, bnt2);         // k2 = -c1 * b1 - c2 * b2

done:
    if (bnctx) {
        BN_CTX_end(bnctx);
        BN_CTX_free(bnctx);
    }
    return 0;
}

static int secp256k1Verify(const unsigned char hash[32],
                           const unsigned char *dersig, size_t sigsize,
                           const EC_KEY *pkey) {
    int rslt = 0;
    const EC_GROUP *group = EC_KEY_get0_group(pkey);
    const EC_POINT *Y = EC_KEY_get0_public_key(pkey);
    const EC_POINT *G = EC_GROUP_get0_generator(group);
    EC_POINT *Glam = EC_POINT_new(group);
    EC_POINT *Ylam = EC_POINT_new(group);
    EC_POINT *R = EC_POINT_new(group);
    const EC_POINT *Points[3];
    const BIGNUM *bnexps[3];

    BN_CTX *ctx = NULL;
    BIGNUM *bnp = NULL, *bnn = NULL, *bnx = NULL, *bny = NULL;
    BIGNUM *bnk = NULL, *bnk1 = NULL, *bnk2 = NULL, *bnk1a = NULL;
    BIGNUM *bnk2a = NULL, *bnsinv = NULL, *bnh = NULL, *bnbeta = NULL;
    if (!(ctx = BN_CTX_new())) goto done;
    BN_CTX_start(ctx);
    bnp = BN_CTX_get(ctx), bnn = BN_CTX_get(ctx);
    bnx = BN_CTX_get(ctx), bny = BN_CTX_get(ctx);
    bnk = BN_CTX_get(ctx), bnk1 = BN_CTX_get(ctx), bnk2 = BN_CTX_get(ctx);
    bnk1a = BN_CTX_get(ctx), bnk2a = BN_CTX_get(ctx);
    bnsinv = BN_CTX_get(ctx), bnh = BN_CTX_get(ctx);
    if (!(bnbeta = BN_CTX_get(ctx))) goto done;

    BN_bin2bn(hash, 32, bnh);

    static unsigned char beta[] = {
        0x7a, 0xe9, 0x6a, 0x2b, 0x65, 0x7c, 0x07, 0x10, 0x6e, 0x64, 0x47,
        0x9e, 0xac, 0x34, 0x34, 0xe9, 0x9c, 0xf0, 0x49, 0x75, 0x12, 0xf5,
        0x89, 0x95, 0xc1, 0x39, 0x6c, 0x28, 0x71, 0x95, 0x01, 0xee,
    };
    BN_bin2bn(beta, 32, bnbeta);
    ECDSA_SIG *sig = d2i_ECDSA_SIG(NULL, &dersig, sigsize);

    if (sig == NULL) goto done;

    EC_GROUP_get_curve_GFp(group, bnp, NULL, NULL, ctx);
    EC_GROUP_get_order(group, bnn, ctx);

    if (BN_is_zero(ECDSA_SIG_get0_r(sig)) ||
        BN_is_negative(ECDSA_SIG_get0_r(sig)) ||
        BN_ucmp(ECDSA_SIG_get0_r(sig), bnn) >= 0 ||
        BN_is_zero(ECDSA_SIG_get0_s(sig)) ||
        BN_is_negative(ECDSA_SIG_get0_s(sig)) ||
        BN_ucmp(ECDSA_SIG_get0_s(sig), bnn) >= 0)
        goto done;

    // bnx = Gx, bny = Gy
    EC_POINT_get_affine_coordinates_GFp(group, G, bnx, bny, ctx);
    BN_mod_mul(bnx, bnx, bnbeta, bnp, ctx);  // bnx = bnx * beta mod p
    // Glam = (beta*x, y)
    EC_POINT_set_affine_coordinates_GFp(group, Glam, bnx, bny, ctx);
    EC_POINT_get_affine_coordinates_GFp(group, Y, bnx, bny, ctx);
    BN_mod_mul(bnx, bnx, bnbeta, bnp, ctx);
    EC_POINT_set_affine_coordinates_GFp(group, Ylam, bnx, bny, ctx);  // Ylam

    Points[0] = Glam;
    Points[1] = Y;
    Points[2] = Ylam;
    // sinv = s^-1 mod n
    BN_mod_inverse(bnsinv, ECDSA_SIG_get0_s(sig), bnn, ctx);
    BN_mod_mul(bnk, bnh, bnsinv, bnn, ctx);  // bnk = h * s^-1
    splitk(bnk1, bnk2, bnk, bnn, ctx);
    bnexps[0] = bnk2;
    BN_mod_mul(bnk, ECDSA_SIG_get0_r(sig), bnsinv, bnn, ctx);  // bnk = r * s^-1
    splitk(bnk1a, bnk2a, bnk, bnn, ctx);
    bnexps[1] = bnk1a;
    bnexps[2] = bnk2a;

    EC_POINTs_mul(group, R, bnk1, 3, Points, bnexps, ctx);
    EC_POINT_get_affine_coordinates_GFp(group, R, bnx, NULL, ctx);
    BN_mod(bnx, bnx, bnn, ctx);
    rslt = (BN_cmp(bnx, ECDSA_SIG_get0_r(sig)) == 0);

    ECDSA_SIG_free(sig);
done:
    if (ctx) {
        BN_CTX_end(ctx);
        BN_CTX_free(ctx);
    }
    EC_POINT_free(Glam);
    EC_POINT_free(Ylam);
    EC_POINT_free(R);

    return rslt;
}

int main() {
    EC_KEY *pkey;
    EC_GROUP *group;
    const EC_POINT *ecpub;
    unsigned char sig[100];
    unsigned siglen = sizeof(sig);
    unsigned char hash[32];
    struct timeval tv1, tv2;
    double time1, time2;
    int i;
    int rslt;

    group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    pkey = EC_KEY_new();
    EC_KEY_set_group(pkey, group);
    EC_KEY_generate_key(pkey);
    ecpub = EC_KEY_get0_public_key(pkey);
    ECDSA_sign(0, hash, 32, sig, &siglen, pkey);

    rslt = ECDSA_verify(0, hash, 32, sig, siglen, pkey);
    printf("rslt = %d\n", rslt);

    rslt = secp256k1Verify(hash, sig, siglen, pkey);
    printf("rslt = %d\n", rslt);

    hash[0]++;

    rslt = ECDSA_verify(0, hash, 32, sig, siglen, pkey);
    printf("rslt = %d\n", rslt);

    rslt = secp256k1Verify(hash, sig, siglen, pkey);
    printf("rslt = %d\n", rslt);

    hash[0]--;

    gettimeofday(&tv1, NULL);
    for (i = 0; i < REPS; i++) {
        rslt = ECDSA_verify(0, hash, 32, sig, siglen, pkey);
    }
    gettimeofday(&tv2, NULL);
    printf("rslt = %d\n", rslt);
    time1 = (tv2.tv_sec - tv1.tv_sec + (tv2.tv_usec - tv1.tv_usec) / 1000000.) /
            REPS;
    printf("time: %g\n", time1);

    gettimeofday(&tv1, NULL);
    for (i = 0; i < REPS; i++) {
        rslt = secp256k1Verify(hash, sig, siglen, pkey);
    }
    gettimeofday(&tv2, NULL);
    printf("rslt = %d\n", rslt);
    time2 = (tv2.tv_sec - tv1.tv_sec + (tv2.tv_usec - tv1.tv_usec) / 1000000.) /
            REPS;
    printf("time: %g\n", time2);
    printf("%f%% speedup\n", (time1 - time2) / time1);

    exit(0);
}
