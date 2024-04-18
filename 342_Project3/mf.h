//
// Created by vboxuser on 09.04.2024.
//

#ifndef PROJECT3_MF_H
#define PROJECT3_MF_H

#include <stdio.h>
#include <semaphore.h>
#define MAX_PROCESSES 16

// min and max msg length
#define MIN_DATALEN 1 // byte
#define MAX_DATALEN 4096 // bytes

#define CONFIG_FILENAME "config.txt"

// min and max queue size
#define MIN_MQSIZE  16 // KB
#define MAX_MQSIZE  128 // KB
// MQSIZE should be a multiple of 4KB
// 1 KB is 2^12 bytes = 1024 bytes

#define MAXFILENAME 128
// max file or shared memory name

#define MAX_MQNAMESIZE 128
// max message queue name size

// min and max shared memory region size
#define MIN_SHMEMSIZE  512  // in KB
#define MAX_SHMEMSIZE  8192 // in KB
// shared memory size must be a power of 2


#define MF_SUCCESS 0
#define MF_ERROR -1

typedef struct {
    char shmem_name[MAX_MQNAMESIZE];
    size_t shmem_size;
    int max_msgs_in_queue;
    int max_queues_in_shmem;
} mf_config; // message queue configuration

// message queue struct
typedef struct {
    char mq_name[MAX_MQNAMESIZE];
    int qid; // queue id

    int reference_count;
    unsigned int size;
    int writePos; // head represents the index of the first element in the queue
    int readPos; // tail represents the index of the last element in the queue
    char *buffer;

    size_t offset; // offset of the queue from the beginning of the shared memory
    sem_t *semaphore;
} mf_message_queue;

// hole struct for shared memory
typedef struct {
    size_t size; // size of the hole
    size_t offset; // offset of the hole from the beginning of the shared memory
} mf_hole;

typedef struct {
    unsigned int shm_size;
    void* shm_ptr;
    int mq_count; // number of message queues in the shared memory
    sem_t shm_sem;
    mf_hole* holes;
    int hole_count; // number of holes in the shared memory
} shared_mem;


int mf_init();
int mf_destroy();
int mf_connect();
int mf_disconnect();
int mf_create(char* mqname, int mqsize);
int mf_remove(char* mqname);
int mf_open(char* mqname);
int mf_close(int qid);
int mf_send(int qid, void* bufptr, int datalen);
int mf_recv(int qid, void* bufptr, int bufsize);
int mf_print();

#endif //PROJECT3_MF_H