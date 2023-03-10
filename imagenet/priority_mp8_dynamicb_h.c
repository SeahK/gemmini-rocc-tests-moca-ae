// split to 2 cores each, multi-program but first-come-first-served
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#ifndef BAREMETAL
#include <sys/mman.h>
#endif
//#include "resnet_params_1.h"
//#include "resnet50_mt_images.h"
//#define NUM_OUTPUT (20+34+16+3)

#define NUM_CORE 8
#define WORKLOAD_CORE 2
#define QUEUE_DEPTH 6
#define NUM_ITER 3
#define CAP 4 
#define CAP_SCALE 1.42
#define TARGET_SCALE 0.8
#define INTER_SCALE 1.15

#define WORKLOAD_A false
#define WORKLOAD_B true
// else: mixed (C)

#define BATCH1 true

#define debug_print 0

#define num_layer 54
#define num_resadd 16
#define num_proc NUM_CORE

#include "include/gemmini.h"
#include "include/gemmini_nn.h"
#include "include/workload.h"
//pthread_barrier_t barrier[NUM_SUB_GROUP]; // between two, total 4
//pthread_barrier_t barrier_sub[NUM_GROUP]; // between four, total 2
pthread_barrier_t barrier_global; // for all 8 cores

pthread_barrier_t barrier[NUM_SUB_GROUP]; // between two, total 4
pthread_barrier_t barrier_sub[NUM_GROUP]; // between four, total 2
pthread_barrier_t barrier_mid[NUM_SUB_GROUP]; // between two, total 4
pthread_barrier_t barrier_start[NUM_SUB_GROUP]; // between two, total 4
pthread_barrier_t barrier_sub_start[NUM_GROUP]; // between four, total 2
pthread_barrier_t barrier_finish2[NUM_SUB_GROUP]; // between two, total 4
pthread_barrier_t barrier_finish3[NUM_SUB_GROUP]; // between two, total 4
pthread_barrier_t barrier_finish[NUM_SUB_GROUP]; // between two, total 4
pthread_barrier_t barrier_sub_finish[NUM_GROUP]; // between four, total 2
pthread_barrier_t barrier_sub_mid2[NUM_GROUP]; // between four, total 2
pthread_barrier_t barrier_sub_mid3[NUM_GROUP]; // between four, total 2
pthread_barrier_t barrier_sub_mid[NUM_GROUP]; // between four, total 2


bool done[NUM_GROUP] = {0};
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
    uint64_t total_thread_cycles, total_cycles, total_conv_cycles, total_matmul_cycles, total_resadd_cycles;
