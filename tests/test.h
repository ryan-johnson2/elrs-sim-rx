// Minimal test harness: no dependencies, prints failures, exit code = count.
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

static int test_failures = 0;
static int test_checks = 0;

#define CHECK(cond) do { \
    test_checks++; \
    if (!(cond)) { \
        test_failures++; \
        fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define CHECK_EQ(a, b) do { \
    test_checks++; \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { \
        test_failures++; \
        fprintf(stderr, "  FAIL %s:%d: %s == %s  (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); \
    } \
} while (0)

#define RUN(fn) do { printf("- %s\n", #fn); fn(); } while (0)

#define TEST_MAIN_END() do { \
    printf("%d checks, %d failures\n", test_checks, test_failures); \
    return test_failures ? 1 : 0; \
} while (0)
