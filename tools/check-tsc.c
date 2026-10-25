#define _GNU_SOURCE
#include <cpuid.h>
#include <errno.h>
#include <math.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <x86intrin.h>

struct measurement { int cpu; double offset; uint64_t bracket; };

static uint64_t now_ns(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &t)) {
        perror("clock_gettime");
        exit(2);
    }
    return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}

static uint64_t tsc(void) {
    unsigned aux;
    return __rdtscp(&aux);
}

static int compare(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(void) {
    unsigned a, b, c, d;
    if (!__get_cpuid(0x80000001, &a, &b, &c, &d) || !(d & (1u << 27))) {
        fprintf(stderr, "RDTSCP is unavailable; this diagnostic requires an x86 CPU with RDTSCP.\n");
        return 2;
    }
    cpu_set_t original, one;
    if (sched_getaffinity(0, sizeof original, &original)) {
        perror("sched_getaffinity");
        return 2;
    }
    int first = -1;
    for (int i = 0; i < CPU_SETSIZE; ++i)
        if (CPU_ISSET(i, &original)) { first = i; break; }
    if (first < 0) return 2;
    CPU_ZERO(&one);
    CPU_SET(first, &one);
    if (sched_setaffinity(0, sizeof one, &one)) {
        perror("sched_setaffinity");
        return 2;
    }

    double frequencies[5];
    for (int i = 0; i < 5; ++i) {
        uint64_t n0 = now_ns(), t0 = tsc();
        struct timespec delay = { .tv_sec = 0, .tv_nsec = 100000000 };
        while (nanosleep(&delay, &delay) && errno == EINTR) {}
        uint64_t t1 = tsc(), n1 = now_ns();
        frequencies[i] = (double)(t1 - t0) * 1e9 / (double)(n1 - n0);
    }
    qsort(frequencies, 5, sizeof frequencies[0], compare);
    double hz = frequencies[2];
    if (!isfinite(hz) || hz <= 0) {
        (void)sched_setaffinity(0, sizeof original, &original);
        fprintf(stderr, "TSC calibration failed.\n");
        return 2;
    }

    struct measurement rows[CPU_SETSIZE];
    double offsets[CPU_SETSIZE];
    int count = 0, result = 0;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (!CPU_ISSET(cpu, &original)) continue;
        CPU_ZERO(&one);
        CPU_SET(cpu, &one);
        if (sched_setaffinity(0, sizeof one, &one)) {
            perror("sched_setaffinity");
            result = 2;
            break;
        }
        struct measurement best = { .cpu = cpu, .offset = 0, .bracket = UINT64_MAX };
        for (int sample = 0; sample < 200; ++sample) {
            uint64_t before = now_ns(), ticks = tsc(), after = now_ns();
            uint64_t bracket = after - before;
            if (bracket < best.bracket) {
                best.bracket = bracket;
                best.offset = (double)ticks / hz - ((double)before + (double)after) / 2e9;
            }
        }
        rows[count] = best;
        offsets[count++] = best.offset;
    }
    if (sched_setaffinity(0, sizeof original, &original)) {
        perror("restoring affinity");
        return 2;
    }
    if (result) return result;
    qsort(offsets, (size_t)count, sizeof offsets[0], compare);
    double median = offsets[count / 2];
    double spread = offsets[count - 1] - offsets[0];
    printf("TSC frequency estimate: %.3f MHz\n", hz / 1e6);
    printf("Offsets relative to the median of CPUs accessible to this process:\n");
    for (int i = 0; i < count; ++i)
        printf("CPU %3d: %+12.6f seconds (measurement bracket %llu ns)\n",
               rows[i].cpu, rows[i].offset - median,
               (unsigned long long)rows[i].bracket);
    printf("Spread: %.6f seconds\n", spread);
    if (count < 2) {
        fprintf(stderr, "Only one CPU is accessible; cannot check synchronization.\n");
        return 2;
    }
    if (spread >= 0.001) {
        puts("WARNING: TSC spread exceeds 1 ms. Cross-check the kernel TSC log.");
        return 1;
    }
    puts("No TSC spread above 1 ms detected in this short measurement.");
    return 0;
}
