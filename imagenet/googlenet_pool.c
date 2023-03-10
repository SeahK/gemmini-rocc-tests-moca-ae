#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#ifndef BAREMETAL
#include <sys/mman.h>
#endif
#include "include/gemmini.h"
#include "include/gemmini_nn.h"

#include "googlenet_params.h"
#include "resnet50_mt_images.h"

#define num_proc 4
#define num_layer (36+22)
#define num_resadd 0
#define num_pool 9

#define THREAD_SYNC true // must do sync
#define BATCH_DIVIDE 1
#define OROW_DIVIDE 4 // 1: independent, 2: 2+2 collab, 4: sequential

#define A_no_max_block 0
#define B_no_max_block 0

#define priority false // ToDo: set it to true for priorized cores
#define target_util 0 // ToDo: needs to be changed for target utilization
#define bubble 0

#define GOOGLENET_REPEAT 10

pthread_barrier_t barrier;

#define MAT_DIM_I 512
#define MAT_DIM_J 512
#define MAT_DIM_K 512
#define FULL_BIAS_WIDTH true
#define REPEATING_BIAS true

//meaningless
static elem_t in_A[MAT_DIM_I][MAT_DIM_K] row_align(MAX_BLOCK_LEN) = {0};
static elem_t in_B[MAT_DIM_K][MAT_DIM_J] row_align(MAX_BLOCK_LEN) = {0};
static elem_t Out[MAT_DIM_I][MAT_DIM_J] row_align(MAX_BLOCK_LEN) = {0};

struct thread_args{
    uint64_t total_thread_cycles, total_pool_cycles, total_conv_cycles, total_matmul_cycles, total_resadd_cycles;
	uint64_t res_add_cycles[num_resadd];
	uint64_t conv_cycles[num_layer];
	uint64_t pool_cycles[num_pool];
    uint64_t matmul_cycles[num_layer];
	uint64_t other_cycles; //global average
};
// random matmul to warm up thread
void *thread_matmul0(void *arg){
        struct thread_args * matmul_args = (struct thread_args *) arg;
        gemmini_flush(0);
        int cid = sched_getcpu();//matmul_args->i;
          elem_t* A = (elem_t*) in_A + MAT_DIM_K*(MAT_DIM_I/2)*(cid/2);
          elem_t* B = (elem_t*) in_B + (MAT_DIM_J/2)*(cid%2);
          elem_t* C = (elem_t*) Out + (MAT_DIM_J/2)*(cid%2) + MAT_DIM_J*(MAT_DIM_I/2)*(cid/2);
          tiled_matmul_auto(MAT_DIM_I/2, MAT_DIM_J/2, MAT_DIM_K,
                                A, B, NULL, C, //NO_BIAS ? NULL : D, C,
                           MAT_DIM_K, MAT_DIM_J, MAT_DIM_J, MAT_DIM_J,
            MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
            NO_ACTIVATION, ACC_SCALE_IDENTITY, 0, REPEATING_BIAS,
            false, false,
            false, !FULL_BIAS_WIDTH,
            WS);
}


