/*******************************************************************************
 * Copyright (c) 2018
 * The National University of Singapore, Xtra Group
 *
 * Author: Zeke Wang (wangzeke638 AT gmail.com)
 * 
 * Description: this file is used to show the impact of the prefetcher and TLB. 
 * And it is also used to demonestrate the impact of branches.
 * See file LICENSE.md for details.
 *******************************************************************************/
#include <cstdio>
#include <cstdlib>
#include <time.h>
#include <stdio.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <immintrin.h>

#include <iostream>
#include <fstream>
#include <string>
#include <vector>


//#include "rdtsc.h"

#include <sched.h>              /* CPU_ZERO, CPU_SET */

#include   "types_simd.h"

#include "cpu_mapping.h"
#include "common_tool.h"
#include "column_compare.h"
#include "memory_tool.h"
#include "rand_tool.h"
#include "perf_counters.h"
//#include "4_column_compare_with_literal.h" 

//#define OR_FIRST_EN
//#include 	"byteslice_column_block.h"
//
#define INTEL_PCM_ENABLE


	
#ifdef __INTEL_COMPILER
typedef long si64;
#else
typedef long long si64;
#endif


struct Monitor_Event inst_Monitor_Event = {
	{
		{0,0},
		{0,0},
		{0,0},
		{0,0},
	},
	0,
	{
		"core_0",
		"core_1",
		"core_2",
		"core_3",
	},
	{
		{0,0},
		{0,0},
		{0,0},
		{0,0},		
	},
	2,
	{
		"MIC_0",
		"MIC_1",
		"MIC_2",
		"MIC_3",
	},
    0	 
};


typedef struct {
	pthread_t id;

	int thread;
	int threads;
    
	bool huge_table_enable;
	uint32_t p_s_model_start;
	uint32_t p_s_model_end;
 	
    int       seed;
	uint64_t *times[3];
	pthread_barrier_t *barrier;
	
    uint32_t T1_bit_width; //bit width for different columns in the table T1.
    uint32_t T2_bit_width; //bit width for different columns in the table T2.
    uint32_t T3_bit_width; //bit width for different columns in the table T3.
    uint32_t T4_bit_width; //bit width for different columns in the table T4.
    float    T1_selevitity;
    float    T2_selevitity;
    float    T3_selevitity;
    float    T4_selevitity;
	
    uint64_t T1_len;     //size of table T1 for the current thread.
    uint64_t T2_len;     //size of table T2 for the current thread.
    uint64_t T3_len;     //size of table T2 for the current thread.	
} info_t;


void first_compare_32b_predicate(WordUnit *bitvector, uint32_t *data, uint32_t literal, uint64_t len)
{
  __m256i v_literal       = _mm256_set1_epi32 ( (int)literal );
  unsigned char *c_vector = (unsigned char *)bitvector;

  for (uint64_t i = 0; i < len; i += 8)
  {
    //unsigned char mask = c_vector[i>>3];
    //if (mask == 0 ) 
    //  break;
    __m256i v_data       = _mm256_loadu_si256( (__m256i *) (data+i) );
    __m256i v_result     = _mm256_cmpgt_epi32( v_literal, v_data );
    int result           = _mm256_movemask_ps( _mm256_castsi256_ps(v_result) );
    c_vector[i/8]        = (char)result;
  }
}


void compare_32b_predicate(WordUnit *bitvector, uint32_t *data, uint32_t literal, uint64_t len)
{
  __m256i v_literal       = _mm256_set1_epi32 ( (int)literal );
  unsigned char *c_vector = (unsigned char *)bitvector;

  for (uint64_t i = 0; i < len; i += 8)
  {
    unsigned char mask = c_vector[i/8];
    
    if (mask == 0 ) 
      continue;

    __m256i v_data       = _mm256_loadu_si256 ( (__m256i *) (data+i) );
    __m256i v_result     = _mm256_cmpgt_epi32( v_literal, v_data );
    int result           = _mm256_movemask_ps( _mm256_castsi256_ps(v_result) );
    c_vector[i/8]        = (mask & ((char)result) );
  }
}



