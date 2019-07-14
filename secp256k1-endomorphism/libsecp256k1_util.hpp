#pragma once

#include "secp256k1.h"
#include "util.hpp"

inline int secp256k1_gen_keypair(secp256k1_context *ctx,
                                 unsigned char privkey[32],
                                 secp256k1_pubkey *pubkey) {
    do {
        rand_bytes(privkey, privkey + 32);
    } while (!secp256k1_ec_seckey_verify(ctx, privkey));

    return secp256k1_ec_pubkey_create(ctx, pubkey, privkey);
}

#define CHECK(cond)                                            \
    do {                                                       \
        if (!(cond)) {                                         \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, \
                    "test condition failed");                  \
            abort();                                           \
        }                                                      \
    } while (0)
