#pragma once

#define CHECK_NULL(ptr)                                                     \
    do {                                                                    \
        if (!(ptr)) {                                                       \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, "null ptr"); \
            abort();                                                        \
        }                                                                   \
    } while (0)

#define CHECK(cond)                                            \
    do {                                                       \
        if (!(cond)) {                                         \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, \
                    "condition test failed");                  \
            abort();                                           \
        }                                                      \
    } while (0)