//	uint64_t resadd_cycles[num_resadd];
//	uint64_t conv_cycles[num_layer];
//    uint64_t matmul_cycles[num_layer];
   int barrier_index;
   int workload_num_core; 
   int workload_id;
   int cid;
   int group_id;
   int queue_group;
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
  int real_cid = sched_getcpu();
  struct thread_args * nn_args = (struct thread_args *) arg;
  gemmini_flush(0);
  uint64_t* cycles;
  int workload_num_core = 2;//nn_args->workload_num_core;
  int cid = nn_args->cid; // 0 or 1
  int group_id = nn_args->group_id; // 4 + 4 (0 or 1)
  done[group_id] = false;
  pthread_barrier_wait(&barrier_global);
  int queue_group = nn_args->queue_group;
  int total_sub_group_id = nn_args->barrier_index; // overall subgroup id 2 + 2 + 2 + 2 (0 to 3)
  int sub_group_id = (total_sub_group_id % NUM_GROUP); // inside each group's sub id 0 or 1
  int group_cid = cid + sub_group_id * SUB_GROUP; // cid inside group
  printf("entered thread_NN - cid: %d, total_sub_group_id(barrier_index): %d, sub_group_id: %d, group_id: %d, real_cid: %d\n", cid, total_sub_group_id, sub_group_id, group_id, real_cid);
  uint64_t start, end;
  uint64_t temp_cycles = global_time; //gemmini_runtime[real_cid];
  pthread_barrier_wait(&barrier[total_sub_group_id]);
  bool all = false; // ToDo
  start = read_cycles();
  bool others_done = false;
  for(int g = 0; g < queue_group; g++){
     for(int i = 0; i < QUEUE_DEPTH; i++){
       pthread_barrier_wait(&barrier_start[total_sub_group_id]);
       for(int o = 0; o < NUM_SUB_GROUP; o++){
         if(o != total_sub_group_id)
           if(gemmini_done[o] && queue_group == NUM_ITER) others_done = true;
       }
       int queue_id = gemmini_workload_assigned[group_id][sub_group_id][g][i];
       int workload_id = total_queue_type[queue_id];
#if debug_print == 1 
printf("queue id: %d, workload id: %d, others done: %d\n", queue_id, workload_id, others_done);
#endif
       if(queue_id != -1){
         int status = total_queue_status[queue_id];
         if(!others_done || status > 0){
           int workload_id = total_queue_type[queue_id];
  // put score here
           if(status < workload_group[workload_id]){           	
	         uint64_t total_runtime = 0;
             int group_queue_id = gemmini_workload_grouped[group_id][sub_group_id][g][i];
#if debug_print == 1
             printf("rid: %d, workload id: %d, queue id: %d, group queue id: %d\n", real_cid, workload_id, queue_id, group_queue_id);
#endif	
             uint64_t inner_start = read_cycles();
             if(group_queue_id >= 0 && !all){
               total_runtime = workload_group_function(inner_start-temp_cycles, queue_id, group_queue_id, workload_id, total_queue_type[group_queue_id], cid, group_id, total_sub_group_id, workload_num_core, 0, &barrier[nn_args->barrier_index]);
               end = read_cycles();
               queue_id = (cid == 0) ? queue_id : group_queue_id;
               total_queue_runtime_total[group_cid][queue_id] = total_runtime;
             }
            else{

             total_runtime = workload_function(inner_start-temp_cycles, queue_id, workload_id, all ? group_cid : cid, group_id, total_sub_group_id, all ? SUB_CORE : workload_num_core, 0, all ? &barrier_sub[group_id] : &barrier[total_sub_group_id]);
             end = read_cycles();
             // runtime store
            }          
           
            uint64_t this_cycles = temp_cycles + end - inner_start;
            total_queue_finish[group_cid][queue_id] = (this_cycles > total_queue_dispatch[queue_id]) ? (this_cycles- total_queue_dispatch[queue_id]) : 1000;
          //total_queue_finish[group_cid][queue_id] = ((temp_cycles + inner_end - start) - total_queue_dispatch[queue_id]);

             total_queue_runtime_thread[group_cid][queue_id] = end - inner_start;
             temp_cycles += (end - inner_start);
             pthread_barrier_wait(&barrier_finish[total_sub_group_id]);
           }
         }
         else total_queue_status[queue_id] = -1; // release the queue 
       }
       else
          break;
     }
      
  }
  end = read_cycles();
  if(cid == 0) gemmini_done[total_sub_group_id] = true;
  pthread_barrier_wait(&barrier_finish2[total_sub_group_id]);
  //if(queue_group != 1)
  //   pthread_barrier_wait(&barrier[NUM_CORE]);
  //pthread_barrier_wait(&barrier[NUM_CORE]);
  gemmini_runtime[real_cid] += (end - start);
  //printf("idle cycle: %llu\n", gemmini_runtime[real_cid] - temp_cycles);
  
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
 
    cpu_set_t cpuset[NUM_CORE];
    pthread_t thread[NUM_CORE];
    pthread_attr_t attr[NUM_CORE];
    for(int i = 0; i < NUM_CORE; i++)
      pthread_attr_init(&attr[i]);
    struct thread_args nn_args[NUM_CORE];

    printf("create threading \n");
    for(int i = 0; i < NUM_CORE; i++){
      CPU_ZERO(&cpuset[i]);
      CPU_SET(i, &cpuset[i]);
      pthread_attr_setaffinity_np(&attr[i], sizeof(cpu_set_t), &cpuset[i]);
      pthread_create(&thread[i], &attr[i], print_message, NULL);
    }

    for(int i = 0; i < NUM_CORE; i++){
        pthread_join(thread[i], NULL);
    }
    printf("thread joined after message printing\n");

    //just random turn
    for(int i = 0; i < NUM_CORE; i++){
        pthread_create(&thread[i], &attr[i], thread_matmul0, &nn_args[i]);
    }

    for(int i = 0; i < NUM_CORE; i++)
        pthread_join(thread[i], NULL);
    printf("thread joined after doing thread matmul\n");

   // for(int i = 0; i < OROW_DIVIDE; i++)
   //     nn_args[i].target_cycles = RESNET_TARGET;
    
    pthread_barrier_init(&barrier_global, NULL, NUM_CORE);
    
    for(int i = 0; i < NUM_GROUP; i++){
      pthread_barrier_init(&barrier_sub_start[i], NULL, SUB_CORE); // between 4 cores
    }
    for(int i = 0; i < NUM_SUB_GROUP; i++){
      pthread_barrier_init(&barrier_mid[i], NULL, WORKLOAD_CORE);
    }
    for(int i = 0; i < NUM_SUB_GROUP; i++){
      pthread_barrier_init(&barrier_start[i], NULL, WORKLOAD_CORE);
    }
    for(int i = 0; i < NUM_GROUP; i++){
      pthread_barrier_init(&barrier_sub_mid2[i], NULL, SUB_CORE); // between 4 cores
    }
    for(int i = 0; i < NUM_GROUP; i++){
      pthread_barrier_init(&barrier_sub_mid3[i], NULL, SUB_CORE); // between 4 cores
    }
    for(int i = 0; i < NUM_GROUP; i++){
      pthread_barrier_init(&barrier_sub_mid[i], NULL, SUB_CORE); // between 4 cores
    }
    for(int i = 0; i < NUM_GROUP; i++){
      pthread_barrier_init(&barrier_sub_finish[i], NULL, SUB_CORE); // between 4 cores
    }
    for(int i = 0; i < NUM_SUB_GROUP; i++){
      pthread_barrier_init(&barrier_finish2[i], NULL, WORKLOAD_CORE);
    }
    for(int i = 0; i < NUM_SUB_GROUP; i++){
      pthread_barrier_init(&barrier_finish3[i], NULL, WORKLOAD_CORE);
    }
    for(int i = 0; i < NUM_SUB_GROUP; i++){
      pthread_barrier_init(&barrier_finish[i], NULL, WORKLOAD_CORE);
    }
    for(int i = 0; i < NUM_GROUP; i++){
      pthread_barrier_init(&barrier_sub[i], NULL, SUB_CORE); // between 4 cores
    }
    for(int i = 0; i < NUM_SUB_GROUP; i++){
      pthread_barrier_init(&barrier[i], NULL, WORKLOAD_CORE);
    }
    printf("starting workload creation \n");
    workload_mode_2(total_workloads, BATCH1, false, false, SEED, TARGET_SCALE, CAP_SCALE); 
    printf("workload creation finished \n");

    int queue_group = 1;
    while((queue_group != 0) || (total_queue_status[total_workloads-1] == -1)){
      global_time = gemmini_runtime[0];
      for(int i = 0; i < NUM_CORE; i++)
        if(global_time < gemmini_runtime[i]) 
          global_time = gemmini_runtime[i];
      queue_group = workload_priority_mp(total_workloads, NUM_ITER, global_time); // or instead use max cycle
      //workload_grouping(queue_group, NUM_GROUP);
      printf("finished workload queue assignment, number of queue group: %d, gemmini runtime: %d\n", queue_group, global_time);

      for(int k = 0; k < NUM_GROUP; k++)
        for(int x = 0; x < queue_group; x++){
           for(int y = 0; y < SUB_GROUP; y++){ 
              printf("group %d queue %d, sub-group %d: ", k, x, y);
              for(int j = 0; j < QUEUE_DEPTH; j++)
                 printf("%d(%d), ", gemmini_workload_assigned[k][y][x][j], total_queue_type[gemmini_workload_assigned[k][y][x][j]]);
              printf("\n");
           }
        }
      workload_grouping(queue_group);

      printf("grouped\n");
      for(int k = 0; k < NUM_GROUP; k++)
        for(int x = 0; x < queue_group; x++){
           for(int y = 0; y < SUB_GROUP; y++){  
              printf("group %d queue %d, sub-group %d: ", k, x, y);
              for(int j = 0; j < QUEUE_DEPTH; j++)
                 printf("%d(%d), ", gemmini_workload_grouped[k][y][x][j], total_queue_type[gemmini_workload_grouped[k][y][x][j]]);
              printf("\n");
           }
        }
      
      for(int j = 0; j < NUM_SUB_GROUP; j++)
        gemmini_done[j] = false;
      if(queue_group != 0){
        for(int i = 0; i < NUM_GROUP; i++){
          for(int j = 0; j < SUB_CORE; j++){
            int index = i * SUB_CORE + j;
            nn_args[index].barrier_index = (int)(index / WORKLOAD_CORE);
            nn_args[index].workload_num_core = WORKLOAD_CORE;
            nn_args[index].cid = j % WORKLOAD_CORE;
            nn_args[index].queue_group = queue_group;
            nn_args[index].group_id = i;
            pthread_create(&thread[index], &attr[index], thread_NN, &nn_args[index]);
          }
        }	
        for(int i = 0; i < NUM_CORE; i++)
          pthread_join(thread[i], NULL);
      }
      else{
        for(int i = 0; i < NUM_CORE; i++)
          gemmini_runtime[i] += 1000000;
      }	
    }

