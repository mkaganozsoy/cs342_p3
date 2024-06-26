#include "mf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

mf_config config; // global variable to store the configuration
int shm_fd = -1; // shared memory file descriptor
void* shm_ptr = NULL; // shared memory pointer

void trim_newline(char* string) {
    char* newline = strchr(string, '\n');
    if (newline) {
        *newline = '\0'; // Replace newline with null terminator
    }
}

int read_file(char* filename){
    FILE* file = fopen(filename, "r");
    printf("filename: %s\n", filename);
    if(file == NULL){
        printf("Error opening file %s\n", filename);
        return MF_ERROR;
    }
    char line[256];
    while(fgets(line, sizeof(line), file)){

        trim_newline(line);
        if(line[0] == '#' || line[0] == '\0'){
            continue;
        }
        char* key = strtok(line, " ");
        char* value = strtok(NULL, " ");

        if(key == NULL || value == NULL){
            continue;
        }

        if(strcmp(key, "SHMEM_NAME") == 0){
            // Remove quotes from the value if present
            if (value[0] == '"') {
                value++; // Skip the starting quote
                char* endQuote = strchr(value, '"');
                if (endQuote) *endQuote = '\0'; // Nullify the ending quote
            }
            strcpy(config.shmem_name, value);
        } else if(strcmp(key, "SHMEM_SIZE") == 0){
            config.shmem_size = atoi(value);
        } else if(strcmp(key, "MAX_MSGS_IN_QUEUE") == 0){
            config.max_msgs_in_queue = atoi(value);
        } else if(strcmp(key, "MAX_QUEUES_IN_SHMEM") == 0){
            config.max_queues_in_shmem = atoi(value);
        }
    }
    fclose(file);
    return MF_SUCCESS;
}


void* connect_shared_memory(const char* name, size_t size) {

    // Open the existing shared memory object
    int shm_fd = shm_open(name, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open in mf_connect");
        return NULL;
    }

    // Map the shared memory object
    void* shm_ptr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED) {
        perror("mmap in mf_connect");
        close(shm_fd);
        return NULL;
    }

    // Close the file descriptor as it is no longer needed after mmap
    close(shm_fd);

    return shm_ptr;
}


mf_message_queue* mf_get_queue(int qid){
    // first get the shared memory struct
    shared_mem* sh_mem = (shared_mem*) shm_ptr;
    // start checking for qid in the message queues after the shared memory struct
    char *ptr = (char*) shm_ptr + sizeof(shared_mem);
    for(int i = 0; i < sh_mem->mq_count; i++){
        mf_message_queue* mq = (mf_message_queue*) ptr;
        if(mq->qid == qid){
            return mq;
        }
        ptr += sizeof(mf_message_queue) + mq->size;
    }
    return NULL;
}

void update_hole_for_create(int index, size_t size, shared_mem* sh_mem){
    // update the hole
    sh_mem->holes[index].offset += size;
    sh_mem->holes[index].size -= size;

    // if the hole is empty, remove it
    if(sh_mem->holes[index].size == 0){
        sh_mem->hole_count--;
        for(int j = index; j < sh_mem->hole_count; j++){
            sh_mem->holes[j] = sh_mem->holes[j+1];
        }
    }
}

