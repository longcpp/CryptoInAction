#include "libsecp256k1_util.hpp"
#include "secp256k1.h"
#include "secp256k1_recovery.h"

const int counts = 10000;

struct secp256k1_ecdsa_bench_context {
    secp256k1_context *ctx;
    unsigned char hash[32];
    unsigned char privkey[32];
    secp256k1_pubkey pubkey;
    secp256k1_ecdsa_signature sig;
    unsigned char sig_der[80];
    size_t sig_len = 80;
    secp256k1_ecdsa_recoverable_signature rsig;
    unsigned char cpubkey[33];
    size_t cpubkey_len = 33;
    unsigned char rsig_compact[64];
    int recid;
};

void secp256k1_ecdsa_bench_setup(secp256k1_ecdsa_bench_context &bctx) {
    bctx.ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN |
                                        SECP256K1_CONTEXT_VERIFY);
    rand_bytes(bctx.hash, bctx.hash + 32);
    CHECK(secp256k1_gen_keypair(bctx.ctx, bctx.privkey, &(bctx.pubkey)));
}

void secp256k1_bench_sign_verify_recover() {
    secp256k1_ecdsa_bench_context bctx;
    secp256k1_ecdsa_bench_setup(bctx);

    // warm up cache and ease the impact of CPU frequency scaling
    for (int i = 0; i < counts; ++i)
        CHECK(secp256k1_ecdsa_sign(bctx.ctx, &bctx.sig, bctx.hash, bctx.privkey,
                                   NULL, NULL));

    // bench sign
    auto sign = [&]() {
        for (int i = 0; i < counts; ++i) {
            CHECK(secp256k1_ecdsa_sign(bctx.ctx, &bctx.sig, bctx.hash,
                                       bctx.privkey, NULL, NULL));
            CHECK(secp256k1_ecdsa_signature_serialize_der(
                bctx.ctx, bctx.sig_der, &bctx.sig_len, &bctx.sig));
        }
    };

    auto milliseconds = perf(sign);
    printf("avg time of per   secp256k1_sign: %.2fus, ",
           milliseconds * 1000 / counts);
    printf("about %.2f sign/s\n", counts / (milliseconds / 1000));

    // bench verify
    auto verify = [&]() {
        for (int i = 0; i < counts; ++i) {
            CHECK(secp256k1_ecdsa_signature_parse_der(
                bctx.ctx, &bctx.sig, bctx.sig_der, bctx.sig_len));
            CHECK(secp256k1_ecdsa_verify(bctx.ctx, &bctx.sig, bctx.hash,
                                         &bctx.pubkey));
        }
    };
    milliseconds = perf(verify);
    printf("avg time of per secp256k1_verify: %.2fus, ",
           milliseconds * 1000 / counts);
    printf("about %.2f verify/s\n", counts / (milliseconds / 1000));

    // bench recoverable sign
    auto recoverable_sign = [&]() {
        for (int i = 0; i < counts; ++i) {
            CHECK(secp256k1_ecdsa_sign_recoverable(
                bctx.ctx, &bctx.rsig, bctx.hash, bctx.privkey, NULL, NULL));
            CHECK(secp256k1_ecdsa_recoverable_signature_serialize_compact(
                bctx.ctx, bctx.rsig_compact, &bctx.recid, &bctx.rsig));
        }
    };

    milliseconds = perf(recoverable_sign);
    printf("avg time of per secp256k1_recoverable_sign: %.2fus, ",
           milliseconds * 1000 / counts);
    printf("about %.2f recoverable_sign/s\n", counts / (milliseconds / 1000));

    // bench public key recovery from signature

    // recover public key from signature
    auto recover = [&]() {
        for (int i = 0; i < counts; ++i) {
            CHECK(secp256k1_ecdsa_recover(bctx.ctx, &bctx.pubkey, &bctx.rsig,
                                          bctx.hash));
            CHECK(secp256k1_ec_pubkey_serialize(bctx.ctx, bctx.cpubkey,
                                                &bctx.cpubkey_len, &bctx.pubkey,
                                                SECP256K1_EC_COMPRESSED));
        }
    };
    milliseconds = perf(recover);
    printf("avg time of per secp256k1_recover: %.2fus, ",
           milliseconds * 1000 / counts);
    printf("about %.2f recover/s\n", counts / (milliseconds / 1000));

    if (!bctx.ctx) secp256k1_context_destroy(bctx.ctx);
}

int main() { secp256k1_bench_sign_verify_recover(); }
