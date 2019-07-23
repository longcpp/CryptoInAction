#include <math.h>
#include <stdio.h>
#include <string.h>
#include "sodium.h"
#include "sys/time.h"

static double gettimedouble(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_usec * 0.000001 + tv.tv_sec;
}

void print_number(double x) {
    double y = x;
    int c = 0;
    if (y < 0.0) {
        y = -y;
    }
    while (y > 0 && y < 100.0) {
        y *= 10.0;
        c++;
    }
    printf("%.*f", c, x);
}

void run_benchmark(char *name, int (*benchmark)(void *), void (*setup)(void *),
                   void (*teardown)(void *), void *data, int count, int iter) {
    int i;
    double min = HUGE_VAL;
    double sum = 0.0;
    double max = 0.0;
    for (i = 0; i < count; i++) {
        if (setup != NULL) setup(data);

        double begin, total;
        begin = gettimedouble();
        benchmark(data);
        total = gettimedouble() - begin;

        if (teardown != NULL) teardown(data);

        if (total < min) min = total;
        if (total > max) max = total;

        sum += total;
    }
    printf("%s: min ", name);
    print_number(min * 1000000.0 / iter);
    printf("us / avg ");
    print_number((sum / count) * 1000000.0 / iter);
    printf("us / max ");
    print_number(max * 1000000.0 / iter);
    printf("us\n");
}

#define MESSAGE_LEN 32
#define COUNT_NUM 1000

typedef struct {
    unsigned char *sk;
    unsigned char *pk;
    unsigned char **msgs;
    unsigned char **sigs;
} bench_sig_data;

int bench_ed25519_sign(void *args) {
    bench_sig_data *data = (bench_sig_data *)args;

    for (int i = 0; i < COUNT_NUM; ++i) {
        crypto_sign_detached(data->sigs[i], NULL, data->msgs[i], MESSAGE_LEN,
                             data->sk);
    }
    return 0;
}

int bench_ed25519_verify(void *args) {
    bench_sig_data *data = (bench_sig_data *)args;

    for (int i = 0; i < COUNT_NUM; ++i) {
        if (crypto_sign_verify_detached(data->sigs[i], data->msgs[i],
                                        MESSAGE_LEN, data->pk) != 0) {
            return -1;
        }
    }
    return 0;
}

int main() {
    bench_sig_data data;

    data.msgs = (unsigned char **)malloc(COUNT_NUM * sizeof(unsigned char *));
    data.sk = (unsigned char *)malloc(crypto_sign_SECRETKEYBYTES);
    data.pk = (unsigned char *)malloc(crypto_sign_PUBLICKEYBYTES);
    data.sigs = (unsigned char **)malloc(COUNT_NUM * sizeof(unsigned char *));

    crypto_sign_keypair(data.pk, data.sk);

    for (int i = 0; i < COUNT_NUM; ++i) {
        data.msgs[i] = (unsigned char *)malloc(MESSAGE_LEN);
        data.sigs[i] = (unsigned char *)malloc(crypto_sign_BYTES);
    }

    run_benchmark("bench_sign", bench_ed25519_sign, NULL, NULL, &data, 10,
                  1000);
    run_benchmark("bench_verify", bench_ed25519_verify, NULL, NULL, &data, 10,
                  1000);

    for (int i = 0; i < COUNT_NUM; ++i) {
        free(data.msgs[i]);
        free(data.sigs[i]);
    }

    free(data.sk);
    free(data.pk);
    free(data.msgs);
    free(data.sigs);

    return 0;
}
