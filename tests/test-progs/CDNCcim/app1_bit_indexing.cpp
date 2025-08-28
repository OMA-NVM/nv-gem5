#include "cim_api.hpp"

#include <fcntl.h>

#include <cstdio>
#include <iostream>
#include <random>

#include "gem5/m5ops.h"

using namespace std;

#if !defined(NUM_WEEKS) || NUM_WEEKS <= 0
#define NUM_WEEKS (1)
#endif

#define NUM_USER_INT (DEFAULT_ROW_SIZE_BYTE >> 3)

#if !defined(inCIM) || inCIM == 0
#define CPU
#else
#undef CPU
#endif

uint64_t DayActivity[NUM_WEEKS][7][NUM_USER_INT] = { 0 };
uint64_t Genders[NUM_USER_INT] = { 0 };

uint64_t WeekActivity[NUM_WEEKS][NUM_USER_INT] = { 0 };

inline uint8_t
bit64_count(uint64_t int_str)
{
    uint8_t result = 0;
    for (uint32_t i = 0; i < 32; i++) {
        if (int_str & (1 << i)) {
            result++;
        }
    }
    return result;
}

#ifdef CPU
uint64_t Results[NUM_WEEKS] = { 0 };

void
cpu()
{
    printf("BIT-CPU\tweek numbers: %d\n", NUM_WEEKS);
#ifdef CHECKPOINT_FI
    m5_checkpoint(0, 0);
#endif // CHECKPOINT_FI
    for (uint32_t w = 0; w < NUM_WEEKS; w++) {
        for (uint32_t d = 0; d < 7; d++) {
            for (uint32_t i = 0; i < NUM_USER_INT; i++) {
                WeekActivity[w][i] |= DayActivity[w][d][i];
            }
        }
    }
    for (uint32_t w = 0; w < NUM_WEEKS; w++) {
        for (uint32_t i = 0; i < NUM_USER_INT; i++) {
            uint64_t tmp = WeekActivity[w][i] & Genders[i];
            Results[w] += bit64_count(tmp);
        }
    }
#ifdef CHECKPOINT_FI
    FILE *M5fp;
    M5fp = fopen("OUTPUTS/output_BIT_CPU.txt", "w");
    for (uint32_t w = 0; w < NUM_WEEKS; w++) {
        fprintf(M5fp, "%ld\n", Results[w]);
    }
    fflush(M5fp);
    fclose(M5fp);
    m5_switch_cpu();
    write_file("OUTPUTS/output_BIT_CPU.txt");
    m5_exit(0);
#elif defined(STD_PRINT_OUTPUT)
    for (uint32_t w = 0; w < NUM_WEEKS; w++) {
        printf("%ld\n", Results[w]);
    }
#endif // CHECKPOINT_FI
}
#else
uint64_t ResTemp[NUM_USER_INT] = { 0 };
uint64_t ResultsCIM[NUM_WEEKS] = { 0 };

void
cim()
{
    printf("BIT-CIM\tweek numbers: %d\n", NUM_WEEKS);
    CimModule cimModule;

    cimModule.copy_to_cim(9, (void *)Genders);

    for (uint32_t w = 0; w < NUM_WEEKS; w++) {
        // copy Array to row 0 ~ 6
        cimModule.copy_to_cim(0, (void *)&DayActivity[w][0][0]);
        cimModule.copy_to_cim(1, (void *)&DayActivity[w][1][0]);
        cimModule.copy_to_cim(2, (void *)&DayActivity[w][2][0]);
        cimModule.copy_to_cim(3, (void *)&DayActivity[w][3][0]);
        cimModule.copy_to_cim(4, (void *)&DayActivity[w][4][0]);
        cimModule.copy_to_cim(5, (void *)&DayActivity[w][5][0]);
        cimModule.copy_to_cim(6, (void *)&DayActivity[w][6][0]);
        cimModule.copy_to_cim(7, (void *)&DayActivity[w][6][0]);

        cimModule.OR({ 0, 1, 2, 3, 4, 5, 6, 7 });
        cimModule.COPY(8); // Write back results to row 8

        cimModule.AND({ 8, 9 });
        cimModule.COPY(10); // Write back results to row 10

        // Copy row 10 to CPU
        cimModule.copy_to_cpu((void *)ResTemp, 10);

        for (uint32_t i = 0; i < NUM_USER_INT; i++) {
            ResultsCIM[w] += bit64_count(ResTemp[i]);
        }
    }
#ifdef CHECKPOINT_FI
    FILE *M5fp;
    M5fp = fopen("OUTPUTS/output_BIT_CIM.txt", "w");
    for (uint32_t w = 0; w < NUM_WEEKS; w++) {
        fprintf(M5fp, "%ld\n", ResultsCIM[w]);
    }
    fflush(M5fp);
    fclose(M5fp);
    m5_exit(0);
#elif defined(STD_PRINT_OUTPUT)
    for (uint32_t w = 0; w < NUM_WEEKS; w++) {
        printf("%lld\n", ResultsCIM[w]);
    }
#endif // CHECKPOINT_FI
}
#endif // CPU

int
main(int argc, char *argv[])
{
    random_device seed_source;
    mt19937 gen(seed_source());
    uniform_int_distribution<uint64_t> distrib(0, 0xfffffffffffffffful);

    //====init
    for (uint32_t w = 0; w < NUM_WEEKS; w++) {
        for (uint32_t d = 0; d < 7; d++) {
            for (uint32_t i = 0; i < NUM_USER_INT; i++) {
                DayActivity[w][d][i] = distrib(gen);
            }
        }
    }
    for (uint32_t i = 0; i < NUM_USER_INT; i++) {
        Genders[i] = distrib(gen);
    }

#ifdef CPU
    cpu();
#else
    cim();
#endif
    return 0;
}
