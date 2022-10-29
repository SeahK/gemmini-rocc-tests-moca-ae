// See LICENSE for license details.

#ifndef SRC_MAIN_C_GEMMINI_TESTUTILS_H
#define SRC_MAIN_C_GEMMINI_TESTUTILS_H

#undef abs

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <stdbool.h>


#include "include/gemmini_params.h"
#include "include/gemmini.h"

#ifdef BAREMETAL
#undef assert
#define assert(expr) \
    if (!(expr)) { \
      printf("Failed assertion: " #expr "\n  " __FILE__ ":%u\n", __LINE__); \
      exit(1); \
    }
#endif

// #define GEMMINI_ASSERTIONS

// NUM_CORES: number of integrated gemmini cores
// MAX_WORKLOAD: number of maximum workload that the queue can hold
// compiled_time[workload_type][NUM_CORES]: pre-compiled estimated time per core settings
// workload_queue[workload_id][workload_type, target, arrival_time, priority, states, total_id]: state of running or waiting workloads, with targets and arrival time
// states: state of run (-1: finished, 0: not started, 1: fully assigined and running, 2: need more allocation)
// workload_id: the place on the workload_queue become workload_id, -1 when starting (all need to be allocated)
// workload_type: pre-compiled workload type, order or compiled_time
// total_id: id out of total workloads, for savings for printing purpose
// running_gemmini[NUM_CORES(gemmini_states)]: state of gemmini
// gemmini_states: idle (-1), running (workload id)

/*
#define MAX_PRIORITY 11
#define PRIORI0 1 // Free: 0-1
#define PRIORI1 8 // Normal: 2-8
#define PRIORI2 9 // Production: 9
#define PRIORI3 11 // Monitoring: 10-11
// latency sensitiveness
#define QoS0 0 // lowest (infinite) -> 1 core
#define QoS1 4 // low (4 Tq) -> 1 core or 2 core
#define QoS2 2 // middle (2 Tq) -> 2 cores
#define QoS3 1 // high (1 Tq)
// to make things easy, allow 1 for QoS3, 2 for QoS2, 2 for QoS1, 

#define PRIORITY_SCALE 5
#define Tq 1 // unit T for QoS target (T_isolated)

// macros for workload queue
#define ENTRY_TYPE
#define ENTRY_TARGET
#define ENTRY_PRIORITY
#define ENTRY_ARRIVAL_TIME
#define ENTRY_START_TIME
#define ENTRY_STATE
#define ENTRY_NUM_CORE
#define ENTRY_EXPECT_TIME
#define ENTRY_BATCH
#define ENTRY_TOTAL_ID
// BATCH: less preempt, kill overhead & more flexibility

static uint64_t prerun_time[MAX_WORKLOAD] = {};
static uint64_t compiled_time[MAX_WORKLOAD][NUM_CORES] = {}; // 6 workloads, 4 different core settings
static uint64_t workload_queue[MAX_WORKLOAD][10] = {};
static int gemmini_queue[MAX_WORKLOAD][2] = {-1}; // store workloads to execute, and number of cores needed for each
static int running_gemmini[NUM_CORES] = {-1}; // gemmini state 
static uint64_t total_workload[TOTAL_WORKLOAD] = {0};

static int start_queue = 0;// start pointer for gemmini_queue
static int end_queue = 0;// end poiinter for gemmini_queue
*/
//////////////////////////////////////////////////////////

// Matmul utility functions
static void matmul(elem_t A[DIM][DIM], elem_t B[DIM][DIM], elem_t D[DIM][DIM], full_t C_full[DIM][DIM]) {
  for (size_t r = 0; r < DIM; r++)
    for (size_t c = 0; c < DIM; c++) {
      C_full[r][c] = D[r][c];
      for (size_t k = 0; k < DIM; k++)
        C_full[r][c] += A[r][k]*B[k][c];
    }
}

static void matmul_short(elem_t A[DIM][DIM], elem_t B[DIM][DIM], elem_t D[DIM][DIM], elem_t C[DIM][DIM]) {
  for (size_t r = 0; r < DIM; r++)
    for (size_t c = 0; c < DIM; c++) {
      C[r][c] = D[r][c];
      for (size_t k = 0; k < DIM; k++)
        C[r][c] += A[r][k]*B[k][c];
    }
}

static void matmul_full(elem_t A[DIM][DIM], elem_t B[DIM][DIM], full_t D[DIM][DIM], full_t C_full[DIM][DIM]) {
  // Identical to the other matmul function, but with a 64-bit bias
  for (size_t r = 0; r < DIM; r++)
    for (size_t c = 0; c < DIM; c++) {
      C_full[r][c] = D[r][c];
      for (size_t k = 0; k < DIM; k++)
        C_full[r][c] += A[r][k]*B[k][c];
    }
}

static void matmul_A_transposed(elem_t A[DIM][DIM], elem_t B[DIM][DIM], elem_t D[DIM][DIM], full_t C_full[DIM][DIM]) {
  for (size_t r = 0; r < DIM; r++)
    for (size_t c = 0; c < DIM; c++) {
      C_full[r][c] = D[r][c];
      for (size_t k = 0; k < DIM; k++)
        C_full[r][c] += A[k][r]*B[k][c];
    }
}

static void matmul_short_A_transposed(elem_t A[DIM][DIM], elem_t B[DIM][DIM], elem_t D[DIM][DIM], elem_t C[DIM][DIM]) {
  for (size_t r = 0; r < DIM; r++)
    for (size_t c = 0; c < DIM; c++) {
      C[r][c] = D[r][c];
      for (size_t k = 0; k < DIM; k++)
        C[r][c] += A[k][r]*B[k][c];
    }
}

static void matmul_full_A_transposed(elem_t A[DIM][DIM], elem_t B[DIM][DIM], full_t D[DIM][DIM], full_t C_full[DIM][DIM]) {
  for (size_t r = 0; r < DIM; r++)
    for (size_t c = 0; c < DIM; c++) {
      C_full[r][c] = D[r][c];
      for (size_t k = 0; k < DIM; k++)
        C_full[r][c] += A[k][r]*B[k][c];
    }
}

static void matmul_B_transposed(elem_t A[DIM][DIM], elem_t B[DIM][DIM], elem_t D[DIM][DIM], full_t C_full[DIM][DIM]) {
  for (size_t r = 0; r < DIM; r++)
    for (size_t c = 0; c < DIM; c++) {
      C_full[r][c] = D[r][c];
      for (size_t k = 0; k < DIM; k++)
        C_full[r][c] += A[r][k]*B[c][k];
    }
}

static void matmul_short_B_transposed(elem_t A[DIM][DIM], elem_t B[DIM][DIM], elem_t D[DIM][DIM], elem_t C[DIM][DIM]) {
  for (size_t r = 0; r < DIM; r++)
    for (size_t c = 0; c < DIM; c++) {
      C[r][c] = D[r][c];
      for (size_t k = 0; k < DIM; k++)
        C[r][c] += A[r][k]*B[c][k];
    }
}

static void matmul_full_B_transposed(elem_t A[DIM][DIM], elem_t B[DIM][DIM], full_t D[DIM][DIM], full_t C_full[DIM][DIM]) {
  for (size_t r = 0; r < DIM; r++)
    for (size_t c = 0; c < DIM; c++) {
      C_full[r][c] = D[r][c];
      for (size_t k = 0; k < DIM; k++)
        C_full[r][c] += A[r][k]*B[c][k];
    }
}

static void matmul_AB_transposed(elem_t A[DIM][DIM], elem_t B[DIM][DIM], elem_t D[DIM][DIM], full_t C_full[DIM][DIM]) {
  for (size_t r = 0; r < DIM; r++)
    for (size_t c = 0; c < DIM; c++) {
      C_full[r][c] = D[r][c];
      for (size_t k = 0; k < DIM; k++)
        C_full[r][c] += A[k][r]*B[c][k];
    }
}

static void matmul_short_AB_transposed(elem_t A[DIM][DIM], elem_t B[DIM][DIM], elem_t D[DIM][DIM], elem_t C[DIM][DIM]) {
  for (size_t r = 0; r < DIM; r++)
    for (size_t c = 0; c < DIM; c++) {
      C[r][c] = D[r][c];
      for (size_t k = 0; k < DIM; k++)
        C[r][c] += A[k][r]*B[c][k];
    }
}

static void matmul_full_AB_transposed(elem_t A[DIM][DIM], elem_t B[DIM][DIM], full_t D[DIM][DIM], full_t C_full[DIM][DIM]) {
  for (size_t r = 0; r < DIM; r++)
    for (size_t c = 0; c < DIM; c++) {
      C_full[r][c] = D[r][c];
      for (size_t k = 0; k < DIM; k++)
        C_full[r][c] += A[k][r]*B[c][k];
    }
}

static void matadd(full_t sum[DIM][DIM], full_t m1[DIM][DIM], full_t m2[DIM][DIM]) {
  for (size_t r = 0; r < DIM; r++)
    for (size_t c = 0; c < DIM; c++)
      sum[r][c] = m1[r][c] + m2[r][c];
}

// THIS IS A ROUNDING SHIFT! It also performs a saturating cast
static void matshift(full_t full[DIM][DIM], elem_t out[DIM][DIM], int shift) {
  for (size_t r = 0; r < DIM; r++)
    for (size_t c = 0; c < DIM; c++) {
      // Bitshift and round element
      full_t shifted = ROUNDING_RIGHT_SHIFT(full[r][c], shift);

      // Saturate and cast element
#ifndef ELEM_T_IS_FLOAT
      full_t elem = shifted > elem_t_max ? elem_t_max : (shifted < elem_t_min ? elem_t_min : shifted);
      out[r][c] = elem;
#else
      out[r][c] = shifted; // TODO should we also saturate when using floats?
#endif
    }
}

static void matscale(full_t full[DIM][DIM], elem_t out[DIM][DIM], acc_scale_t scale) {
  for (size_t r = 0; r < DIM; r++)
    for (size_t c = 0; c < DIM; c++) {
      // Bitshift and round element
      full_t scaled = ACC_SCALE(full[r][c], scale);

      // Saturate and cast element
#ifndef ELEM_T_IS_FLOAT
      full_t elem = scaled > elem_t_max ? elem_t_max : (scaled < elem_t_min ? elem_t_min : scaled);
      out[r][c] = elem;
#else
      out[r][c] = scaled; // TODO should we also saturate when using floats?
#endif
    }
}

static void matrelu(elem_t in[DIM][DIM], elem_t out[DIM][DIM]) {
  for (size_t r = 0; r < DIM; r++)
    for (size_t c = 0; c < DIM; c++)
      out[r][c] = in[r][c] > 0 ? in[r][c] : 0;
}

static void matrelu6(elem_t in[DIM][DIM], elem_t out[DIM][DIM], int scale) {
  int max = 6 * scale;

  for (size_t r = 0; r < DIM; r++)
    for (size_t c = 0; c < DIM; c++) {
      elem_t positive = in[r][c] > 0 ? in[r][c] : 0;
      out[r][c] = positive > max ? max : positive;
    }
}

static void transpose(elem_t in[DIM][DIM], elem_t out[DIM][DIM]) {
  for (size_t r = 0; r < DIM; r++)
    for (size_t c = 0; c < DIM; c++)
      out[c][r] = in[r][c];
}

int rand() {
  static uint32_t x = 777;
  x = x * 1664525 + 1013904223;
  return x >> 24;
}


#ifdef ELEM_T_IS_FLOAT
double rand_double() {
    double a = (double)(rand() % 128) / (double)(1 + (rand() % 64));
    double b = (double)(rand() % 128) / (double)(1 + (rand() % 64));
    return a - b;
}
#endif

static void printMatrix(elem_t m[DIM][DIM]) {
  for (size_t i = 0; i < DIM; ++i) {
    for (size_t j = 0; j < DIM; ++j)
#ifndef ELEM_T_IS_FLOAT
      printf("%d ", m[i][j]);
#else
      printf("%x ", elem_t_to_elem_t_bits(m[i][j]));
#endif
    printf("\n");
  }
}

static void printMatrixAcc(acc_t m[DIM][DIM]) {
  for (size_t i = 0; i < DIM; ++i) {
    for (size_t j = 0; j < DIM; ++j)
#ifndef ELEM_T_IS_FLOAT
      printf("%d ", m[i][j]);
#else
      printf("%x ", acc_t_to_acc_t_bits(m[i][j]));
#endif
    printf("\n");
  }
}

static int is_equal(elem_t x[DIM][DIM], elem_t y[DIM][DIM]) {
  for (size_t i = 0; i < DIM; ++i)
    for (size_t j = 0; j < DIM; ++j) {
#ifndef ELEM_T_IS_FLOAT
      if (x[i][j] != y[i][j])
#else
      bool isnanx = elem_t_isnan(x[i][j]);
      bool isnany = elem_t_isnan(y[i][j]);

      if (x[i][j] != y[i][j] && !(isnanx && isnany))
#endif
          return 0;
    }
  return 1;
}

static int is_equal_transposed(elem_t x[DIM][DIM], elem_t y[DIM][DIM]) {
  for (size_t i = 0; i < DIM; ++i)
    for (size_t j = 0; j < DIM; ++j) {
#ifndef ELEM_T_IS_FLOAT
      if (x[i][j] != y[j][i])
#else
      bool isnanx = elem_t_isnan(x[i][j]);
      bool isnany = elem_t_isnan(y[j][i]);

      if (x[i][j] != y[j][i] && !(isnanx && isnany))
#endif
          return 0;
    }
  return 1;
}

// This is a GNU extension known as statment expressions
#define MAT_IS_EQUAL(dim_i, dim_j, x, y) \
    ({int result = 1; \
      for (size_t i = 0; i < dim_i; i++) \
        for (size_t j = 0; j < dim_j; ++j) { \
          if (x[i][j] != y[i][j]) { \
            result = 0; \
            break; \
          } \
        } \
      result;})


#undef abs

#ifndef NUM_CORE
static uint64_t read_cycles() {
    uint64_t cycles;
    asm volatile ("rdcycle %0" : "=r" (cycles));
    return cycles;

    // const uint32_t * mtime = (uint32_t *)(33554432 + 0xbff8);
    // const uint32_t * mtime = (uint32_t *)(33554432 + 0xbffc);
    // return *mtime;
}
#endif
#endif  // SRC_MAIN_C_GEMMINI_TESTUTILS_H
