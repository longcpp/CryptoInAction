#include <iostream>
#include <chrono>
#include <cstdio>
#include "sodium.h"

template <typename Func, typename Clock = std::chrono::steady_clock,
        typename Unit = std::chrono::milliseconds>
inline double perf(Func f) {
    auto t1 = Clock::now();
    f();
    auto t2 = Clock::now();
    return std::chrono::duration_cast<Unit>(t2 - t1).count();
}

const int counts = 10000;
const int msglen = 32;

struct libsodium_ed25519_context {
    unsigned char secret[crypto_sign_SECRETKEYBYTES];
    unsigned char pubkey[crypto_sign_PUBLICKEYBYTES];
    unsigned char message[msglen];
    unsigned char signature[crypto_sign_BYTES];
};

void libsodium_bench_ed25519_sign_verify() {
    libsodium_ed25519_context ctx{};
    crypto_sign_keypair(ctx.pubkey, ctx.secret);

    auto sign = [&]() {
        for(int i = 0; i < counts; ++i) {
            crypto_sign_detached(ctx.signature, NULL, 
                ctx.message, msglen, ctx.secret);
        }
    };

    auto milliseconds = perf(sign);
    printf("avg time of per ed25519_sign: %.2f us, ",
            milliseconds * 1000 / counts);
    printf("about %.2f sign/s\n", counts / (milliseconds / 1000));

    auto verify = [&]() {
        for(int i = 0; i < counts; ++i) {
            if(crypto_sign_verify_detached(ctx.signature, ctx.message, 
                msglen, ctx.pubkey) != 0) {
                fprintf(stderr, "signature verification failed\n");
                exit(1);
            }
        }
    };

    milliseconds = perf(verify);
    printf("avg time of per ed25519_verify: %.2f us, ",
           milliseconds * 1000 / counts);
    printf("about %.2f verify/s\n", counts / (milliseconds / 1000));
}

int main() {
    libsodium_bench_ed25519_sign_verify();
    return 0;
}