int mf_init(){

    //read config.txt file
    memset(&config, 0, sizeof(config));
    if(read_file(CONFIG_FILENAME) == MF_ERROR){
        printf("Error reading config.txt file\n");
        return MF_ERROR;
    }

    // test config.txt values
    printf("SHMEM_NAME=%s\n", config.shmem_name);
    printf("SHMEM_SIZE=%ld\n", config.shmem_size);
    printf("MAX_MSGS_IN_QUEUE=%d\n", config.max_msgs_in_queue);
    printf("MAX_QUEUES_IN_SHMEM=%d\n", config.max_queues_in_shmem);
    printf("test 1\n");

    //create shared memory
    shared_mem sh_mem;
    shm_fd = shm_open(config.shmem_name, O_CREAT | O_RDWR, 0666);
    if(shm_fd == -1){
        printf("Shared memory failed\n");
        return MF_ERROR;
    }
    // setting the size of the shared memory segment
    if(ftruncate(shm_fd, (int) config.shmem_size) != 0){
        printf("ftruncate failed\n");
        close(shm_fd);
        shm_unlink(config.shmem_name); // remove shared memory if ftruncate fails
        return MF_ERROR;
    }
    // map the shared memory segment to the address space of the process
    shm_ptr = mmap(0, config.shmem_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if(shm_ptr == MAP_FAILED){
        printf("mmap failed\n");
        close(shm_fd);
        shm_unlink(config.shmem_name); // remove shared memory if mmap fails
        return MF_ERROR;
    }
    // initialize the shared memory struct - initially there is a single hole that spans the entire shared memory
    sh_mem.shm_ptr = shm_ptr;
    sh_mem.shm_size = config.shmem_size;
    sh_mem.mq_count = 0;
    sh_mem.hole_count = 1;
    sh_mem.holes = (mf_hole*) shm_ptr; // holes must be initialized - buna bakalım tekrardan
    sh_mem.holes[0].offset = sizeof(sh_mem);
    sh_mem.holes[0].size = config.shmem_size - sizeof(sh_mem);


    printf("mf initialized\n");
    return MF_SUCCESS;
}

int mf_destroy(){

    // cleanup
    if(shm_ptr != MAP_FAILED && shm_ptr != NULL){
        // unmap the shared memory segment from the address space of the process
        if(munmap(shm_ptr, config.shmem_size) == -1){
            printf("munmap failed\n");
            return MF_ERROR;
        }
    }

    if(shm_fd != -1){
        // close the shared memory file descriptor
        if(close(shm_fd) == -1){
            printf("close failed\n");
            return MF_ERROR;
        }
    }

    // unlink the shared memory
    if(shm_unlink(config.shmem_name) == -1){
        printf("shm_unlink failed\n");
        return MF_ERROR;
    }
    printf("mf destroyed\n");
    return MF_SUCCESS;
}

int mf_connect(){
    // Read the configuration file
    memset(&config, 0, sizeof(config));
    if(read_file(CONFIG_FILENAME) == MF_ERROR){
        perror("Error reading config.txt file");
        return MF_ERROR;
    }

    // Open the shared memory
    shm_ptr = connect_shared_memory(config.shmem_name, config.shmem_size);
    if(shm_ptr == NULL){
        printf("Failed to connect to shared memory\n");
        return MF_ERROR;
    }

    // At this point, shm_ptr points to the beginning of the shared memory segment
    // Here you can perform additional initialization specific to the connecting process
    // For example, initializing pointers to shared structures, semaphores, etc.

    // Store shm_ptr in a global or pass it around as needed for further operations

    return MF_SUCCESS;
}

int mf_disconnect(){
    if (shm_ptr == NULL || shm_ptr == MAP_FAILED) {
        printf("Error: process is not connected to shared memory\n");
        return MF_ERROR;
    }

    if (munmap(shm_ptr, config.shmem_size) == -1) {
        perror("munmap in mf_disconnect");
        return MF_ERROR;
    }

    shm_ptr = NULL;

    // Check if the shared memory file descriptor is open
    if (shm_fd != -1) {
        // Close the file descriptor
        if (close(shm_fd) == -1) {
            perror("close failed in mf_disconnect");
            return MF_ERROR;
        }
        shm_fd = -1;  // Reset the file descriptor after closing
    }

    return MF_SUCCESS;
}

int mf_create(char* mqname, int mqsize){
    // first get the shared memory struct
    shared_mem* sh_mem = (shared_mem*) shm_ptr;

    size_t real_mq_size = sizeof(mf_message_queue) + mqsize;
    // check if there is enough space in the shared memory for the new message queue
    for(int i = 0; i < sh_mem->hole_count; i++){
        if(sh_mem->holes[i].size >= real_mq_size){
            // create the message queue
            mf_message_queue* mq = (mf_message_queue*) ((char*) shm_ptr + sh_mem->holes[i].offset);
            // update the number of message queues in the shared memory
            sh_mem->mq_count++;

            // initialize the message queue
            mq->qid = sh_mem->mq_count;
            // copy the message queue name using MAX_MQNAMESIZE
            strncpy(mq->mq_name, mqname, MAX_MQNAMESIZE);

            mq->reference_count = 0;
            mq->size = mqsize;
            mq->writePos = 0;
            mq->readPos = 0;
            mq->buffer = (char*) mq + sizeof(mf_message_queue);
            mq->offset = sh_mem->holes[i].offset;

            // it must be a named semaphore - check this part
            //mq->semaphore = (sem_t*) ((char*) mq + sizeof(mf_message_queue) + mqsize);
            //sem_init(mq->semaphore, 1, 1);
            // initialize the named semaphore
            char sem_name[MAX_MQNAMESIZE + 5]; // 5 for the prefix "/sem_"
            sprintf(sem_name, "/sem_%s", mqname);
            // use sem_unlink to remove the semaphore if it already exists
            sem_unlink(sem_name);
            // create the named semaphore
            mq->semaphore = sem_open(sem_name, O_CREAT, 0666, 1);
            if (mq->semaphore == SEM_FAILED) {
                perror("sem_open in mf_create");
                free(sem_name);
                return MF_ERROR;
            }

            update_hole_for_create(i, real_mq_size, sh_mem);
            printf("Message queue created with id %d\n", mq->qid);
            return MF_SUCCESS;
        }
    }

    return MF_ERROR;
}

int mf_remove(char* mqname){
    return 0;
}

int mf_open(char* mqname){
    return 0;
}

int mf_close(int qid){
    return 0;
}

int mf_send(int qid, void* bufptr, int datalen){
    return 0;
}

int mf_recv(int qid, void* bufptr, int bufsize){


    return 0;
}

int mf_print(){
    return 0;
}
