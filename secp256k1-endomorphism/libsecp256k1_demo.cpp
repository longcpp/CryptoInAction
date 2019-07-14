#include <cassert>  // std::assert
#include <cstring>  // std::memcmp
#include "libsecp256k1_util.hpp"
#include "secp256k1.h"
#include "secp256k1_ecdh.h"
#include "secp256k1_recovery.h"

int main() {
    // randomize();

    secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY |
                                                      SECP256K1_CONTEXT_SIGN);
    secp256k1_context_set_illegal_callback(ctx, NULL, NULL);
    secp256k1_context_set_error_callback(ctx, NULL, NULL);

    unsigned char hash[32];
    rand_bytes(hash, hash + 32);
    print_bytes("hash", hash, hash + 32);

    unsigned char privkey[32];
    secp256k1_pubkey pubkey;

    CHECK(secp256k1_gen_keypair(ctx, privkey, &pubkey));

    print_bytes("privkey:", privkey, privkey + 32);
    print_bytes(" pubkey:", pubkey.data, pubkey.data + 64);

    // sign/verify and serialize/parse demo

    secp256k1_ecdsa_signature sig;
    CHECK(secp256k1_ecdsa_sign(ctx, &sig, hash, privkey, NULL, NULL));

    unsigned char sig_compact[64];
    secp256k1_ecdsa_signature_serialize_compact(ctx, sig_compact, &sig);
    print_bytes("sig_compact", sig_compact, sig_compact + 64);

    size_t len = 80;
    unsigned char sig_der[len];  // 80 bytes should be enough
    CHECK(secp256k1_ecdsa_signature_serialize_der(ctx, sig_der, &len, &sig));
    printf("length of signature der: %zu\n", len);
    print_bytes("sig_der", sig_der, sig_der + len);

    CHECK(secp256k1_ecdsa_signature_parse_der(ctx, &sig, sig_der, len));

    CHECK(secp256k1_ecdsa_verify(ctx, &sig, hash, &pubkey));

    // recoverable signature and verify with recovered public key

    secp256k1_ecdsa_recoverable_signature rsig;
    CHECK(secp256k1_ecdsa_sign_recoverable(ctx, &rsig, hash, privkey, NULL,
                                           NULL));

    secp256k1_pubkey rpubkey;
    CHECK(secp256k1_ecdsa_recover(ctx, &rpubkey, &rsig, hash));

    print_bytes("recovered pubkey", rpubkey.data, rpubkey.data + 64);
    print_bytes("original  pubkey", pubkey.data, pubkey.data + 64);

    assert(std::memcmp(&rpubkey, &pubkey, sizeof(pubkey)) == 0);

    secp256k1_ecdsa_recoverable_signature_convert(ctx, &sig, &rsig);
    print_bytes(" recoverable sig", rsig.data, rsig.data + 65);
    print_bytes("normal signature", sig.data, sig.data + 64);

    CHECK(secp256k1_ecdsa_verify(ctx, &sig, hash, &rpubkey));

    // ecdh between alice and bob

    secp256k1_context *ctx_alice = secp256k1_context_clone(ctx);
    secp256k1_context *ctx_bob = secp256k1_context_clone(ctx);

    unsigned char privkey_alice[32], privkey_bob[32];
    secp256k1_pubkey pubkey_alice, pubkey_bob;
    CHECK(secp256k1_gen_keypair(ctx_alice, privkey_alice, &pubkey_alice));
    CHECK(secp256k1_gen_keypair(ctx_bob, privkey_bob, &pubkey_bob));

    unsigned char secret_alice[32], secret_bob[32];
    CHECK(secp256k1_ecdh(ctx_alice, secret_alice, &pubkey_bob, privkey_alice));
    print_bytes("secret_alice", secret_alice, secret_alice + 32);

    CHECK(secp256k1_ecdh(ctx_bob, secret_bob, &pubkey_alice, privkey_bob));
    print_bytes("  secret_bob", secret_bob, secret_bob + 32);

    assert(std::memcmp(secret_alice, secret_bob, 32) == 0);

    if (!ctx) secp256k1_context_destroy(ctx);
    if (!ctx_alice) secp256k1_context_destroy(ctx_alice);
    if (!ctx_bob) secp256k1_context_destroy(ctx_bob);

    return 0;
}