void *run(void *arg)
{
	info_t *d = (info_t*) arg;
	
	assert(pthread_equal(pthread_self(), d->id));
	bind_thread(d->thread, d->threads);	

	rand32_t *gen              = rand32_init(d->seed);
	pthread_barrier_t *barrier = d->barrier;
    int seed                   = d->seed;
	uint64_t i, T1_len         = d->T1_len;            //number of input codes...
	bool huge_table_enable     = d->huge_table_enable;
	uint32_t T1_bit_width      = d->T1_bit_width;
	uint32_t T2_bit_width      = d->T2_bit_width;
	uint32_t T3_bit_width      = d->T3_bit_width;
	uint32_t T4_bit_width      = d->T4_bit_width;
	uint32_t p_s_model_start   = d->p_s_model_start;
	uint32_t p_s_model_end     = d->p_s_model_end;
		
	float    T1_selevitity     = d->T1_selevitity;
	float    T2_selevitity     = d->T2_selevitity;
	float    T3_selevitity     = d->T3_selevitity;
	float    T4_selevitity     = d->T4_selevitity;
		
  int kNumBytesPerCode_1       = (T1_bit_width+7)/8;
	int kNumPaddingBits_1        = kNumBytesPerCode_1 * 8 - T1_bit_width;
  int kNumBytesPerCode_2       = (T2_bit_width+7)/8;
	int kNumPaddingBits_2        = kNumBytesPerCode_2 * 8 - T2_bit_width;
  int kNumBytesPerCode_3       = (T3_bit_width+7)/8;
	int kNumPaddingBits_3        = kNumBytesPerCode_3 * 8 - T3_bit_width;
  int kNumBytesPerCode_4       = (T4_bit_width+7)/8;
	int kNumPaddingBits_4        = kNumBytesPerCode_4 * 8 - T4_bit_width;

//printf("1\n");
  //uint32_t literal_1           = (uint32_t) ( (1-T1_selevitity) * (float)((1<<T1_bit_width)-1) ); //do greater than literal...
	uint32_t literal_1           = (uint32_t) (    T1_selevitity  * (float)((1<<T1_bit_width)-1) );   //do less    than literal...
  uint32_t literal_2           = (uint32_t) (    T2_selevitity  * (float)((1<<T1_bit_width)-1) );   //do less    than literal...
  uint32_t literal_3           = (uint32_t) (    T3_selevitity  * (float)((1<<T1_bit_width)-1) );   //do less    than literal...
  uint32_t literal_4           = (uint32_t) (    T4_selevitity  * (float)((1<<T1_bit_width)-1) );   //do less    than literal...

  ///////////////////////Generate the input output data..///////////////////////////////////////	
  uint32_t *original_1, *original_2, *original_3, *original_4;  // input original data.  
	WordUnit *bitvector;                //ourput bit map for the original data. 
  uint64_t T1_len_aligned = ( ((T1_len + 63)>>6)<<6 ); //upper boundary to 64-byte alignment.
	
  ///////////////////////allocate space for input data and output bit vector///////////////////////////////////////	
   original_1               = (uint32_t *) malloc_memory(T1_len_aligned*sizeof(uint32_t), huge_table_enable);//use 4K page.	
   if (original_1 == NULL) 
   {
         printf ( "input original_malloc 1 fails\n");
         return NULL;
   }
   original_2               = (uint32_t *) malloc_memory(T1_len_aligned*sizeof(uint32_t), huge_table_enable);//use 4K page.	
   if (original_2 == NULL) 
   {
         printf ( "input original_malloc 2 fails\n");
         return NULL;
   }
   original_3               = (uint32_t *) malloc_memory(T1_len_aligned*sizeof(uint32_t), huge_table_enable);//use 4K page.	
   if (original_3 == NULL) 
   {
         printf ( "input original_malloc 3 fails\n");
         return NULL;
   }   
   original_4               = (uint32_t *) malloc_memory(T1_len_aligned*sizeof(uint32_t), huge_table_enable);//use 4K page.	
   if (original_4 == NULL) 
   {
         printf ( "input original_malloc 4 fails\n");
         return NULL;
   }   
   //consider to use 2M huge table.
   bitvector              = (WordUnit *) malloc_memory(T1_len_aligned/8, huge_table_enable);//use 4K page.	
   if (bitvector == NULL) 
   {
         printf ( "output bitvector_malloc fails\n");
         return NULL;
   }
//printf("2\n");
   //  printf("before set tuple.\n"); 
	 
   //assign random value for two columns....
   for(i=0; i < T1_len; i++){
      uint32_t tmp_1 = rand32_next(gen) & ( (1<<T1_bit_width) - 1 ); 
      original_1[i]  = tmp_1; //
      uint32_t tmp_2 = rand32_next(gen) & ( (1<<T1_bit_width) - 1 ); 
      original_2[i]  = tmp_2; //
      uint32_t tmp_3 = rand32_next(gen) & ( (1<<T1_bit_width) - 1 ); 
      original_3[i]  = tmp_3; //
      uint32_t tmp_4 = rand32_next(gen) & ( (1<<T1_bit_width) - 1 ); 
      original_4[i]  = tmp_4; //
   }
//printf("3\n");
  //  printf("after set data_.\n");  
	
   for(i=0; i < T1_len_aligned/64; i++){
      bitvector[i] = 0; //it is used to load to L2 TLB when huge table is used. 
   }
//printf("4\n");

  for (uint32_t p_s_model = p_s_model_start; p_s_model <= p_s_model_end; p_s_model++)	
  {	
  	barrier = d->barrier; //reuse the barrier resource.../////
  	
  	if (d->thread == 0)
  	{
  	  printf("p_s_model = %d\n", p_s_model);
  	}
      ///////////////////////first barrier: make sure all threads have finished the initialization.///////////////////////
      //////////Otherwise, the writing operations from the above might increase the memory read/write operations./////////
      pthread_barrier_wait(barrier++);
  		
    #ifdef INTEL_PCM_ENABLE		
      if (d->thread == 0)
  	  {   
          PCM_initPerformanceMonitor(&inst_Monitor_Event, NULL);
          PCM_start();
  	  }
    #endif	
		
    ///////////////////////second barrier.to sync all the threads then begin to execute the code./////////////////////		
	  pthread_barrier_wait(barrier++);
		uint64_t t1 = thread_time(); //
	
      //do the job.....	
	  //four_columns_cmp_with_literal_rp_nP_nS four_columns_cmp_with_literal_rp_best_nP_nS
    first_compare_32b_predicate(bitvector, original_1, literal_1, T1_len_aligned);
          compare_32b_predicate(bitvector, original_2, literal_2, T1_len_aligned);
          //compare_32b_predicate(bitvector, original_3, literal_3, T1_len_aligned);
          //compare_32b_predicate(bitvector, original_4, literal_4, T1_len_aligned);

								 
	///////////////////////third barrier to make sure all the threads have finished the execution/////////////////////		
	  pthread_barrier_wait(barrier++);
		 t1 = thread_time() - t1;

#ifdef INTEL_PCM_ENABLE			
	if (d->thread == 0)
	{
        PCM_stop();
        printf("=====print the profiling result==========\n");//PCM_log("======= Partitioning phase profiling results ======\n");
        PCM_printResults();		
		PCM_cleanup();
	}
#endif		
     d->times[0][d->thread] = t1;
	 
	 
    ///////////////fourth barrier to make sure no too much noise comes from the other threads when the thread collects the statistics/////////////////////		
	  pthread_barrier_wait(barrier++);


	  
	 //test the bitmap is right or not....
     //if (d->thread == 0)
	 {   
		 for (size_t ii = 0; ii < T1_len; ii++) //
		{
      //bool real = (original_1[ii] < literal_1);
      bool real = ( (original_1[ii] < literal_1) && (original_2[ii] < literal_2) );
			//bool real = ( (original_1[ii] < literal_1) && (original_2[ii] < literal_2) && (original_3[ii] < literal_3) && (original_4[ii] < literal_4)); 
		  bool eval  = GetBit(bitvector, ii); //bvblock->GetBit(ii); 
      if (real !=  eval )
			{
        printf("thread_%d::index_%d:  eval: %d, real: %d \n", d->thread, ii, eval, real);
			  break;
			}
    }
	 }	
     //  printf("d-thread = %d, loop = 100, ns = %x\n", d->thread, t1);
		
	if (d->thread == 0) {
		uint64_t t1 = 0.0;
		for (size_t t = 0 ; t != d->threads ; ++t) {
			t1 += d->times[0][t];
		}
		printf("%2d-%2d-%2d-%2d-bit codes, time: %6.3f, codes_per_ns: %6.3f\n", T1_bit_width, T2_bit_width, T3_bit_width, T4_bit_width, ((double)t1 / (double)d->threads), 
		       (T1_len * d->threads * 1.0) / ((double)t1 / (double)d->threads) );
	}
  }
//}


	//free(compressed);
	//free(decompressed);
	//free(bitmap);
	pthread_exit(NULL);
}


  
void main(int argc, char **argv)
{
  uint64_t t, thread_num   = argc > 1 ? atoi(argv[1]) : hardware_threads(); //deflaut to use all threads.
  bool huge_table_enable   = argc > 2 ? atoi(argv[2]) : false;              //deflaut to use normal 4k page.
  uint32_t  T1_bit_width   = argc > 3 ? atoi(argv[3]) : 17;                 //default bit width for 1st column
  uint32_t  T2_bit_width   = argc > 4 ? atoi(argv[4]) : 17;                 //default bit width for 2nd column
  uint32_t  T3_bit_width   = argc > 5 ? atoi(argv[5]) : 17;                 //default bit width for 3th column
  uint32_t  T4_bit_width   = argc > 6 ? atoi(argv[6]) : 17;                 //default bit width for 4th column
  uint32_t  prefetch_model = argc > 7 ? atoi(argv[7]) : 0;                  //default: enable prefetcher
  uint32_t  p_s_model_start= argc > 8 ? atoi(argv[8]) : 0;                  //0:nP_nS, 1:nP_S, 2:P_ns, 3:P_S,   
  uint32_t  p_s_model_end  = argc > 9 ? atoi(argv[9]) : 0;                  //4:pf_nP_nS, 1:pf_nP_S, 2:pf_P_ns, 3:pf_P_S,   
  float     T1_selevitity  = argc > 10 ? atof(argv[10]) : 0.5; 
  float     T2_selevitity  = argc > 11 ? atof(argv[11]) : 0.5; 
  float     T3_selevitity  = argc > 12 ? atof(argv[12]) : 0.5; 
  float     T4_selevitity  = argc > 13 ? atof(argv[13]) : 0.5; 

 
  //modify the L2 cache's prefetching model only when the input model is not default value (0). 
  if (prefetch_model != 0)  
    inst_Monitor_Event.prefetch_model = prefetch_model;
    
  uint64_t tuples          = 1000000000; //

  printf("tuples = %d, thread number = %d, huge_table_enable = %d, T1_bit_width = %d, T2_bit_width = %d, T3_bit_width = %d, T4_bit_width = %d, prefetch_model = %d\n", 
         tuples,       thread_num,         huge_table_enable,      T1_bit_width,      T2_bit_width,      T3_bit_width,      T4_bit_width,      prefetch_model     ); 
 

  
  	srand(time(NULL));

  //std::cout << "first\n";  
  std::vector<uint64_t> task_len;
  task_len.resize(thread_num);
  uint64_t size_for_each_thread = compute_task_len_for_each_thread(task_len, tuples, thread_num);


    //initialize 20 barriers to sync between threads.
	int b, barrier_num = 20;
	pthread_barrier_t barrier[barrier_num];
	for (b = 0 ; b != barrier_num; ++b)
		pthread_barrier_init(&barrier[b], NULL, thread_num);

	
    info_t info[thread_num]; //
	uint64_t times[3][thread_num];
	size_t set_bits[thread_num];
	
	//for affinity setting.
    pthread_t tid[thread_num];
    pthread_attr_t attr;
    cpu_set_t set; //cpu_set_t *set = (cpu_set_t *) malloc (sizeof (cpu_set_t)); //
    pthread_attr_init(&attr);
		
	//printf("HHHHHHHHHHHHHHHHHtest 1\n"); //OK
	 
	for (t = 0 ; t != thread_num ; ++t) 
	{
	    //for affinity setting.		
        int cpu_idx = get_cpu_id(t);
        //DEBUGMSG(1, "Assigning thread-%d to CPU-%d\n", i, cpu_idx);
        CPU_ZERO(&set);
        CPU_SET(cpu_idx, &set);
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &set);

        info[t].seed              = rand();
		info[t].huge_table_enable = huge_table_enable;
		info[t].T1_bit_width      = T1_bit_width;
		info[t].T2_bit_width      = T2_bit_width;
		info[t].T3_bit_width      = T3_bit_width;
		info[t].T4_bit_width      = T4_bit_width;

		info[t].p_s_model_start   = p_s_model_start;
		info[t].p_s_model_end     = p_s_model_end;
	
		info[t].T1_selevitity     = T1_selevitity;
		info[t].T2_selevitity     = T2_selevitity;
		info[t].T3_selevitity     = T3_selevitity;
		info[t].T4_selevitity     = T4_selevitity;
 	
		info[t].thread            = t;
		info[t].threads           = thread_num;
		info[t].barrier           = barrier;

		info[t].times[0] = times[0];
		info[t].times[1] = times[1];
		info[t].times[2] = times[2];
		
		info[t].T1_len          = task_len[t]; //same for all the threads (64*), except the last one 
		//printf("task_len[%d] = %d\n", t, task_len[t]);
		pthread_create(&info[t].id, &attr, run, (void*) &info[t]); //&info[t].id, NULL
	}
	
   //finish the execution of all threads......	
	for (t = 0 ; t != thread_num ; ++t)
		pthread_join(info[t].id, NULL);
	
	
	
	for (b = 0 ; b != barrier_num; ++b)
		pthread_barrier_destroy(&barrier[b]);
	
   return;//EXIT_SUCCESS  

}



