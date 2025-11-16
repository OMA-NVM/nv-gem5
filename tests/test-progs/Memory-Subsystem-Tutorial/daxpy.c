/**
 * Minimal DAXPY example for gem5 tutorial.
 *
 * Performs y += a * x twice:
 *   1) on DRAM-backed arrays allocated with malloc
 *   2) on statically placed arrays in the NVM address range
 *
 * The NVM arrays assume the hybrid_example.py script maps the second
 * memory range starting at 0x200000000 into the process address space.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef STREAM_ARRAY_SIZE
#define STREAM_ARRAY_SIZE 1024
#endif

/* Base of the NVM range configured in hybrid_example.py */
#define NVM_BASE 0x200000000ull

/* Place x and y back-to-back in NVM */
#define NVM_X_ADDR ((double *const)(NVM_BASE))
#define NVM_Y_ADDR \
    ((double *const)(NVM_BASE + (STREAM_ARRAY_SIZE * sizeof(double))))

static void
daxpy(size_t n, double a, const double *x, double *y)
{
    for (size_t i = 0; i < n; ++i) {
        y[i] += a * x[i];
    }
}

static void
print_sample(const char *label, const double *y, size_t n)
{
    printf("%s: y[0]=%.2f  y[%zu]=%.2f\n", label, y[0], n - 1, y[n - 1]);
}

int
main(void)
{
    const size_t n = STREAM_ARRAY_SIZE;
    const double a = 2.5;

    /* DRAM-backed arrays */
    double *dram_x = malloc(n * sizeof(double));
    double *dram_y = malloc(n * sizeof(double));
    if (!dram_x || !dram_y) {
        fprintf(stderr, "Failed to allocate DRAM arrays\n");
        return 1;
    }
    for (size_t i = 0; i < n; ++i) {
        dram_x[i] = (double)i;
        dram_y[i] = 1.0;
    }

    daxpy(n, a, dram_x, dram_y);
    print_sample("DRAM", dram_y, n);
    free(dram_x);
    free(dram_y);
    
    /* NVM-backed arrays at fixed addresses */
    double *nvm_x = NVM_X_ADDR;
    double *nvm_y = NVM_Y_ADDR;
    for (size_t i = 0; i < n; ++i) {
        nvm_x[i] = 100.0 + (double)i;
        nvm_y[i] = 10.0;
    }

    daxpy(n, a, nvm_x, nvm_y);
    print_sample("NVM ", nvm_y, n);
    
    return 0;
    
}
