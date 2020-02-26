#include "sm3.h"

#include <assert.h>
#include <stdio.h>

#include "openssl/evp.h"

int sm3_hash_openssl(uint8_t *dgst, const void *msg, size_t len) {
    int res = 0;
    const EVP_MD *md = EVP_get_digestbyname("sm3");
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) goto done;

    EVP_DigestInit_ex(mdctx, md, NULL);
    EVP_DigestUpdate(mdctx, msg, len);
    res = EVP_DigestFinal_ex(mdctx, dgst, NULL);

done:
    EVP_MD_CTX_free(mdctx);
    return res;
}

int sm3_hash_verify_openssl(const void *msg, size_t len, const void *dgst) {
    uint8_t buf[32];
    sm3_hash_openssl(buf, msg, len);
    return memcmp(buf, dgst, 32);
}

void verify_sm3_with_openssl() {  // verify sm3 impl with openssl's sm3 impl
    printf("%s ...\n", __PRETTY_FUNCTION__);
    uint8_t digest[sm3_digest_BYTES], digest_openssl[sm3_digest_BYTES];
    for (int mlen = 0; mlen <= 1024; ++mlen) {
        uint8_t data[mlen];
        for (int i = 0; i < mlen; ++i) data[mlen] = rand() & 0xff;
        sm3_hash(digest, data, mlen);
        sm3_hash_openssl(digest_openssl, data, mlen);
        assert(memcmp(digest, digest_openssl, sizeof(digest)) == 0);
    }
    printf("%s done\n", __PRETTY_FUNCTION__);
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

int main() { verify_sm3_with_openssl(); }

