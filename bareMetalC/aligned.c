// See LICENSE for license details.

#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "include/systolic.h"

#define PG_SIZE (4*1024)
#define OFFSET 1

/*struct aligned_buffer {
  char garbage[0];
  elem_t data[DIM][DIM];
} __attribute__((__packed__));*/

struct offset_buffer {
  char garbage[OFFSET];
  elem_t data[DIM][DIM];
} __attribute__((__packed__));

int main() {
  matmul_flush(0);

  // static struct aligned_buffer In __attribute__((aligned(PG_SIZE)));
  static struct offset_buffer In __attribute__((aligned(PG_SIZE)));
  static struct offset_buffer Out __attribute__((aligned(PG_SIZE)));

  for (size_t i = 0; i < OFFSET; ++i)
      In.garbage[i] = ~0;

  for (size_t i = 0; i < DIM; ++i)
    for (size_t j = 0; j < DIM; ++j)
      In.data[i][j] = i*DIM + j;

  // printf("Mvin\n");
  matmul_mvin(In.data, 0, 1, 0, 0, 0);
  // printf("Mvout\n");
  matmul_mvout(Out.data, 0, 0, 1, 0, 0);

  // printf("Fence\n");
  matmul_fence();

  if (!is_equal(In.data, Out.data)) {
    printf("Matrix:\n");
    printMatrix(In.data);
    printf("Matrix output:\n");
    printMatrix(Out.data);
    printf("\n");

    exit(1);
  }

  exit(0);
}

