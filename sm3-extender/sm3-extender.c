#include "sm3.h"

// compute dgst = sm3(secret || data)
void sm3_hash_secret(uint8_t *dgst, const uint8_t *data, size_t dlen) {
    const uint8_t secret[] = {'p', 'a', 's', 's', 'w', 'o', 'r', 'd'};
    sm3_ctx ctx;
    sm3_init(&ctx);
    sm3_update(&ctx, secret, sizeof(secret));
    sm3_update(&ctx, data, dlen);
    sm3_final(&ctx, dgst);
}

int sm3_hash_verify_secret(const uint8_t *dgst, const uint8_t *data,
                           size_t dlen) {
    uint8_t buf[sm3_digest_BYTES];
    sm3_hash_secret(buf, data, dlen);
    return memcmp(dgst, buf, sm3_digest_BYTES);
}

// return the padding length in byte for the
// message of @mlen byte to be hashed with sm3
size_t sm3_padding_size(size_t mlen) {
    // sm3 padding the l-bit message to have a length of multiple 512-bit
    // (64-byte) the padding works by the following rules
    // 1. append bit 1 to the message
    // 2. then, append k-bit 0 s.t. (l + k + 1) = 448 mod 512
    // 3. then, append l as 64-bit big-endian integer
    // for example, 24-bit (0x18) message "abc"  will be padded as (k = 423)
    //   61626380 00000000 00000000 00000000
    //   00000000 00000000 00000000 00000000
    //   00000000 00000000 00000000 00000000
    //   00000000 00000000 00000000 00000018
    //   in short, (l + k + 1) shoud be (64n + 56)-byte long

    size_t left = mlen % 64;
    size_t res = sm3_block_BYTES - left;
    if (left + 9 > sm3_block_BYTES) res += sm3_block_BYTES;
    return res;
}

// save the actual padding value into @pad for the
// message of @mlen byte to be hashed with sm3
void sm3_padding(uint8_t *pad, size_t plen, size_t mlen) {
    memset(pad, 0, plen);
    pad[0] = 0x80;                                 // append bit 1 to message
    uint64_t *p64 = (uint64_t *)(pad + plen - 8);  // padding size always > 8
    p64[0] = __builtin_bswap64(mlen << 3);  // l as 64-bit bigendian integer
}

void printmem(const void *mem, size_t mlen) {
    const uint8_t *p8 = mem;
    for (int i = 0; i < mlen; ++i) {
        printf("%02x", p8[i]);
        if ((i + 1) % 4 == 0) printf(" ");
        if ((i + 1) % 32 == 0) printf("\n");
    }
    if (mlen % 32 != 0) printf("\n");
}

int main() {
    size_t secret_bytes = 8;
    const char *msg = "hello";
    const char *more = "world!";

    uint8_t msg_digest[sm3_digest_BYTES];
    sm3_hash_secret(msg_digest, (const uint8_t *)msg, strlen(msg));
    printf("sm3(secret || msg) = \n");
    printmem(msg_digest, sizeof(msg_digest));

    size_t pad_len = sm3_padding_size(secret_bytes + strlen(msg));
    uint8_t pad[pad_len];
    sm3_padding(pad, pad_len, secret_bytes + strlen(msg));
    printf("padding for \"secret || msg\":\n");
    printmem(pad, pad_len);

    uint8_t target[strlen(msg) + pad_len + strlen(more)];
    memcpy(target, msg, strlen(msg));
    memcpy(target + strlen(msg), pad, pad_len);
    memcpy(target + strlen(msg) + pad_len, more, strlen(more));

    uint8_t msg_more_digest[sm3_digest_BYTES];
    sm3_hash_secret(msg_more_digest, target, sizeof(target));
    printf("via secret value: sm3(secret || msg || pad || more) = \n");
    printmem(msg_more_digest, sizeof(msg_more_digest));

    // sm3 length extension attack
    // compute sm3(secret || msg || pad || more) without secret
    // but with the knowledge of sm3(secret || msg)
    // the length of secret is assumed to be known,
    // which otherwise can be brute forced

    uint8_t buf[secret_bytes + strlen(msg) + pad_len];
    sm3_ctx ctx;
    sm3_init(&ctx);
    sm3_update(&ctx, buf, sizeof(buf));  // restore aux internal state

    // set the internal state to the previous digest
    uint32_t *p32 = (uint32_t *)msg_digest;
    for (int i = 0; i < sm3_digest_BYTES / sizeof(uint32_t); ++i)
        ctx.digest[i] = byte_swap32(p32[i]);
    sm3_update(&ctx, (const uint8_t *)more, strlen(more));
    sm3_final(&ctx, buf);  // use buf to store the crafed digest
    printf("length extension: sm3(secret || msg || pad || more) = \n");
    printmem(buf, sm3_digest_BYTES);

    int valid = sm3_hash_verify_secret(buf, target, sizeof(target));
    printf("%s\n", valid == 0 ? "success" : "failure");
}
