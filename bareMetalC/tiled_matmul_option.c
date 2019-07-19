// See LICENSE for license details.

#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#ifndef BAREMETAL
#include <sys/mman.h>
#endif
#include "include/systolic.h"

#define MAT_DIM_I 256
#define MAT_DIM_K 256
#define MAT_DIM_J 256

void full_matmul(elem_t A[MAT_DIM_I][MAT_DIM_K], elem_t B[MAT_DIM_K][MAT_DIM_J], acc_t D[MAT_DIM_I][MAT_DIM_J], int64_t C_full[MAT_DIM_I][MAT_DIM_J]) {
  for (size_t r = 0; r < MAT_DIM_I; r++)
    for (size_t c = 0; c < MAT_DIM_J; c++) {
      C_full[r][c] = D[r][c];
      for (size_t k = 0; k < MAT_DIM_K; k++)
        C_full[r][c] += A[r][k]*B[k][c];
    }
}

void full_printMatrix(elem_t m[MAT_DIM_I][MAT_DIM_J]) {
  for (size_t i = 0; i < MAT_DIM_I; ++i) {
    for (size_t j = 0; j < MAT_DIM_J; ++j)
      printf("%d ", m[i][j]);
    printf("\n");
  }
}

int full_is_equal(elem_t x[MAT_DIM_I][MAT_DIM_J], elem_t y[MAT_DIM_I][MAT_DIM_J]) {
  for (size_t i = 0; i < MAT_DIM_I; ++i)
    for (size_t j = 0; j < MAT_DIM_J; ++j)
      if (x[i][j] != y[i][j])
        return 0;
  return 1;
}

void full_matshift(int64_t full[MAT_DIM_I][MAT_DIM_J], elem_t out[MAT_DIM_I][MAT_DIM_J], int shift) {
  int divisor = 1 << shift;

  for (size_t r = 0; r < MAT_DIM_I; r++)                             
    for (size_t c = 0; c < MAT_DIM_J; c++) {
      // Bitshift and round element
      int64_t abs = full[r][c] > 0 ? full[r][c] : -full[r][c];
      int64_t shifted = (abs + (divisor/2)) / divisor;
      if (full[r][c] < 0)
        shifted = -shifted;

      // Saturate and cast element
      int64_t elem = shifted > elem_t_max ? elem_t_max : (shifted < elem_t_min ? elem_t_min : shifted);
      out[r][c] = elem;
    }
}

void full_matrelu(elem_t in[MAT_DIM_I][MAT_DIM_J], elem_t out[MAT_DIM_I][MAT_DIM_J]) {
  for (size_t r = 0; r < MAT_DIM_I; r++)
    for (size_t c = 0; c < MAT_DIM_J; c++)
      out[r][c] = in[r][c] > 0 ? in[r][c] : 0;
}

void full_matrelu6(elem_t in[MAT_DIM_I][MAT_DIM_J], elem_t out[MAT_DIM_I][MAT_DIM_J], int scale) {
  int max = 6 * scale;

  for (size_t r = 0; r < MAT_DIM_I; r++)
    for (size_t c = 0; c < MAT_DIM_J; c++) {
      elem_t positive = in[r][c] > 0 ? in[r][c] : 0;
      out[r][c] = positive > max ? max : positive;
    }
}

int main() {
  #ifndef BAREMETAL
  if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
    perror("mlockall failed");
    exit(1);
  }
  #endif

  matmul_flush(0);

  for (enum tiled_matmul_type_t option = OS; option <= CPU; option++) {
    for (int activation = 0; activation <= 2; ++activation) {
      for (int shift = 0; shift <= 12; shift += 6) {
        for (int relu6_shift = 0; shift <= 6; shift += 3) {
          for (int no_bias = 0; no_bias <= 1; no_bias += 1) {
            static elem_t full_A[MAT_DIM_I][MAT_DIM_K] row_align(1);
            static elem_t full_B[MAT_DIM_K][MAT_DIM_J] row_align(1);
            static elem_t full_C[MAT_DIM_I][MAT_DIM_J] row_align(1);
            static acc_t full_D[MAT_DIM_I][MAT_DIM_J] row_align_acc(1);

            static int64_t gold_full[MAT_DIM_I][MAT_DIM_J];
            static elem_t gold[MAT_DIM_I][MAT_DIM_J];

            // printf("Init A\n");
            for (size_t i = 0; i < MAT_DIM_I; ++i) {
              for (size_t j = 0; j < MAT_DIM_K; ++j) {
                full_A[i][j] = (rand() % 3) - 1;
              }
            }

            // printf("Init B\n");
            for (size_t i = 0; i < MAT_DIM_K; ++i) {
              for (size_t j = 0; j < MAT_DIM_J; ++j) {
                full_B[i][j] = (rand() % 3) - 1;
              }
            }

            // printf("Init D\n");
            for (size_t i = 0; i < MAT_DIM_I; ++i) {
              for (size_t j = 0; j < MAT_DIM_J; ++j) {
                full_D[i][j] = no_bias ? 0 : ((rand() % 3) - 1);
              }
            }

            printf("Starting CPU matmul\n");
            full_matmul(full_A, full_B, full_D, gold_full);
            full_matshift(gold_full, gold, 0);

            if (activation == RELU) {
              full_matrelu(gold, gold);
            } else if (activation == RELU6) {
              full_matrelu6(gold, gold, 1 << relu6_shift);
            }

            printf("Starting systolic matmul\n");
            tiled_matmul_option(MAT_DIM_I, MAT_DIM_J, MAT_DIM_K,
                    full_A, full_B, full_D, full_C,
                    no_bias, activation, shift, relu6_shift,
                    option);

            if (!full_is_equal(full_C, gold)) {
              printf("C:\n");
              full_printMatrix(full_C);
              printf("Gold:\n");
              full_printMatrix(gold);
              printf("\n");

              exit(1);
            }
          }
        }
      }
    }
  }

  exit(0);
}
