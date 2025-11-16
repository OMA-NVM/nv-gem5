#include <stdio.h>
#include <gem5/m5ops.h>

#define ROWS_A 2
#define COLS_A 3
#define ROWS_B COLS_A
#define COLS_B 2

static void multiply_matrices(const int a[ROWS_A][COLS_A],
                              const int b[ROWS_B][COLS_B],
                              int result[ROWS_A][COLS_B])
{
    for (int i = 0; i < ROWS_A; ++i) {
        for (int j = 0; j < COLS_B; ++j) {
            int sum = 0;
            for (int k = 0; k < COLS_A; ++k) {
                sum += a[i][k] * b[k][j];
            }
            result[i][j] = sum;
        }
    }
}

static void print_matrix(const int *matrix, int rows, int cols)
{
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            printf("%4d ", matrix[i * cols + j]);
        }
        printf("\n");
    }
}

int main(void)
{
    const int a[ROWS_A][COLS_A] = {
        {1, 2, 3},
        {4, 5, 6}
    };

    const int b[ROWS_B][COLS_B] = {
        {7, 8},
        {9, 10},
        {11, 12}
    };

    int c[ROWS_A][COLS_B] = {0};

    m5_reset_stats(0, 0);
    /* ** Region of Interest ** */
    multiply_matrices(a, b, c);
    m5_dump_stats(0, 0);

    printf("Matrix A (%dx%d):\n", ROWS_A, COLS_A);
    print_matrix(&a[0][0], ROWS_A, COLS_A);

    printf("\nMatrix B (%dx%d):\n", ROWS_B, COLS_B);
    print_matrix(&b[0][0], ROWS_B, COLS_B);

    printf("\nResult A x B (%dx%d):\n", ROWS_A, COLS_B);
    print_matrix(&c[0][0], ROWS_A, COLS_B);

    return 0;
}
