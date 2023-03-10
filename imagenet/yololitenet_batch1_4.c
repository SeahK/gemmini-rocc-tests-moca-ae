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

//#include "yololitenet_params_1.h"
//#include "yololitenet50_mt_images.h"
#define NUM_OUTPUT (7+5+3)

#define num_proc 4
#define num_layer 12
#define num_pool 5

pthread_barrier_t barrier[num_proc];

#include "funct_googlenet_1.h"
#include "funct_yololitenet_1.h"

#define BATCH_DIVIDE 1
#define OROW_DIVIDE num_proc // 1: independent, 2: 2+2 collab, 4: sequential

#define target_util -1 // ToDo: needs to be changed for target utilization
#define bubble 0

#define YOLOLITENET_REPEAT 7


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
    uint64_t total_thread_cycles, total_cycles, total_conv_cycles, total_matmul_cycles, total_pool_cycles;
	uint64_t pool_cycles[num_pool];
	uint64_t conv_cycles[num_layer];
    uint64_t matmul_cycles[num_layer];
	uint64_t other_cycles; //global average
    //uint64_t target_cycles;
    int barrier_index;
};
// random matmul to warm up thread
void *thread_matmul0(void *arg){
        struct thread_args * matmul_args = (struct thread_args *) arg;
        gemmini_flush(0);
        int cid = sched_getcpu();//matmul_args->i;
	printf("entered thread_matmul function - cid: %d\n", cid);
          elem_t* A = (elem_t*) in_A + MAT_DIM_K*(MAT_DIM_I/2)*(cid/2);
          elem_t* B = (elem_t*) in_B + (MAT_DIM_J/2)*(cid%2);
          elem_t* C = (elem_t*) Out + (MAT_DIM_J/2)*(cid%2) + MAT_DIM_J*(MAT_DIM_I/2)*(cid/2);
	if(cid == 0 || cid == 1)
          tiled_matmul_auto(MAT_DIM_I/2, MAT_DIM_J/2, MAT_DIM_K,
                                A, B, NULL, C, //NO_BIAS ? NULL : D, C,
                           MAT_DIM_K, MAT_DIM_J, MAT_DIM_J, MAT_DIM_J,
            MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
            NO_ACTIVATION, ACC_SCALE_IDENTITY, 0, REPEATING_BIAS,
            false, false,
            false, !FULL_BIAS_WIDTH,
	    1,
            WS);
}


