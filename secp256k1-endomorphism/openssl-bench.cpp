#include "openssl/ec.h"
#include "openssl/ecdsa.h"
#include "openssl/objects.h"
#include "openssl_util.hpp"
#include "util.hpp"

const int counts = 10000;

struct openssl_ecdsa_bench_context {
    EC_GROUP *ec_group;
    EC_KEY *ec_key;
    unsigned char hash[32];
    ECDSA_SIG *sig;
    unsigned char sig_der[80];
    int sig_der_len;
};

void openssl_ecdsa_bench_setup(openssl_ecdsa_bench_context &ctx, int nid) {
    ctx.ec_group = EC_GROUP_new_by_curve_name(nid);
    CHECK_NULL(ctx.ec_group);

    ctx.ec_key = EC_KEY_new_by_curve_name(nid);
    CHECK_NULL(ctx.ec_key);
    CHECK(EC_KEY_generate_key(ctx.ec_key));
}

void openssl_bench_ecdsa_by_name(int nid) {
    openssl_ecdsa_bench_context ctx;
    openssl_ecdsa_bench_setup(ctx, nid);

    // bench sign
    auto sign = [&]() {
        for (int i = 0; i < counts; ++i) {
            ctx.sig = ECDSA_do_sign(ctx.hash, 32, ctx.ec_key);
            CHECK_NULL(ctx.sig);
            unsigned char *out = ctx.sig_der;
            ctx.sig_der_len = i2d_ECDSA_SIG(ctx.sig, &out);
            if (ctx.sig_der_len > 80) {
                fprintf(stderr, "i2d_ECDSA_SIG overwrite\n");
                abort();
            }
        }
    };

    auto milliseconds = perf(sign);
    printf("avg time of per openssl_%s_ecdsa_sign: %.2fus, ", OBJ_nid2sn(nid),
           milliseconds * 1000 / counts);
    printf("about %.2f sign/s\n", counts / (milliseconds / 1000));

    // bench verify
    auto verify = [&]() {
        for (int i = 0; i < counts; ++i) {
            const unsigned char *der = ctx.sig_der;
            CHECK_NULL(d2i_ECDSA_SIG(&ctx.sig, &der, ctx.sig_der_len));
            CHECK(ECDSA_do_verify(ctx.hash, 32, ctx.sig, ctx.ec_key));
        }
    };

    milliseconds = perf(verify);
    printf("avg time of per openssl_%s_ecdsa_verify: %.2fus, ", OBJ_nid2sn(nid),
           milliseconds * 1000 / counts);
    printf("about %.2f verify/s\n", counts / (milliseconds / 1000));

    if (!ctx.ec_group) EC_GROUP_free(ctx.ec_group);
    if (!ctx.ec_key) EC_KEY_free(ctx.ec_key);
    if (!ctx.sig) ECDSA_SIG_free(ctx.sig);
}

int main() {
    openssl_bench_ecdsa_by_name(NID_secp256k1);
    openssl_bench_ecdsa_by_name(NID_X9_62_prime256v1);
}
