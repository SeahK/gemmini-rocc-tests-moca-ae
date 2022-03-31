//yolo_v2
#include "include/gemmini.h"
#include "include/gemmini_nn.h"

#include "yolonet_params_2.h"
#include "images.h"

#ifndef BAREMETAL
#define THREAD_SYNC 1
#else
#define THREAD_SYNC 0
#endif

#ifndef BAREMETAL
uint64_t* yolonet_function_2(int cid, int orow_divide, int batch_divide, int target_util, pthread_barrier_t  *barrier_yolo2){
#else
uint64_t* yolonet_function_2(int cid, int orow_divide, int batch_divide, int target_util){
#endif

#define num_cycle (19+5+3)

  static uint64_t cycles[num_proc][num_cycle];
 
    uint64_t start, end;
    uint64_t total_matmul_cycles = 0, total_conv_cycles = 0, total_pool_cycles = 0, conv_dw_cycles = 0, other_cycles = 0;
    uint64_t conv_cycles[19];
    uint64_t pool_cycles[5];

    //uint64_t target_cycle = target_cycles;
#if THREAD_SYNC == 1
    pthread_barrier_wait(barrier_yolo2);
#endif

    // conv_1
    start = read_cycles();
    tiled_conv_A_stride_auto_stride(
        conv_1_params_yolo2.batch_size, conv_1_params_yolo2.in_dim, conv_1_params_yolo2.in_channels,
        conv_1_params_yolo2.out_channels, conv_1_params_yolo2.out_dim,
        conv_1_params_yolo2.stride, 1, conv_1_params_yolo2.padding, conv_1_params_yolo2.kernel_size,
        conv_1_params_yolo2.out_stride, conv_1_params_yolo2.in_channels, 64,

        (elem_t*)images_2, (elem_t*)conv_1_w_yolo2, (acc_t*)conv_1_b_yolo2, (elem_t*)conv_1_out_yolo2,

        RELU, conv_1_params_yolo2.output_scale, 0,
        1, 1, 0, false,
	//conv_1_params_yolo2.pool_size, conv_1_params_yolo2.pool_stride, conv_1_params_yolo2.pool_padding, false,

        WS, orow_divide, batch_divide, cid, target_util);

    end = read_cycles();
    total_conv_cycles += end - start;
    conv_cycles[0] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(barrier_yolo2);
#endif        
 
    start = read_cycles();
    tiled_pool_auto_cid(
        conv_1_params_yolo2.batch_size,
        conv_1_params_yolo2.out_channels, conv_1_params_yolo2.out_dim, conv_1_params_yolo2.out_dim_pooled,
        conv_1_params_yolo2.out_stride,
        conv_1_params_yolo2.pool_size, conv_1_params_yolo2.pool_stride, conv_1_params_yolo2.pool_padding,

        (elem_t*)conv_1_out_yolo2, (elem_t*)conv_1_out_yolo2_pooled,
	orow_divide, batch_divide, cid, target_util);

    end = read_cycles();
    total_pool_cycles += end - start;
    pool_cycles[0] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(barrier_yolo2);
#endif        
        
    // conv_2
    start = read_cycles();
    tiled_conv_A_stride_auto_cid(
        conv_2_params_yolo2.batch_size, conv_2_params_yolo2.in_dim, conv_2_params_yolo2.in_channels,
        conv_2_params_yolo2.out_channels, conv_2_params_yolo2.out_dim,
        conv_2_params_yolo2.stride, 1, conv_2_params_yolo2.padding, conv_2_params_yolo2.kernel_size,
        conv_2_params_yolo2.out_stride,

        (elem_t*)conv_1_out_yolo2_pooled, (elem_t*)conv_2_w_yolo2, (acc_t*)conv_2_b_yolo2, (elem_t*)conv_2_out_yolo2,

        RELU, conv_2_params_yolo2.output_scale, 0,
        1, 1, 0, false,
	//conv_2_params_yolo2.pool_size, conv_2_params_yolo2.pool_stride, conv_2_params_yolo2.pool_padding, false,

        WS, orow_divide, batch_divide, cid, target_util);

    end = read_cycles();
    total_conv_cycles += end - start;
    conv_cycles[1] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(barrier_yolo2);
#endif        
  
    start = read_cycles();
    tiled_pool_auto_cid(
        conv_2_params_yolo2.batch_size,
        conv_2_params_yolo2.out_channels, conv_2_params_yolo2.out_dim, conv_2_params_yolo2.out_dim_pooled,
        conv_2_params_yolo2.out_stride,
        conv_2_params_yolo2.pool_size, conv_2_params_yolo2.pool_stride, conv_2_params_yolo2.pool_padding,

        (elem_t*)conv_2_out_yolo2, (elem_t*)conv_2_out_yolo2_pooled,
	orow_divide, batch_divide, cid, target_util);

    end = read_cycles();
    total_pool_cycles += end - start;
    pool_cycles[1] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(barrier_yolo2);
#endif              
    // conv_3
    start = read_cycles();
    tiled_conv_A_stride_auto_cid(
        conv_3_params_yolo2.batch_size, conv_3_params_yolo2.in_dim, conv_3_params_yolo2.in_channels,
        conv_3_params_yolo2.out_channels, conv_3_params_yolo2.out_dim,
        conv_3_params_yolo2.stride, 1, conv_3_params_yolo2.padding, conv_3_params_yolo2.kernel_size,
        conv_3_params_yolo2.out_stride,

        (elem_t*)conv_2_out_yolo2_pooled, (elem_t*)conv_3_w_yolo2, (acc_t*)conv_3_b_yolo2, (elem_t*)conv_3_out_yolo2,

        RELU, conv_3_params_yolo2.output_scale, 0,
        conv_3_params_yolo2.pool_size, 0, conv_3_params_yolo2.pool_padding, false,

        WS, orow_divide, batch_divide, cid, target_util);

    end = read_cycles();
    total_conv_cycles += end - start;
    conv_cycles[2] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(barrier_yolo2);
#endif        
        
    // conv_4
      start = read_cycles();
      tiled_matmul_nn_auto_cid(conv_4_params_yolo2.I, conv_4_params_yolo2.J, conv_4_params_yolo2.K, conv_4_params_yolo2.out_stride,
          (elem_t*)conv_3_out_yolo2, (elem_t*)conv_4_w_yolo2, (acc_t*)conv_4_b_yolo2, (elem_t*)conv_4_out_yolo2,
          NO_ACTIVATION, conv_4_params_yolo2.output_scale, 0, true,
          WS,
          orow_divide, batch_divide, cid, target_util);

    end = read_cycles();
    total_conv_cycles += end - start;
    conv_cycles[3] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(barrier_yolo2);
#endif        
        
    // conv_5
    start = read_cycles();
    tiled_conv_A_stride_auto_cid(
        conv_5_params_yolo2.batch_size, conv_5_params_yolo2.in_dim, conv_5_params_yolo2.in_channels,
        conv_5_params_yolo2.out_channels, conv_5_params_yolo2.out_dim,
        conv_5_params_yolo2.stride, 1, conv_5_params_yolo2.padding, conv_5_params_yolo2.kernel_size,
        conv_5_params_yolo2.out_stride,

        (elem_t*)conv_4_out_yolo2, (elem_t*)conv_5_w_yolo2, (acc_t*)conv_5_b_yolo2, (elem_t*)conv_5_out_yolo2,

        RELU, conv_5_params_yolo2.output_scale, 0,
        1, 1, 0, false,
	//conv_5_params_yolo2.pool_size, conv_5_params_yolo2.pool_stride, conv_5_params_yolo2.pool_padding, false,

        WS, orow_divide, batch_divide, cid, target_util);

    end = read_cycles();
    total_conv_cycles += end - start;
    conv_cycles[4] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(barrier_yolo2);
#endif        
     
    start = read_cycles();
    tiled_pool_auto_cid(
        conv_5_params_yolo2.batch_size,
        conv_5_params_yolo2.out_channels, conv_5_params_yolo2.out_dim, conv_5_params_yolo2.out_dim_pooled,
        conv_5_params_yolo2.out_stride,
        conv_5_params_yolo2.pool_size, conv_5_params_yolo2.pool_stride, conv_5_params_yolo2.pool_padding,

        (elem_t*)conv_5_out_yolo2, (elem_t*)conv_5_out_yolo2_pooled,
	orow_divide, batch_divide, cid, target_util);

    end = read_cycles();
    total_pool_cycles += end - start;
    pool_cycles[2] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(barrier_yolo2);
#endif
    
    // conv_6
    start = read_cycles();
    tiled_conv_A_stride_auto_cid(
        conv_6_params_yolo2.batch_size, conv_6_params_yolo2.in_dim, conv_6_params_yolo2.in_channels,
        conv_6_params_yolo2.out_channels, conv_6_params_yolo2.out_dim,
        conv_6_params_yolo2.stride, 1, conv_6_params_yolo2.padding, conv_6_params_yolo2.kernel_size,
        conv_6_params_yolo2.out_stride,

        (elem_t*)conv_5_out_yolo2_pooled, (elem_t*)conv_6_w_yolo2, (acc_t*)conv_6_b_yolo2, (elem_t*)conv_6_out_yolo2,

        RELU, conv_6_params_yolo2.output_scale, 0,
        conv_6_params_yolo2.pool_size, 0, conv_6_params_yolo2.pool_padding, false,

        WS, orow_divide, batch_divide, cid, target_util);

    end = read_cycles();
    total_conv_cycles += end - start;
    conv_cycles[5] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(barrier_yolo2);
#endif
        
    // conv_7
      start = read_cycles();
      tiled_matmul_nn_auto_cid(conv_7_params_yolo2.I, conv_7_params_yolo2.J, conv_7_params_yolo2.K, conv_7_params_yolo2.out_stride,
          (elem_t*)conv_6_out_yolo2, (elem_t*)conv_7_w_yolo2, (acc_t*)conv_7_b_yolo2, (elem_t*)conv_7_out_yolo2,
          NO_ACTIVATION, conv_7_params_yolo2.output_scale, 0, true,
          WS,
          orow_divide, batch_divide, cid, target_util);

    end = read_cycles();
    total_conv_cycles += end - start;
    conv_cycles[6] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(barrier_yolo2);
#endif
        
    // conv_8
    start = read_cycles();
    tiled_conv_A_stride_auto_cid(
        conv_8_params_yolo2.batch_size, conv_8_params_yolo2.in_dim, conv_8_params_yolo2.in_channels,
        conv_8_params_yolo2.out_channels, conv_8_params_yolo2.out_dim,
        conv_8_params_yolo2.stride, 1, conv_8_params_yolo2.padding, conv_8_params_yolo2.kernel_size,
        conv_8_params_yolo2.out_stride,

        (elem_t*)conv_7_out_yolo2, (elem_t*)conv_8_w_yolo2, (acc_t*)conv_8_b_yolo2, (elem_t*)conv_8_out_yolo2,

        RELU, conv_8_params_yolo2.output_scale, 0,
        1, 1, 0, false,
    //conv_8_params_yolo2.pool_size, conv_8_params_yolo2.pool_stride, conv_8_params_yolo2.pool_padding, false,

        WS, orow_divide, batch_divide, cid, target_util);

    end = read_cycles();
    total_conv_cycles += end - start;
    conv_cycles[7] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(barrier_yolo2);
#endif
     
    start = read_cycles();
    tiled_pool_auto_cid(
        conv_8_params_yolo2.batch_size,
        conv_8_params_yolo2.out_channels, conv_8_params_yolo2.out_dim, conv_8_params_yolo2.out_dim_pooled,
        conv_8_params_yolo2.out_stride,
        conv_8_params_yolo2.pool_size, conv_8_params_yolo2.pool_stride, conv_8_params_yolo2.pool_padding,

        (elem_t*)conv_8_out_yolo2, (elem_t*)conv_8_out_yolo2_pooled,
    orow_divide, batch_divide, cid, target_util);

    end = read_cycles();
    total_pool_cycles += end - start;
    pool_cycles[3] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(barrier_yolo2);
#endif
    
    // conv_9
    start = read_cycles();
    tiled_conv_A_stride_auto_cid(
        conv_9_params_yolo2.batch_size, conv_9_params_yolo2.in_dim, conv_9_params_yolo2.in_channels,
        conv_9_params_yolo2.out_channels, conv_9_params_yolo2.out_dim,
        conv_9_params_yolo2.stride, 1, conv_9_params_yolo2.padding, conv_9_params_yolo2.kernel_size,
        conv_9_params_yolo2.out_stride,

        (elem_t*)conv_8_out_yolo2_pooled, (elem_t*)conv_9_w_yolo2, (acc_t*)conv_9_b_yolo2, (elem_t*)conv_9_out_yolo2,

        RELU, conv_9_params_yolo2.output_scale, 0,
        conv_9_params_yolo2.pool_size, 0, conv_9_params_yolo2.pool_padding, false,

        WS, orow_divide, batch_divide, cid, target_util);

    end = read_cycles();
    total_conv_cycles += end - start;
    conv_cycles[8] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(barrier_yolo2);
#endif
        
    // conv_10
      start = read_cycles();
      tiled_matmul_nn_auto_cid(conv_10_params_yolo2.I, conv_10_params_yolo2.J, conv_10_params_yolo2.K, conv_10_params_yolo2.out_stride,
          (elem_t*)conv_9_out_yolo2, (elem_t*)conv_10_w_yolo2, (acc_t*)conv_10_b_yolo2, (elem_t*)conv_10_out_yolo2,
          NO_ACTIVATION, conv_10_params_yolo2.output_scale, 0, true,
          WS,
          orow_divide, batch_divide, cid, target_util);

    end = read_cycles();
    total_conv_cycles += end - start;
    conv_cycles[9] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(barrier_yolo2);
#endif
     
    
    // conv_11
    start = read_cycles();
    tiled_conv_A_stride_auto_cid(
        conv_11_params_yolo2.batch_size, conv_11_params_yolo2.in_dim, conv_11_params_yolo2.in_channels,
        conv_11_params_yolo2.out_channels, conv_11_params_yolo2.out_dim,
        conv_11_params_yolo2.stride, 1, conv_11_params_yolo2.padding, conv_11_params_yolo2.kernel_size,
        conv_11_params_yolo2.out_stride,

        (elem_t*)conv_10_out_yolo2, (elem_t*)conv_11_w_yolo2, (acc_t*)conv_11_b_yolo2, (elem_t*)conv_11_out_yolo2,

        RELU, conv_11_params_yolo2.output_scale, 0,
        conv_11_params_yolo2.pool_size, 0, conv_11_params_yolo2.pool_padding, false,

        WS, orow_divide, batch_divide, cid, target_util);

    end = read_cycles();
    total_conv_cycles += end - start;
    conv_cycles[10] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(barrier_yolo2);
#endif
        
    // conv_12
      start = read_cycles();
      tiled_matmul_nn_auto_cid(conv_12_params_yolo2.I, conv_12_params_yolo2.J, conv_12_params_yolo2.K, conv_12_params_yolo2.out_stride,
          (elem_t*)conv_11_out_yolo2, (elem_t*)conv_12_w_yolo2, (acc_t*)conv_12_b_yolo2, (elem_t*)conv_12_out_yolo2,
          NO_ACTIVATION, conv_12_params_yolo2.output_scale, 0, true,
          WS,
          orow_divide, batch_divide, cid, target_util);

    end = read_cycles();
    total_conv_cycles += end - start;
    conv_cycles[11] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(barrier_yolo2);
#endif
    

    // conv_13
    start = read_cycles();
    tiled_conv_A_stride_auto_cid(
        conv_13_params_yolo2.batch_size, conv_13_params_yolo2.in_dim, conv_13_params_yolo2.in_channels,
        conv_13_params_yolo2.out_channels, conv_13_params_yolo2.out_dim,
        conv_13_params_yolo2.stride, 1, conv_13_params_yolo2.padding, conv_13_params_yolo2.kernel_size,
        conv_13_params_yolo2.out_stride,

        (elem_t*)conv_12_out_yolo2, (elem_t*)conv_13_w_yolo2, (acc_t*)conv_13_b_yolo2, (elem_t*)conv_13_out_yolo2,

        RELU, conv_13_params_yolo2.output_scale, 0,
        1, 1, 0, false,
    //conv_13_params_yolo2.pool_size, conv_13_params_yolo2.pool_stride, conv_13_params_yolo2.pool_padding, false,

        WS, orow_divide, batch_divide, cid, target_util);

    end = read_cycles();
    total_conv_cycles += end - start;
    conv_cycles[12] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(barrier_yolo2);
#endif
     
    start = read_cycles();
    tiled_pool_auto_cid(
        conv_13_params_yolo2.batch_size,
        conv_13_params_yolo2.out_channels, conv_13_params_yolo2.out_dim, conv_13_params_yolo2.out_dim_pooled,
        conv_13_params_yolo2.out_stride,
        conv_13_params_yolo2.pool_size, conv_13_params_yolo2.pool_stride, conv_13_params_yolo2.pool_padding,

        (elem_t*)conv_13_out_yolo2, (elem_t*)conv_13_out_yolo2_pooled,
    orow_divide, batch_divide, cid, target_util);

    end = read_cycles();
    total_pool_cycles += end - start;
    pool_cycles[4] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(barrier_yolo2);
#endif

 
    // conv_14
    start = read_cycles();
    tiled_conv_A_stride_auto_cid(
        conv_14_params_yolo2.batch_size, conv_14_params_yolo2.in_dim, conv_14_params_yolo2.in_channels,
        conv_14_params_yolo2.out_channels, conv_14_params_yolo2.out_dim,
        conv_14_params_yolo2.stride, 1, conv_14_params_yolo2.padding, conv_14_params_yolo2.kernel_size,
        conv_14_params_yolo2.out_stride,

        (elem_t*)conv_13_out_yolo2_pooled, (elem_t*)conv_14_w_yolo2, (acc_t*)conv_14_b_yolo2, (elem_t*)conv_14_out_yolo2,

        RELU, conv_14_params_yolo2.output_scale, 0,
        conv_14_params_yolo2.pool_size, 0, conv_14_params_yolo2.pool_padding, false,

        WS, orow_divide, batch_divide, cid, target_util);

    end = read_cycles();
    total_conv_cycles += end - start;
    conv_cycles[13] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(barrier_yolo2);
#endif
        
    // conv_15
      start = read_cycles();
      tiled_matmul_nn_auto_cid(conv_15_params_yolo2.I, conv_15_params_yolo2.J, conv_15_params_yolo2.K, conv_15_params_yolo2.out_stride,
          (elem_t*)conv_14_out_yolo2, (elem_t*)conv_15_w_yolo2, (acc_t*)conv_15_b_yolo2, (elem_t*)conv_15_out_yolo2,
          NO_ACTIVATION, conv_15_params_yolo2.output_scale, 0, true,
          WS,
          orow_divide, batch_divide, cid, target_util);

    end = read_cycles();
    total_conv_cycles += end - start;
    conv_cycles[14] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(barrier_yolo2);
#endif
     
    
    // conv_16
    start = read_cycles();
    tiled_conv_A_stride_auto_cid(
        conv_16_params_yolo2.batch_size, conv_16_params_yolo2.in_dim, conv_16_params_yolo2.in_channels,
        conv_16_params_yolo2.out_channels, conv_16_params_yolo2.out_dim,
        conv_16_params_yolo2.stride, 1, conv_16_params_yolo2.padding, conv_16_params_yolo2.kernel_size,
        conv_16_params_yolo2.out_stride,

        (elem_t*)conv_15_out_yolo2, (elem_t*)conv_16_w_yolo2, (acc_t*)conv_16_b_yolo2, (elem_t*)conv_16_out_yolo2,

        RELU, conv_16_params_yolo2.output_scale, 0,
        conv_16_params_yolo2.pool_size, 0, conv_16_params_yolo2.pool_padding, false,

        WS, orow_divide, batch_divide, cid, target_util);

    end = read_cycles();
    total_conv_cycles += end - start;
    conv_cycles[15] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(barrier_yolo2);
#endif
        
    // conv_17
      start = read_cycles();
      tiled_matmul_nn_auto_cid(conv_17_params_yolo2.I, conv_17_params_yolo2.J, conv_17_params_yolo2.K, conv_17_params_yolo2.out_stride,
          (elem_t*)conv_16_out_yolo2, (elem_t*)conv_17_w_yolo2, (acc_t*)conv_17_b_yolo2, (elem_t*)conv_17_out_yolo2,
          NO_ACTIVATION, conv_17_params_yolo2.output_scale, 0, true,
          WS,
          orow_divide, batch_divide, cid, target_util);

    end = read_cycles();
    total_conv_cycles += end - start;
    conv_cycles[16] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(barrier_yolo2);
#endif
    

    // conv_18
    start = read_cycles();
    tiled_conv_A_stride_auto_cid(
        conv_18_params_yolo2.batch_size, conv_18_params_yolo2.in_dim, conv_18_params_yolo2.in_channels,
        conv_18_params_yolo2.out_channels, conv_18_params_yolo2.out_dim,
        conv_18_params_yolo2.stride, 1, conv_18_params_yolo2.padding, conv_18_params_yolo2.kernel_size,
        conv_18_params_yolo2.out_stride,

        (elem_t*)conv_17_out_yolo2, (elem_t*)conv_18_w_yolo2, (acc_t*)conv_18_b_yolo2, (elem_t*)conv_18_out_yolo2,

        RELU, conv_18_params_yolo2.output_scale, 0,
        1, 1, 0, false,
    //conv_18_params_yolo2.pool_size, conv_18_params_yolo2.pool_stride, conv_18_params_yolo2.pool_padding, false,

        WS, orow_divide, batch_divide, cid, target_util);

    end = read_cycles();
    total_conv_cycles += end - start;
    conv_cycles[17] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(barrier_yolo2);
#endif
   
   // conv_19
    start = read_cycles();
    tiled_matmul_nn_auto_cid(conv_19_params_yolo2.I, conv_19_params_yolo2.J, conv_19_params_yolo2.K, conv_19_params_yolo2.out_stride,
        (elem_t*)conv_18_out_yolo2, (elem_t*)conv_19_w_yolo2, (acc_t*)conv_19_b_yolo2, (elem_t*)conv_19_out_yolo2,
        RELU, conv_19_params_yolo2.output_scale, 0, true,
        WS,
        orow_divide, batch_divide, cid, target_util);

    end = read_cycles();
    total_conv_cycles += end - start;
    conv_cycles[18] = end - start;
#if THREAD_SYNC == 1
    pthread_barrier_wait(barrier_yolo2);
#endif


    
    for(int i = 0; i < num_cycle; i++){
      if(i < 19){
        cycles[cid][i] = conv_cycles[i];
      }
      else if (i < 24){
        cycles[cid][i] = pool_cycles[i - 19];
      }
      else{
        if(i == 24) cycles[cid][i] = total_conv_cycles;
        if(i == 25) cycles[cid][i] = total_pool_cycles;
        if(i == 26) cycles[cid][i] = total_conv_cycles + total_pool_cycles;
      }
    }
    return cycles[cid];
#undef num_cycle
}
