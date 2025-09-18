#include "cim_api.hpp"

#include <fcntl.h>
#include <cstdio>
#include <iostream>
#include <random>

// #include "gem5/m5ops.h"

using namespace std;

#if !defined(NUM_WEEKS) || NUM_WEEKS <= 0
#define NUM_WEEKS (1)
#endif

#define NUM_USER_INT (DEFAULT_ROW_SIZE_BYTE >> 3)
// #define NUM_USER_INT 16

#if !defined(inCIM) || inCIM == 0
#define CPU
#else
#undef CPU
#endif

// === Data ===
uint64_t DayActivity[NUM_WEEKS][7][NUM_USER_INT] = {0};
uint64_t WeekActivity[NUM_WEEKS][NUM_USER_INT]   = {0};

static inline uint64_t row_checksum(const uint64_t* p, size_t n)
{
    uint64_t acc = 0;
    for (size_t i = 0; i < n; ++i) acc ^= p[i];
    return acc;
}

#if defined(STD_PRINT_OUTPUT)
static inline void print_row_hex(const char* label, const uint64_t* p, size_t n)
{
    printf("%s", label);
    for (size_t i = 0; i < n; ++i)
        printf("%016llx ", (unsigned long long)p[i]);
    printf("\n");
}
#endif

#ifdef CPU
void cpu()
{
    printf("BIT-CPU (OR-only)\tweek numbers: %d\n", NUM_WEEKS);

    // 只做：WeekActivity[w][i] = OR_{d=0..6} DayActivity[w][d][i]
    for (uint32_t w = 0; w < NUM_WEEKS; w++) {

#if defined(STD_PRINT_OUTPUT)
        // 印出輸入（Day0..Day6）
        printf("=== CPU week %u inputs ===\n", w);
        for (uint32_t d = 0; d < 7; d++) {
            char lab[32];
            snprintf(lab, sizeof(lab), "Day %u: ", d);
            print_row_hex(lab, &DayActivity[w][d][0], NUM_USER_INT);
        }
#endif

        for (uint32_t d = 0; d < 7; d++) {
            for (uint32_t i = 0; i < NUM_USER_INT; i++) {
                WeekActivity[w][i] |= DayActivity[w][d][i];
            }
        }

#if defined(STD_PRINT_OUTPUT)
        // 印出輸出（CPU 計算的 OR 結果）
        print_row_hex("CPU OR result: ", &WeekActivity[w][0], NUM_USER_INT);
        uint64_t csum = row_checksum(&WeekActivity[w][0], NUM_USER_INT);
        printf("CPU W%u checksum=0x%016llx\n", w, (unsigned long long)csum);
#endif
    }
}
#else
uint64_t ResTemp[NUM_USER_INT] = {0};

void cim()
{
    printf("BIT-CIM (OR-only)\tweek numbers: %d\n", NUM_WEEKS);
    CimModule cimModule;

    for (uint32_t w = 0; w < NUM_WEEKS; w++) {

#if defined(STD_PRINT_OUTPUT)
        printf("=== CIM week %u inputs ===\n", w);
        for (uint32_t d = 0; d < 2; d++) {
            char lab[32];
            snprintf(lab, sizeof(lab), "Day %u: ", d);
            print_row_hex(lab, &DayActivity[w][d][0], NUM_USER_INT);
        }
#endif
        cimModule.copy_to_cim(0, (void*)&DayActivity[w][0][0]);
        cimModule.copy_to_cim(1, (void*)&DayActivity[w][1][0]);

        const uint8_t  dest_row = 2;                
        const uint8_t  bytemask = 0xFF;
        const uint32_t bankmask = 0xFFFFFFFFu;
        const uint64_t colmask  = 0xFFFFFFFFFFFFFFFFull;

        // 1. Write the OR result to buffer's row = dest_row
        cimModule.OR({0, 1}, bytemask, bankmask, colmask, dest_row);

        // 2. Copy the result from buffer's dest_row to data's dest_row
        //   Here src need to add 0x100, which means the source is from buffer
        cimModule.COPY(dest_row, (0x100u | dest_row));

        // 3. Write back the result to CPU
        cimModule.copy_to_cpu((void*)ResTemp, dest_row);

        for (uint32_t i = 0; i < NUM_USER_INT; i++) {
            WeekActivity[w][i] = ResTemp[i];
        }

        print_row_hex("Row0 input: ", &DayActivity[w][0][0], NUM_USER_INT);
        print_row_hex("Row1 input: ", &DayActivity[w][1][0], NUM_USER_INT);
        print_row_hex("ResTemp: ", ResTemp, NUM_USER_INT);

#if defined(STD_PRINT_OUTPUT)
    uint64_t OrResult[NUM_USER_INT] = {0};

    cimModule.copy_to_cpu((void*)OrResult, 1);

    size_t total = NUM_USER_INT;
    size_t same  = 0;
    for (size_t i = 0; i < NUM_USER_INT; ++i) {
        if (OrResult[i] == DayActivity[w][1][i]) {
            same++;
        }
    }

    printf("Row 1 copy compare @ week %u: total=%zu, equal=%zu, diff=%zu\n",
           w, total, same, total - same);
#endif
    }
}
#endif // CPU

int main(int argc, char* argv[])
{
    random_device seed_source;
    mt19937_64 gen(seed_source());
    uniform_int_distribution<uint64_t> distrib(0, 0xffffffffffffffffull);

    for (uint32_t w = 0; w < NUM_WEEKS; w++) {
        for (uint32_t d = 0; d < 7; d++) {
            for (uint32_t i = 0; i < NUM_USER_INT; i++) {
                DayActivity[w][d][i] = distrib(gen);
            }
        }
    }

#ifdef CPU
    printf("Running in CPU mode (OR-only)\n");
    cpu();
#else
    printf("Running in CIM mode (OR-only)\n");
    cim();
#endif
    return 0;
}