void *thread_NN(void *arg){
	int cid = sched_getcpu();
	struct thread_args * nn_args = (struct thread_args *) arg;
	gemmini_flush(0);
	uint64_t* cycles;

    uint64_t start, end;
    //printf("entered thread_NN - cid: %d\n", cid);
    //uint64_t target_cycle = nn_args->target_cycles;
    pthread_barrier_wait(&barrier[nn_args->barrier_index]);
    //printf("barrier working - cid: %d\n", cid);
    
    uint64_t thread_start = read_cycles();

    cycles = yololitenet_function_1(cid, 0, OROW_DIVIDE, BATCH_DIVIDE, target_util, &barrier[nn_args->barrier_index]);

    uint64_t thread_end = read_cycles();
    nn_args->total_thread_cycles = thread_end - thread_start;
    nn_args->total_conv_cycles = *(cycles+12);
    nn_args->total_pool_cycles = *(cycles+13);
    nn_args->total_cycles = *(cycles+14);
    for(int i = 0; i < NUM_OUTPUT; i++){
	if(i < 7) nn_args->conv_cycles[i] = *(cycles+i);
	else if(i < 12) nn_args->pool_cycles[i-7] = *(cycles+i);
    }

}
void *print_message(void *ptr){
    printf("entered message thread\n");
    gemmini_flush(0); // check whether all have gemmini cores
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

    int cpu_id;
    cpu_id = sched_getcpu();
    printf("main cpu: %d \n", cpu_id);
 
    cpu_set_t cpuset[num_proc];
    pthread_t thread[num_proc];
    pthread_attr_t attr[num_proc];
    for(int i = 0; i < num_proc; i++)
	pthread_attr_init(&attr[i]);
    struct thread_args nn_args[num_proc];

    printf("create threading \n");
    for(int i = 0; i < num_proc; i++){
	 CPU_ZERO(&cpuset[i]);
	 CPU_SET(i, &cpuset[i]);
	 pthread_attr_setaffinity_np(&attr[i], sizeof(cpu_set_t), &cpuset[i]);
	 pthread_create(&thread[i], &attr[i], print_message, NULL);
    }

    for(int i = 0; i < num_proc; i++){
        pthread_join(thread[i], NULL);
    }
    printf("thread joined after message printing\n");

    //just random turn
    for(int i = 0; i < num_proc; i++){
        pthread_create(&thread[i], &attr[i], thread_matmul0, &nn_args[i]);
    }

    for(int i = 0; i < num_proc; i++)
        pthread_join(thread[i], NULL);
    printf("thread joined after doing thread matmul\n");

   // for(int i = 0; i < OROW_DIVIDE; i++)
   //     nn_args[i].target_cycles = YOLOLITENET_TARGET;
    
    //pthread_barrier_init(&barrier, NULL, OROW_DIVIDE);
    pthread_barrier_init(&barrier[0], NULL, OROW_DIVIDE);
    //printf("thread barrier initialized \n");
    for(int r = 0; r < YOLOLITENET_REPEAT; r++){
	 uint64_t start = read_cycles();
	 for(int i = 0; i < OROW_DIVIDE; i++){
		  nn_args[i].barrier_index = 0;
		  pthread_create(&thread[i], &attr[i], thread_NN, &nn_args[i]);
	 }
	 for(int i = 0; i < OROW_DIVIDE; i++)
		  pthread_join(thread[i], NULL);
	 uint64_t end = read_cycles();
	
//	 printf("yololitenet repeat %d total cycles with threading overhead: %llu \n", r, end - start);

	 uint64_t thread_yololitenet_max = 0;
	 uint64_t total_yololitenet_max = 0;
	 for(int i = 0; i < OROW_DIVIDE; i++){
		  uint64_t matmul_cycles = nn_args[i].total_matmul_cycles;
		  uint64_t conv_cycles = nn_args[i].total_conv_cycles;
		  uint64_t pool_cycles = nn_args[i].total_pool_cycles;
		  //uint64_t other_cycles = nn_args[i].other_cycles;
		  uint64_t total_cycles =  nn_args[i].total_cycles; //conv_cycles + matmul_cycles + pool_cycles + other_cycles;
		  uint64_t thread_cycles = nn_args[i].total_thread_cycles;
		  
		  thread_yololitenet_max = thread_yololitenet_max > thread_cycles ? thread_yololitenet_max : thread_cycles;
		  total_yololitenet_max = total_yololitenet_max > total_cycles ? total_yololitenet_max : total_cycles;
	 }
	  printf("\nyololitenet repeat %d total thread cycles: %llu\n", r, thread_yololitenet_max);
	  printf("yololitenet repeat %d total cycles: %llu\n", r, total_yololitenet_max);
	

	 for(int i = 0; i < 7; i++)    

	 {
		  uint64_t max = 0;
		  for(int j = 0; j < OROW_DIVIDE; j++)
			  max = (max > nn_args[j].conv_cycles[i]) ? max : nn_args[j].conv_cycles[i];
		  
		  printf("yololitenet repeat %d Conv layer %d worst cycles: %llu \n", r, i, max);
		  max = 0;
	 }
	 

	 for(int i = 0; i < 0; i++)    

	 {
		  uint64_t max = 0;
		  for(int j = 0; j < OROW_DIVIDE; j++)
			  max = (max > nn_args[j].matmul_cycles[i]) ? max : nn_args[j].matmul_cycles[i];
		  
		  printf("yololitenet repeat %d Matmul layer %d worst cycles: %llu \n", r, i, max);
		  max = 0;
	 

	 }
	 

	 for(int i = 0; i < 5; i++)    

	 {
		  uint64_t max = 0;
		  for(int j = 0; j < OROW_DIVIDE; j++)
			  max = (max > nn_args[j].pool_cycles[i]) ? max : nn_args[j].pool_cycles[i];
		  
		  printf("yololitenet repeat %d Pool layer %d worst cycles: %llu \n", r, i, max);
		  max = 0;

		  

	 }
    }
    pthread_barrier_destroy(&barrier[0]);
 
    printf("==================================\n");
    exit(0);
}