// check total_queue_finish, total_queue_runtime_thread, total_queue_runtime_total of each workload (total_queue_type)
// also check gemmini_runtime 

  for(int i = 0; i < total_workloads; i++){
    uint64_t max = 0;     
    for(int j = 0; j < SUB_CORE; j++){
      max = max > total_queue_finish[j][i] ? max : total_queue_finish[j][i]; 
    }
    if (max < tp_prediction_cycles[total_queue_type[i]-1]) max = tp_prediction_cycles[total_queue_type[i]-1];
	  printf("queue id %d workload type: %d\n", i, total_queue_type[i]);
	  printf("queue id %d dispatch to finish time: %llu\n", i, max);
    printf("queue id %d togo: %llu\n", i, total_queue_togo[i]); 
    printf("queue id %d conv: %d\n", i, total_queue_conv[i]);
    printf("queue id %d priority: %d\n", i, total_queue_priority[i]);
    printf("queue id %d dispatched time: %llu\n", i, total_queue_dispatch[i]);
    printf("queue id %d target: %llu\n", i, total_queue_target[i]);

    max = 0;
    for(int j = 0; j < SUB_CORE; j++){
      max = max > total_queue_runtime_thread[j][i] ? max : total_queue_runtime_thread[j][i]; 
    }
    printf("queue id %d thread runtime: %llu\n", i, max);

    max = 0;
    for(int j = 0; j < SUB_CORE; j++){
       max = max > total_queue_runtime_total[j][i] ? max : total_queue_runtime_total[j][i]; 
    }
    printf("queue id %d total runtime: %llu\n", i, max);
  }

  for(int i = 0; i < NUM_CORE; i++){
	  printf("gemmini core id %d runtime: %llu\n", i, gemmini_runtime[i]);
  }


  for(int i = 0; i < NUM_SUB_GROUP; i++)
    pthread_barrier_destroy(&barrier_finish2[i]);
  for(int i = 0; i < NUM_SUB_GROUP; i++)
    pthread_barrier_destroy(&barrier_finish3[i]);
  for(int i = 0; i < NUM_SUB_GROUP; i++)
    pthread_barrier_destroy(&barrier_finish[i]);
  for(int i = 0; i < NUM_GROUP; i++)
    pthread_barrier_destroy(&barrier_sub_mid2[i]);
  for(int i = 0; i < NUM_GROUP; i++)
    pthread_barrier_destroy(&barrier_sub_mid3[i]);
  for(int i = 0; i < NUM_GROUP; i++)
    pthread_barrier_destroy(&barrier_sub_mid[i]);
  for(int i = 0; i < NUM_GROUP; i++)
    pthread_barrier_destroy(&barrier_sub_finish[i]);
  for(int i = 0; i < NUM_SUB_GROUP; i++)
    pthread_barrier_destroy(&barrier_mid[i]);
  for(int i = 0; i < NUM_SUB_GROUP; i++)
    pthread_barrier_destroy(&barrier_start[i]);
  for(int i = 0; i < NUM_GROUP; i++)
    pthread_barrier_destroy(&barrier_sub_start[i]);
  for(int i = 0; i < NUM_SUB_GROUP; i++)
    pthread_barrier_destroy(&barrier[i]);
  for(int i = 0; i < NUM_GROUP; i++)
    pthread_barrier_destroy(&barrier_sub[i]);
  pthread_barrier_destroy(&barrier_global); 
  printf("==================================\n");
  exit(0);
}

