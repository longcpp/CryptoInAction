#pragma once

#include "util.h"

#define sm3_digest_BYTES 32
#define sm3_block_BYTES 64

typedef struct sm3_ctx_t {
    uint32_t digest[sm3_digest_BYTES / sizeof(uint32_t)];
    int nblocks;  // number of blocks that have been processed
    uint8_t block[sm3_block_BYTES];
    int num;
} sm3_ctx;

void sm3_init(sm3_ctx *ctx);
void sm3_update(sm3_ctx *ctx, const uint8_t *data, size_t data_len);
void sm3_final(sm3_ctx *ctx, uint8_t *digest);

void sm3_hash(uint8_t *digest, const uint8_t *data, size_t dlen);
int sm3_hash_verify(const uint8_t *data, size_t dlen, const uint8_t *digest);