void *thread_NN(void *arg){
	int cid = sched_getcpu();
	struct thread_args * nn_args = (struct thread_args *) arg;
    enum tiled_matmul_type_t tiled_matmul_type = WS;
	gemmini_flush(0);
    uint64_t start, end;
    uint64_t im2col_cycles = 0, matmul_cycles = 0, conv_cycles = 0, pool_cycles = 0, conv_dw_cycles = 0, res_add_cycles = 0, other_cycles = 0;
    pthread_barrier_wait(&barrier);
    
    uint64_t thread_start = read_cycles();
    
    // conv_1
    start = read_cycles();
    tiled_conv_A_stride_auto_cid(
        conv_1_params.batch_size, conv_1_params.in_dim, conv_1_params.in_channels,
        conv_1_params.out_channels, conv_1_params.out_dim,
        conv_1_params.stride, 1, conv_1_params.padding, conv_1_params.kernel_size,
        conv_1_params.out_stride,

        (elem_t*)image0, (elem_t*)conv_1_w, (acc_t*)conv_1_b, (elem_t*)conv_1_out_pooled,

        RELU, conv_1_params.output_scale, 0,
        conv_1_params.pool_size, conv_1_params.pool_stride, conv_1_params.pool_padding, true,

        WS, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[0] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        
        
    // conv_2
    start = read_cycles();
    tiled_matmul_nn_auto_cid(conv_2_params.I, conv_2_params.J, conv_2_params.K, conv_2_params.out_stride,
        (elem_t*)conv_1_out_pooled, (elem_t*)conv_2_w, (acc_t*)conv_2_b, (elem_t*)conv_2_out,
        RELU, conv_2_params.output_scale, 0, true,
        tiled_matmul_type,
        OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    matmul_cycles += end - start;
    nn_args->matmul_cycles[0] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
        
    // conv_3
    start = read_cycles();
    tiled_conv_A_stride_auto_cid(
        conv_3_params.batch_size, conv_3_params.in_dim, conv_3_params.in_channels,
        conv_3_params.out_channels, conv_3_params.out_dim,
        conv_3_params.stride, 1, conv_3_params.padding, conv_3_params.kernel_size,
        conv_3_params.out_stride,

        (elem_t*)conv_2_out, (elem_t*)conv_3_w, (acc_t*)conv_3_b, (elem_t*)conv_3_out,

        RELU, conv_3_params.output_scale, 0,
        conv_3_params.pool_size, 0, conv_3_params.pool_padding, true,

        WS, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[1] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        
       
    // pool_9 for Inception 3a branch 4
    start = read_cycles();
    tiled_pool_auto_cid(pool_9_params.batch_size, pool_9_params.out_channels, pool_9_params.out_dim, pool_9_params.out_dim_pooled,
		pool_9_params.out_stride,
      pool_9_params.pool_size, pool_9_params.pool_stride, pool_9_params.pool_padding,	
      (elem_t*) conv_3_out_pooled, (elem_t*) pool_9_out,
		OROW_DIVIDE, BATCH_DIVIDE, cid);
    end = read_cycles();
    pool_cycles += end - start;
    nn_args->pool_cycles[0] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        

	 //Inception 3a
	 //Branch 1
    // conv_4
    start = read_cycles();
   tiled_matmul_nn_auto_cid(conv_4_params.I, conv_4_params.J, conv_4_params.K, 256 + 64,
        (elem_t*)conv_3_out_pooled, (elem_t*)conv_4_w, (acc_t*)conv_4_b, (elem_t*) inception3a_out,
        RELU, conv_4_params.output_scale, 0, true,
        tiled_matmul_type,
        OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[2] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif

	 // Branch 2
    // conv_5
    start = read_cycles();
    tiled_matmul_nn_auto_cid(conv_5_params.I, conv_5_params.J, conv_5_params.K, conv_5_params.out_stride,
        (elem_t*)conv_3_out_pooled, (elem_t*)conv_5_w, (acc_t*)conv_5_b, (elem_t*)conv_5_out,
        RELU, conv_5_params.output_scale, 0, true,
        tiled_matmul_type,
        OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    matmul_cycles += end - start;
    nn_args->matmul_cycles[1] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
               
    // conv_6
    start = read_cycles();
	  tiled_conv_A_stride_auto_cid(
			conv_6_params.batch_size, conv_6_params.in_dim, conv_6_params.in_channels,
			conv_6_params.out_channels, conv_6_params.out_dim,
			conv_6_params.stride, 1, conv_6_params.padding, conv_6_params.kernel_size,
			256 + 64,

			(elem_t*)conv_5_out, (elem_t*)conv_6_w, (acc_t*)conv_6_b, (elem_t*)((elem_t*)inception3a_out + 64), 

			RELU, conv_6_params.output_scale, 0,
			conv_5_params.pool_size, conv_5_params.pool_stride, conv_5_params.pool_padding, true,

			WS, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[3] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
    
	 // Branch 3
    // conv_7
    start = read_cycles();

	  tiled_matmul_nn_auto_cid(conv_7_params.I, conv_7_params.J, conv_7_params.K,
			conv_7_params.out_stride,
			(elem_t*) conv_3_out_pooled, (elem_t*) conv_7_w, (acc_t*) conv_7_b, (elem_t*) conv_7_out,
			RELU, conv_7_params.output_scale, 0, true,
			tiled_matmul_type, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    matmul_cycles += end - start;
    nn_args->matmul_cycles[2] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        
       
    // conv_8
    start = read_cycles();
	  tiled_conv_A_stride_auto_cid(
			conv_8_params.batch_size, conv_8_params.in_dim, conv_8_params.in_channels,
			conv_8_params.out_channels, conv_8_params.out_dim,
			conv_8_params.stride, 1, conv_8_params.padding, conv_8_params.kernel_size,
			256 + 64,

			(elem_t*)conv_7_out, (elem_t*)conv_8_w, (acc_t*)conv_8_b, (elem_t*)((elem_t*)inception3a_out + 192), 

			RELU, conv_8_params.output_scale, 0,
			conv_8_params.pool_size, conv_8_params.pool_stride, conv_8_params.pool_padding, true,

			WS, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[4] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
        
    // Branch 4
    // conv_10
    start = read_cycles();
    tiled_matmul_nn_auto_cid(conv_10_params.I, conv_10_params.J, conv_10_params.K, 256 + 64,
        (elem_t*)pool_9_out, (elem_t*)conv_10_w, (acc_t*)conv_10_b, (elem_t*)inception3a_out + 224,
        RELU, conv_10_params.output_scale, 0, true,
        tiled_matmul_type,
        OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[5] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        

// pool_16 for Inception 3a branch 4
    start = read_cycles();
    tiled_pool_auto_cid(pool_16_params.batch_size, pool_16_params.out_channels, pool_16_params.out_dim, pool_16_params.out_dim_pooled,
		pool_16_params.out_stride,
      pool_16_params.pool_size, pool_16_params.pool_stride, pool_16_params.pool_padding,	
      (elem_t*) inception3a_out, (elem_t*) pool_16_out,
		OROW_DIVIDE, BATCH_DIVIDE, cid);
    end = read_cycles();
    pool_cycles += end - start;
    nn_args->pool_cycles[1] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        


	 //Inception 3b
	 //Branch 1
    // conv_11
    start = read_cycles();
	  tiled_conv_A_stride_auto_cid(
			conv_11_params.batch_size, conv_11_params.in_dim, conv_11_params.in_channels,
			conv_11_params.out_channels, conv_11_params.out_dim,
			conv_11_params.stride, 1, conv_11_params.padding, conv_11_params.kernel_size,
			480,

			(elem_t*) inception3a_out, (elem_t*)conv_11_w, (acc_t*)conv_11_b, (elem_t*) pool_18_out,

			RELU, conv_11_params.output_scale, 0,
			pool_18_params.pool_size, pool_18_params.pool_stride, pool_18_params.pool_padding, true,

			WS, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[6] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif

	 // Branch 2
    // conv_12
    start = read_cycles();
    tiled_matmul_nn_auto_cid(conv_12_params.I, conv_12_params.J, conv_12_params.K, conv_12_params.out_stride,
        (elem_t*) inception3a_out, (elem_t*)conv_12_w, (acc_t*)conv_12_b, (elem_t*)conv_12_out,
        NO_ACTIVATION, conv_12_params.output_scale, 0, true,
        tiled_matmul_type,
        OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    matmul_cycles += end - start;
    nn_args->matmul_cycles[3] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
               
    // conv_13
    start = read_cycles();
	  tiled_conv_A_stride_auto_cid(
			conv_13_params.batch_size, conv_13_params.in_dim, conv_13_params.in_channels,
			conv_13_params.out_channels, conv_13_params.out_dim,
			conv_13_params.stride, 1, conv_13_params.padding, conv_13_params.kernel_size,
			480,

			(elem_t*)conv_12_out, (elem_t*)conv_13_w, (acc_t*)conv_13_b, (elem_t*)((elem_t*)(pool_18_out) + 128),

			RELU, conv_13_params.output_scale, 0,
			pool_18_params.pool_size, pool_18_params.pool_stride, pool_18_params.pool_padding, true,

			WS, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[7] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
    
	 // Branch 3
    // conv_14
    start = read_cycles();

	  tiled_matmul_nn_auto_cid(conv_14_params.I, conv_14_params.J, conv_14_params.K,
			conv_14_params.out_stride,
			(elem_t*) inception3a_out, (elem_t*) conv_14_w, (acc_t*) conv_14_b, (elem_t*) conv_14_out,
			RELU, conv_14_params.output_scale, 0, true,
			tiled_matmul_type, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    matmul_cycles += end - start;
    nn_args->matmul_cycles[4] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        
       
    // conv_15
    start = read_cycles();
	  tiled_conv_A_stride_auto_cid(
			conv_15_params.batch_size, conv_15_params.in_dim, conv_15_params.in_channels,
			conv_15_params.out_channels, conv_15_params.out_dim,
			conv_15_params.stride, 1, conv_15_params.padding, conv_15_params.kernel_size,
			480,

			(elem_t*)conv_14_out, (elem_t*)conv_15_w, (acc_t*)conv_15_b, (elem_t*)((elem_t*) pool_18_out + 320),

			RELU, conv_15_params.output_scale, 0,
			pool_18_params.pool_size, pool_18_params.pool_stride, pool_18_params.pool_padding, true,

			WS, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[8] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
        
    // Branch 4
    // conv_17
    start = read_cycles();
    tiled_conv_A_stride_auto_cid(
        conv_17_params.batch_size, conv_17_params.in_dim, conv_17_params.in_channels,
        conv_17_params.out_channels, conv_17_params.out_dim,
        conv_17_params.stride, 1, conv_17_params.padding, conv_17_params.kernel_size,
        480,

        (elem_t*)pool_16_out, (elem_t*)conv_17_w, (acc_t*)conv_17_b, (elem_t*)((elem_t*) pool_18_out + 416),

        RELU, conv_17_params.output_scale, 0,
  		  pool_18_params.pool_size, pool_18_params.pool_stride, pool_18_params.pool_padding, true,

        WS, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[9] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        

	 //pool for inception 4a branch 4 input
    start = read_cycles();
    tiled_pool_auto_cid(pool_24_params.batch_size, pool_24_params.out_channels, pool_24_params.out_dim, pool_24_params.out_dim_pooled,
		   pool_24_params.out_stride,
        pool_24_params.pool_size, pool_24_params.pool_stride, pool_24_params.pool_padding,
			(elem_t*) pool_18_out, (elem_t*) pool_24_out,
			OROW_DIVIDE, BATCH_DIVIDE, cid);
    end = read_cycles();
    pool_cycles += end - start;
    nn_args->pool_cycles[2] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        

	 //Inception 4a
	 //Branch 1
    // conv_19
    start = read_cycles();
    tiled_matmul_nn_auto_cid(conv_19_params.I, conv_19_params.J, conv_19_params.K, 512 + 64,
        (elem_t*)pool_18_out, (elem_t*)conv_19_w, (acc_t*)conv_19_b, (elem_t*)inception4a_out,
        RELU, conv_19_params.output_scale, 0, true,
        tiled_matmul_type,
        OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[10] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif

	 // Branch 2
    // conv_20
    start = read_cycles();
    tiled_matmul_nn_auto_cid(conv_20_params.I, conv_20_params.J, conv_20_params.K, conv_20_params.out_stride,
        (elem_t*)pool_18_out, (elem_t*)conv_20_w, (acc_t*)conv_20_b, (elem_t*)conv_20_out,
        RELU, conv_20_params.output_scale, 0, true,
        tiled_matmul_type,
        OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    matmul_cycles += end - start;
    nn_args->matmul_cycles[5] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
               
    // conv_21
    start = read_cycles();
	  tiled_conv_A_stride_auto_cid(
			conv_21_params.batch_size, conv_21_params.in_dim, conv_21_params.in_channels,
			conv_21_params.out_channels, conv_21_params.out_dim,
			conv_21_params.stride, 1, conv_21_params.padding, conv_21_params.kernel_size,
			512 + 64,

			(elem_t*)conv_20_out, (elem_t*)conv_21_w, (acc_t*)conv_21_b, (elem_t*)((elem_t*)inception4a_out + 192),

			RELU, conv_21_params.output_scale, 0,
			conv_21_params.pool_size, conv_21_params.pool_stride, conv_21_params.pool_padding, true,

			WS, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[11] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
    
	 // Branch 3
    // conv_22
    start = read_cycles();

	  tiled_matmul_nn_auto_cid(conv_22_params.I, conv_22_params.J, conv_22_params.K,
			conv_22_params.out_stride,
			(elem_t*) pool_18_out, (elem_t*) conv_22_w, (acc_t*) conv_22_b, (elem_t*) conv_22_out,
			RELU, conv_22_params.output_scale, 0, true,
			tiled_matmul_type, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    matmul_cycles += end - start;
    nn_args->matmul_cycles[6] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        
       
    // conv_23
    start = read_cycles();
	  tiled_conv_A_stride_auto_cid(
			conv_23_params.batch_size, conv_23_params.in_dim, conv_23_params.in_channels,
			conv_23_params.out_channels, conv_23_params.out_dim,
			conv_23_params.stride, 1, conv_23_params.padding, conv_23_params.kernel_size,
			512 + 64,

			(elem_t*)conv_22_out, (elem_t*)conv_23_w, (acc_t*)conv_23_b, (elem_t*)((elem_t*)inception4a_out + 400),

			RELU, conv_23_params.output_scale, 0,
			conv_23_params.pool_size, conv_23_params.pool_stride, conv_23_params.pool_padding, true,

			WS, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[12] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
        
    // Branch 4
    // conv_25
    start = read_cycles();
    tiled_matmul_nn_auto_cid(conv_25_params.I, conv_25_params.J, conv_25_params.K, 512 + 64,
        (elem_t*)pool_24_out, (elem_t*)conv_25_w, (acc_t*)conv_25_b, (elem_t*)inception4a_out + 448,
        RELU, conv_25_params.output_scale, 0, true,
        tiled_matmul_type,
        OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[13] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        

// pool_31 for Inception 3a branch 4
    start = read_cycles();
    tiled_pool_auto_cid(pool_31_params.batch_size, pool_31_params.out_channels, pool_31_params.out_dim, pool_31_params.out_dim_pooled,
		pool_31_params.out_stride,
      pool_31_params.pool_size, pool_31_params.pool_stride, pool_31_params.pool_padding,	
      (elem_t*) inception4a_out, (elem_t*) pool_31_out,
		OROW_DIVIDE, BATCH_DIVIDE, cid);
    end = read_cycles();
    pool_cycles += end - start;
    nn_args->pool_cycles[3] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        

	 //Inception 4b
	 // Branch 1
    // conv_26
    start = read_cycles();
    tiled_matmul_nn_auto_cid(conv_26_params.I, conv_26_params.J, conv_26_params.K, 512 + 64,
        (elem_t*)inception4a_out, (elem_t*)conv_26_w, (acc_t*)conv_26_b, (elem_t*)inception4b_out,
        RELU, conv_26_params.output_scale, 0, true,
        tiled_matmul_type,
        OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);


    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[14] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif

	 // Branch 2
    // conv_27
    start = read_cycles();
    tiled_matmul_nn_auto_cid(conv_27_params.I, conv_27_params.J, conv_27_params.K, conv_27_params.out_stride,
        (elem_t*)inception4a_out, (elem_t*)conv_27_w, (acc_t*)conv_27_b, (elem_t*)conv_27_out,
        NO_ACTIVATION, conv_27_params.output_scale, 0, true,
        tiled_matmul_type,
        OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    matmul_cycles += end - start;
    nn_args->matmul_cycles[7] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
               
    // conv_28
    start = read_cycles();
	  tiled_conv_A_stride_auto_cid(
			conv_28_params.batch_size, conv_28_params.in_dim, conv_28_params.in_channels,
			conv_28_params.out_channels, conv_28_params.out_dim,
			conv_28_params.stride, 1, conv_28_params.padding, conv_28_params.kernel_size,
			512 + 64,

			(elem_t*)conv_27_out, (elem_t*)conv_28_w, (acc_t*)conv_28_b, (elem_t*)((elem_t*)inception4b_out + 160),

			RELU, conv_28_params.output_scale, 0,
			conv_27_params.pool_size, conv_27_params.pool_stride, conv_27_params.pool_padding, true,

			WS, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[15] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
    
	 // Branch 3
    // conv_29
    start = read_cycles();

	  tiled_matmul_nn_auto_cid(conv_29_params.I, conv_29_params.J, conv_29_params.K,
			conv_29_params.out_stride,
			(elem_t*) inception4a_out, (elem_t*) conv_29_w, (acc_t*) conv_29_b, (elem_t*) conv_29_out,
			RELU, conv_29_params.output_scale, 0, true,
			tiled_matmul_type, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    matmul_cycles += end - start;
    nn_args->matmul_cycles[8] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        
       
    // conv_30
    start = read_cycles();
	  tiled_conv_A_stride_auto_cid(
			conv_30_params.batch_size, conv_30_params.in_dim, conv_30_params.in_channels,
			conv_30_params.out_channels, conv_30_params.out_dim,
			conv_30_params.stride, 1, conv_30_params.padding, conv_30_params.kernel_size,
			512 + 64,

			(elem_t*)conv_29_out, (elem_t*)conv_30_w, (acc_t*)conv_30_b, (elem_t*)((elem_t*)inception4b_out + 384),

			RELU, conv_30_params.output_scale, 0,
			conv_30_params.pool_size, conv_30_params.pool_stride, conv_30_params.pool_padding, true,

			WS, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);
//pthread_barrier_wait(&barrier);
    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[16] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
        
    // Branch 4
    // conv_32
    start = read_cycles();
    tiled_matmul_nn_auto_cid(conv_32_params.I, conv_32_params.J, conv_32_params.K, 512 + 64,
        (elem_t*)pool_31_out, (elem_t*)conv_32_w, (acc_t*)conv_32_b, (elem_t*)inception4b_out + 448,
        RELU, conv_32_params.output_scale, 0, true,
        tiled_matmul_type,
        OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);


    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[17] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        

// pool_38 for Inception 4b branch 4
    start = read_cycles();
    tiled_pool_auto_cid(pool_38_params.batch_size, pool_38_params.out_channels, pool_38_params.out_dim, pool_38_params.out_dim_pooled,
		pool_38_params.out_stride,
      pool_38_params.pool_size, pool_38_params.pool_stride, pool_38_params.pool_padding,	
      (elem_t*) inception4b_out, (elem_t*) pool_38_out,
		OROW_DIVIDE, BATCH_DIVIDE, cid);
    end = read_cycles();
    pool_cycles += end - start;
    nn_args->pool_cycles[4] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        

	 //Inception 4c
	 // Branch 1
    // conv_33
    start = read_cycles();
    tiled_matmul_nn_auto_cid(conv_33_params.I, conv_33_params.J, conv_33_params.K, 512 + 64,
        (elem_t*)inception4b_out, (elem_t*)conv_33_w, (acc_t*)conv_33_b, (elem_t*)inception4c_out,
        RELU, conv_33_params.output_scale, 0, true,
        tiled_matmul_type,
        OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);


    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[18] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif

	 // Branch 2
    // conv_34
    start = read_cycles();
    tiled_matmul_nn_auto_cid(conv_34_params.I, conv_34_params.J, conv_34_params.K, conv_34_params.out_stride,
        (elem_t*)inception4b_out, (elem_t*)conv_34_w, (acc_t*)conv_34_b, (elem_t*)conv_34_out,
        NO_ACTIVATION, conv_34_params.output_scale, 0, true,
        tiled_matmul_type,
        OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    matmul_cycles += end - start;
    nn_args->matmul_cycles[9] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
               
    // conv_35
    start = read_cycles();
	  tiled_conv_A_stride_auto_cid(
			conv_35_params.batch_size, conv_35_params.in_dim, conv_35_params.in_channels,
			conv_35_params.out_channels, conv_35_params.out_dim,
			conv_35_params.stride, 1, conv_35_params.padding, conv_35_params.kernel_size,
			512 + 64,

			(elem_t*)conv_34_out, (elem_t*)conv_35_w, (acc_t*)conv_35_b, (elem_t*)((elem_t*)inception4c_out + 128), 

			RELU, conv_35_params.output_scale, 0,
			conv_35_params.pool_size, conv_35_params.pool_stride, conv_35_params.pool_padding, true,

			WS, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[19] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
    
	 // Branch 3
    // conv_36
    start = read_cycles();

	  tiled_matmul_nn_auto_cid(conv_36_params.I, conv_36_params.J, conv_36_params.K,
			conv_36_params.out_stride,
			(elem_t*) inception4b_out, (elem_t*) conv_36_w, (acc_t*) conv_36_b, (elem_t*) conv_36_out,
			RELU, conv_36_params.output_scale, 0, true,
			tiled_matmul_type, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    matmul_cycles += end - start;
    nn_args->matmul_cycles[10] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        
       
    // conv_37
    start = read_cycles();
	  tiled_conv_A_stride_auto_cid(
			conv_37_params.batch_size, conv_37_params.in_dim, conv_37_params.in_channels,
			conv_37_params.out_channels, conv_37_params.out_dim,
			conv_37_params.stride, 1, conv_37_params.padding, conv_37_params.kernel_size,
			512 + 64,

			(elem_t*)conv_36_out, (elem_t*)conv_37_w, (acc_t*)conv_37_b, (elem_t*)((elem_t*)inception4c_out + 384),

			RELU, conv_37_params.output_scale, 0,
			conv_37_params.pool_size, conv_37_params.pool_stride, conv_37_params.pool_padding, true,

			WS, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);
 //pthread_barrier_wait(&barrier);

    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[20] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
        
    // Branch 4
    // conv_39
    start = read_cycles();
    tiled_matmul_nn_auto_cid(conv_39_params.I, conv_39_params.J, conv_39_params.K, 512 + 64,
        (elem_t*)pool_38_out, (elem_t*)conv_39_w, (acc_t*)conv_39_b, (elem_t*)inception4c_out + 448,
        NO_ACTIVATION, conv_39_params.output_scale, 0, true,
        tiled_matmul_type,
        OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);


    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[21] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        

// pool_45 for Inception 4c branch 4
    start = read_cycles();
    tiled_pool_auto_cid(pool_45_params.batch_size, pool_45_params.out_channels, pool_45_params.out_dim, pool_45_params.out_dim_pooled,
		pool_45_params.out_stride,
      pool_45_params.pool_size, pool_45_params.pool_stride, pool_45_params.pool_padding,	
      (elem_t*) inception4c_out, (elem_t*) pool_45_out,
		OROW_DIVIDE, BATCH_DIVIDE, cid);
    end = read_cycles();
    pool_cycles += end - start;
    nn_args->pool_cycles[5] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        


    //Inception 4d
	 // Branch 1
    // conv_40
     start = read_cycles();
    tiled_matmul_nn_auto_cid(conv_40_params.I, conv_40_params.J, conv_40_params.K, 528,
        (elem_t*)inception4c_out, (elem_t*)conv_40_w, (acc_t*)conv_40_b, (elem_t*) inception4d_out,
        NO_ACTIVATION, conv_40_params.output_scale, 0, true,
        tiled_matmul_type,
        OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[22] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif

	 // Branch 2
    // conv_41
    start = read_cycles();
    tiled_matmul_nn_auto_cid(conv_41_params.I, conv_41_params.J, conv_41_params.K, conv_41_params.out_stride,
        (elem_t*)inception4c_out, (elem_t*)conv_41_w, (acc_t*)conv_41_b, (elem_t*)conv_41_out,
        RELU, conv_41_params.output_scale, 0, true,
        tiled_matmul_type,
        OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    matmul_cycles += end - start;
    nn_args->matmul_cycles[11] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
               
    // conv_42
    start = read_cycles();
	  tiled_conv_A_stride_auto_cid(
			conv_42_params.batch_size, conv_42_params.in_dim, conv_42_params.in_channels,
			conv_42_params.out_channels, conv_42_params.out_dim,
			conv_42_params.stride, 1, conv_42_params.padding, conv_42_params.kernel_size,
			528,

			(elem_t*)conv_41_out, (elem_t*)conv_42_w, (acc_t*)conv_42_b, (elem_t*)((elem_t*)inception4d_out + 112),

			RELU, conv_42_params.output_scale, 0,
			conv_42_params.pool_size, conv_42_params.pool_stride, conv_42_params.pool_padding, true,

			WS, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[23] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
    
	 // Branch 3
    // conv_43
    start = read_cycles();

	  tiled_matmul_nn_auto_cid(conv_43_params.I, conv_43_params.J, conv_43_params.K,
			conv_43_params.out_stride,
			(elem_t*) inception4c_out, (elem_t*) conv_43_w, (acc_t*) conv_43_b, (elem_t*) conv_43_out,
			RELU, conv_43_params.output_scale, 0, true,
			tiled_matmul_type, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    matmul_cycles += end - start;
    nn_args->matmul_cycles[12] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        
       
    // conv_44
    start = read_cycles();
	  tiled_conv_A_stride_auto_cid(
			conv_44_params.batch_size, conv_44_params.in_dim, conv_44_params.in_channels,
			conv_44_params.out_channels, conv_44_params.out_dim,
			conv_44_params.stride, 1, conv_44_params.padding, conv_44_params.kernel_size,
			528,

			(elem_t*)conv_43_out, (elem_t*)conv_44_w, (acc_t*)conv_44_b, (elem_t*)((elem_t*)inception4d_out + 400),

			RELU, conv_44_params.output_scale, 0,
			conv_44_params.pool_size, conv_44_params.pool_stride, conv_44_params.pool_padding, true,

			WS, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);
    pthread_barrier_wait(&barrier);


    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[24] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
        
    // Branch 4
    // conv_46
    start = read_cycles();
    tiled_matmul_nn_auto_cid(conv_46_params.I, conv_46_params.J, conv_46_params.K,
			528,
			(elem_t*) pool_45_out, (elem_t*) conv_46_w, (acc_t*) conv_46_b, (elem_t*) inception4d_out + 464,
			RELU, conv_46_params.output_scale, 0, true,
			tiled_matmul_type, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);


    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[25] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        

// pool_52 for Inception 4d branch 4
    start = read_cycles();
    tiled_pool_auto_cid(pool_52_params.batch_size, pool_52_params.out_channels, pool_52_params.out_dim, pool_52_params.out_dim_pooled,
		pool_52_params.out_stride,
      pool_52_params.pool_size, pool_52_params.pool_stride, pool_52_params.pool_padding,	
      (elem_t*) inception4d_out, (elem_t*) pool_52_out,
		OROW_DIVIDE, BATCH_DIVIDE, cid);
    end = read_cycles();
    pool_cycles += end - start;
    nn_args->pool_cycles[6] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        


	 //Inception 4e
	 //Branch 1
    // conv_47
    start = read_cycles();
	  tiled_conv_A_stride_auto_cid(
			conv_47_params.batch_size, conv_47_params.in_dim, conv_47_params.in_channels,
			conv_47_params.out_channels, conv_47_params.out_dim,
			conv_47_params.stride, 1, conv_47_params.padding, conv_47_params.kernel_size,
			832,

			(elem_t*) inception4d_out, (elem_t*)conv_47_w, (acc_t*)conv_47_b, (elem_t*) pool_54_out,

			RELU, conv_47_params.output_scale, 0,
			pool_54_params.pool_size, pool_54_params.pool_stride, pool_54_params.pool_padding, true,

			WS, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[26] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif

	 // Branch 2
    // conv_48
    start = read_cycles();
    tiled_matmul_nn_auto_cid(conv_48_params.I, conv_48_params.J, conv_48_params.K, conv_48_params.out_stride,
        (elem_t*) inception4d_out, (elem_t*)conv_48_w, (acc_t*)conv_48_b, (elem_t*)conv_48_out,
        NO_ACTIVATION, conv_48_params.output_scale, 0, true,
        tiled_matmul_type,
        OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    matmul_cycles += end - start;
    nn_args->matmul_cycles[13] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
               
    // conv_13
    start = read_cycles();
	  tiled_conv_A_stride_auto_cid(
			conv_49_params.batch_size, conv_49_params.in_dim, conv_49_params.in_channels,
			conv_49_params.out_channels, conv_49_params.out_dim,
			conv_49_params.stride, 1, conv_49_params.padding, conv_49_params.kernel_size,
			832,

			(elem_t*)conv_48_out, (elem_t*)conv_49_w, (acc_t*)conv_49_b, (elem_t*)((elem_t*)(pool_54_out) + 256),

			RELU, conv_49_params.output_scale, 0,
			pool_54_params.pool_size, pool_54_params.pool_stride, pool_54_params.pool_padding, true,

			WS, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[27] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
    
	 // Branch 3
    // conv_50
    start = read_cycles();

	  tiled_matmul_nn_auto_cid(conv_50_params.I, conv_50_params.J, conv_50_params.K,
			conv_50_params.out_stride,
			(elem_t*) inception4d_out, (elem_t*) conv_50_w, (acc_t*) conv_50_b, (elem_t*) conv_50_out,
			RELU, conv_50_params.output_scale, 0, true,
			tiled_matmul_type, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    matmul_cycles += end - start;
    nn_args->matmul_cycles[14] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        
       
    // conv_51
    start = read_cycles();
	  tiled_conv_A_stride_auto_cid(
			conv_51_params.batch_size, conv_51_params.in_dim, conv_51_params.in_channels,
			conv_51_params.out_channels, conv_51_params.out_dim,
			conv_51_params.stride, 1, conv_51_params.padding, conv_51_params.kernel_size,
			832,

			(elem_t*)conv_50_out, (elem_t*)conv_51_w, (acc_t*)conv_51_b, (elem_t*)((elem_t*) pool_54_out + 576),

			RELU, conv_51_params.output_scale, 0,
			pool_54_params.pool_size, pool_54_params.pool_stride, pool_54_params.pool_padding, true,

			WS, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);
 //pthread_barrier_wait(&barrier);


    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[28] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
        
    // Branch 4
    // conv_53
    start = read_cycles();
    tiled_conv_A_stride_auto_cid(
        conv_53_params.batch_size, conv_53_params.in_dim, conv_53_params.in_channels,
        conv_53_params.out_channels, conv_53_params.out_dim,
        conv_53_params.stride, 1, conv_53_params.padding, conv_53_params.kernel_size,
        832,

        (elem_t*)pool_52_out, (elem_t*)conv_53_w, (acc_t*)conv_53_b, (elem_t*)((elem_t*) pool_54_out + 704),

        RELU, conv_53_params.output_scale, 0,
  		  pool_54_params.pool_size, pool_54_params.pool_stride, pool_54_params.pool_padding, true,

        WS, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[29] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        

	 //pool for inception 5a branch 4 input
    start = read_cycles();
    tiled_pool_auto_cid(pool_60_params.batch_size, pool_60_params.out_channels, pool_60_params.out_dim, pool_60_params.out_dim_pooled,
		   pool_60_params.out_stride,
        pool_60_params.pool_size, pool_60_params.pool_stride, pool_60_params.pool_padding,
			(elem_t*) pool_54_out, (elem_t*) pool_60_out,
			OROW_DIVIDE, BATCH_DIVIDE, cid);
    end = read_cycles();
    pool_cycles += end - start;
    nn_args->pool_cycles[7] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        

	 // Inception 5a
	 // Branch 1
    // conv_55
    start = read_cycles();
	  tiled_matmul_nn_auto_cid(conv_55_params.I, conv_55_params.J, conv_55_params.K,
			832,
			(elem_t*) pool_54_out, (elem_t*) conv_55_w, (acc_t*) conv_55_b, (elem_t*) inception5a_out,
			RELU, conv_55_params.output_scale, 0, true,
			tiled_matmul_type, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);


    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[30] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif

	 // Branch 2
    // conv_56
    start = read_cycles();
    tiled_matmul_nn_auto_cid(conv_56_params.I, conv_56_params.J, conv_56_params.K, conv_56_params.out_stride,
        (elem_t*)pool_54_out, (elem_t*)conv_56_w, (acc_t*)conv_56_b, (elem_t*)conv_56_out,
        NO_ACTIVATION, conv_56_params.output_scale, 0, true,
        tiled_matmul_type,
        OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    matmul_cycles += end - start;
    nn_args->matmul_cycles[15] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
               
    // conv_57
    start = read_cycles();
	  tiled_conv_A_stride_auto_cid(
			conv_57_params.batch_size, conv_57_params.in_dim, conv_57_params.in_channels,
			conv_57_params.out_channels, conv_57_params.out_dim,
			conv_57_params.stride, 1, conv_57_params.padding, conv_57_params.kernel_size,
			832,

			(elem_t*)conv_56_out, (elem_t*)conv_57_w, (acc_t*)conv_57_b, (elem_t*)((elem_t*)inception5a_out + 256),

			RELU, conv_57_params.output_scale, 0,
			conv_57_params.pool_size, conv_57_params.pool_stride, conv_57_params.pool_padding, true,

			WS, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[31] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
    
	 // Branch 3
    // conv_58
    start = read_cycles();

	  tiled_matmul_nn_auto_cid(conv_58_params.I, conv_58_params.J, conv_58_params.K,
			conv_58_params.out_stride,
			(elem_t*) pool_54_out, (elem_t*) conv_58_w, (acc_t*) conv_58_b, (elem_t*) conv_58_out,
			RELU, conv_58_params.output_scale, 0, true,
			tiled_matmul_type, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    matmul_cycles += end - start;
    nn_args->matmul_cycles[16] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        
       
    // conv_59
    start = read_cycles();
	  tiled_conv_A_stride_auto_cid(
			conv_59_params.batch_size, conv_59_params.in_dim, conv_59_params.in_channels,
			conv_59_params.out_channels, conv_59_params.out_dim,
			conv_59_params.stride, 1, conv_59_params.padding, conv_59_params.kernel_size,
			832,

			(elem_t*)conv_58_out, (elem_t*)conv_59_w, (acc_t*)conv_59_b, (elem_t*)((elem_t*)inception5a_out + 576),

			RELU, conv_59_params.output_scale, 0,
			conv_59_params.pool_size, conv_59_params.pool_stride, conv_59_params.pool_padding, true,

			WS, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);
   pthread_barrier_wait(&barrier);


    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[32] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
        
    // Branch 4
    // conv_61
    start = read_cycles();
    tiled_matmul_nn_auto_cid(conv_61_params.I, conv_61_params.J, conv_61_params.K, 832,
        (elem_t*) pool_60_out, (elem_t*)conv_61_w, (acc_t*)conv_61_b, (elem_t*) inception5a_out + 704,
        NO_ACTIVATION, conv_61_params.output_scale, 0, true,
        tiled_matmul_type,
        OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[33] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        

// pool_67 for Inception 5b branch 4
    start = read_cycles();
    tiled_pool_auto_cid(pool_67_params.batch_size, pool_67_params.out_channels, pool_67_params.out_dim, pool_67_params.out_dim_pooled,
		pool_67_params.out_stride,
      pool_67_params.pool_size, pool_67_params.pool_stride, pool_67_params.pool_padding,	
      (elem_t*) inception5b_out, (elem_t*) pool_67_out,
		OROW_DIVIDE, BATCH_DIVIDE, cid);
    end = read_cycles();
    pool_cycles += end - start;
    nn_args->pool_cycles[8] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        


	 // Inception 5b
	 //Branch 1
    // conv_62
    start = read_cycles();
    tiled_matmul_nn_auto_cid(conv_62_params.I, conv_62_params.J, conv_62_params.K, 1024 + 64,
        (elem_t*) inception5a_out, (elem_t*)conv_62_w, (acc_t*)conv_62_b, (elem_t*)conv_62_out,
        NO_ACTIVATION, conv_62_params.output_scale, 0, true,
        tiled_matmul_type,
        OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    matmul_cycles += end - start;
    nn_args->matmul_cycles[17] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
 
	 // Branch 2
    // conv_63
    start = read_cycles();
    tiled_matmul_nn_auto_cid(conv_63_params.I, conv_63_params.J, conv_63_params.K, conv_63_params.out_stride,
        (elem_t*) inception5a_out, (elem_t*)conv_63_w, (acc_t*)conv_63_b, (elem_t*)conv_63_out,
        NO_ACTIVATION, conv_63_params.output_scale, 0, true,
        tiled_matmul_type,
        OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    matmul_cycles += end - start;
    nn_args->matmul_cycles[18] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
               
    // conv_64
    start = read_cycles();
	  tiled_conv_A_stride_auto_cid(
			conv_64_params.batch_size, conv_64_params.in_dim, conv_64_params.in_channels,
			conv_64_params.out_channels, conv_64_params.out_dim,
			conv_64_params.stride, 1, conv_64_params.padding, conv_64_params.kernel_size,
			1024 + 64,

			(elem_t*)conv_63_out, (elem_t*)conv_64_w, (acc_t*)conv_64_b, (elem_t*)((elem_t*)(inception5b_out) + 384),

			RELU, conv_64_params.output_scale, 0,
			conv_64_params.pool_size, conv_64_params.pool_stride, conv_64_params.pool_padding, true,

			WS, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[34] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
    
	 // Branch 3
    // conv_65
    start = read_cycles();

	  tiled_matmul_nn_auto_cid(conv_65_params.I, conv_65_params.J, conv_65_params.K,
			conv_65_params.out_stride,
			(elem_t*) inception5a_out, (elem_t*) conv_65_w, (acc_t*) conv_65_b, (elem_t*) conv_65_out,
			RELU, conv_65_params.output_scale, 0, true,
			tiled_matmul_type, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    matmul_cycles += end - start;
    nn_args->matmul_cycles[19] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        
       
    // conv_66
    start = read_cycles();
	  tiled_conv_A_stride_auto_cid(
			conv_66_params.batch_size, conv_66_params.in_dim, conv_66_params.in_channels,
			conv_66_params.out_channels, conv_66_params.out_dim,
			conv_66_params.stride, 1, conv_66_params.padding, conv_66_params.kernel_size,
			1024 + 64,

			(elem_t*)conv_65_out, (elem_t*)conv_66_w, (acc_t*)conv_66_b, (elem_t*)((elem_t*) inception5b_out + 768),

			RELU, conv_66_params.output_scale, 0,
			conv_66_params.pool_size, conv_66_params.pool_stride, conv_66_params.pool_padding, true,

			WS, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);
   pthread_barrier_wait(&barrier);


    end = read_cycles();
    conv_cycles += end - start;
    nn_args->conv_cycles[35] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif
        
    // Branch 4
    // conv_68
    start = read_cycles();
    tiled_matmul_nn_auto_cid(conv_68_params.I, conv_68_params.J, conv_68_params.K,
			1024 + 64,
			(elem_t*) pool_67_out, (elem_t*) conv_68_w, (acc_t*) conv_68_b, (elem_t*) inception5b_out + 896,
			RELU, conv_68_params.output_scale, 0, true,
			tiled_matmul_type, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    matmul_cycles += end - start;
    nn_args->matmul_cycles[20] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif        
       
    // Global averaging
    
    static elem_t average[1024][1] row_align(MAX_BLOCK_LEN);

    start = read_cycles();
    if(cid == 0)
       tiled_global_average_auto((elem_t*) inception5b_out, (elem_t*) average, 1, 1024, 7, WS);


    end = read_cycles();
    nn_args->other_cycles = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif

    // fc_69
    start = read_cycles();
    tiled_matmul_nn_auto_cid(fc_69_params.I, fc_69_params.J, fc_69_params.K, fc_69_params.out_stride,
        (elem_t*)fc_69_w, (elem_t*)average, (acc_t*)fc_69_b, (elem_t*)fc_69_out,
        NO_ACTIVATION, fc_69_params.output_scale, 0, false,
        tiled_matmul_type, OROW_DIVIDE, BATCH_DIVIDE, cid, A_no_max_block, B_no_max_block, priority, target_util, bubble);

    end = read_cycles();
    matmul_cycles += end - start;
    nn_args->matmul_cycles[21] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(&barrier);
#endif   


    uint64_t thread_end = read_cycles();
    nn_args->total_thread_cycles = thread_end - thread_start;
    nn_args->total_matmul_cycles = matmul_cycles;
    nn_args->total_conv_cycles = conv_cycles;
	 nn_args->total_pool_cycles = pool_cycles;
    nn_args->other_cycles = other_cycles;
    nn_args->total_resadd_cycles = res_add_cycles;

}
void *print_message(void *ptr){
    int cpu_id = sched_getcpu();
    printf("print msg - cpu_id: %d \n", cpu_id);
}

int main (int argc, char * argv[]) {
#ifndef BAREMETAL
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
      perror("mlockall failed");
      exit(1);
    }
#endif

    gemmini_flush(0);

    enum tiled_matmul_type_t tiled_matmul_type = WS;

    bool conv = true;
    bool check = false;
    int cpu_id;
    cpu_id = sched_getcpu();
    cpu_set_t cpuset[num_proc];
    pthread_t thread[num_proc];
    pthread_attr_t attr[num_proc];
    for(int i = 0; i < num_proc; i++)
	pthread_attr_init(&attr[i]);
    struct thread_args nn_args[num_proc];


    for(int i = 0; i < num_proc; i++){
	 CPU_ZERO(&cpuset[i]);
	 CPU_SET(i, &cpuset[i]);
	 pthread_attr_setaffinity_np(&attr[i], sizeof(cpu_set_t), &cpuset[i]);
	 pthread_create(&thread[i], &attr[i], print_message, NULL);
    }

    for(int i = 0; i < num_proc; i++){
        pthread_join(thread[i], NULL);
    }

    //just random turn
    for(int i = 0; i < num_proc; i++){
        pthread_create(&thread[i], &attr[i], thread_matmul0, &nn_args[i]);
    }

    for(int i = 0; i < num_proc; i++)
        pthread_join(thread[i], NULL);

    pthread_barrier_init(&barrier, NULL, OROW_DIVIDE);
    
    for(int r = 0; r < GOOGLENET_REPEAT; r++){
        uint64_t start = read_cycles();
        for(int i = 0; i < OROW_DIVIDE; i++)
            pthread_create(&thread[i], &attr[i], thread_NN, &nn_args[i]);
        
        for(int i = 0; i < OROW_DIVIDE; i++)
            pthread_join(thread[i], NULL);
        uint64_t end = read_cycles();
        

        uint64_t thread_resnet_max = 0;
        uint64_t total_resnet_max = 0;
        for(int i = 0; i < OROW_DIVIDE; i++){
            uint64_t matmul_cycles = nn_args[i].total_matmul_cycles;
            uint64_t conv_cycles = nn_args[i].total_conv_cycles;
            uint64_t pool_cycles = nn_args[i].total_pool_cycles;
            uint64_t other_cycles = nn_args[i].other_cycles;
            uint64_t total_cycles =  conv_cycles + matmul_cycles + pool_cycles + other_cycles;
            uint64_t thread_cycles = nn_args[i].total_thread_cycles;
            
            thread_resnet_max = thread_resnet_max > thread_cycles ? thread_resnet_max : thread_cycles;
            total_resnet_max = total_resnet_max > total_cycles ? total_resnet_max : total_cycles;
        }
         printf("\ngooglenet repeat %d total thread cycles: %llu\n", r, thread_resnet_max);
         printf("googlenet repeat %d total cycles: %llu\n", r, total_resnet_max);

         printf("worst case for each layers \n");

        for(int i = 0; i < 36; i++)

        {
            uint64_t max = 0;
            for(int j = 0; j < OROW_DIVIDE; j++)
               max = (max > nn_args[j].conv_cycles[i]) ? max : nn_args[j].conv_cycles[i];
            
            printf("googlenet repeat %d Conv layer %d worst cycles: %llu \n", r, i, max);
            max = 0;
        }
        

        for(int i = 0; i < 22; i++)

        {
            uint64_t max = 0;
            for(int j = 0; j < OROW_DIVIDE; j++)
               max = (max > nn_args[j].matmul_cycles[i]) ? max : nn_args[j].matmul_cycles[i];
            
            printf("googlenet repeat %d Matmul layer %d worst cycles: %llu \n", r, i, max);
            max = 0;
        

        }
        

        for(int i = 0; i < 9; i++)

        {
            uint64_t max = 0;
            for(int j = 0; j < OROW_DIVIDE; j++)
               max = (max > nn_args[j].pool_cycles[i]) ? max : nn_args[j].pool_cycles[i];
            
            printf("googlenet repeat %d Pool layer %d worst cycles: %llu \n", r, i, max);
            max = 0;

            

        }
    }
    printf("==================================\n");
    
    
    pthread_barrier_destroy(&barrier);
    exit(0);
}

