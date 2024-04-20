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
#include <stdbool.h>

mf_config config; // global variable to store the configuration
static int shm_fd = -1; // shared memory file descriptor
static void* shm_ptr; // shared memory pointer

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
            if(atoi(value) < MIN_SHMEMSIZE || atoi(value) > MAX_SHMEMSIZE){
                printf("Shared memory size is out of range\n");
                return MF_ERROR;
            }
            config.shmem_size = atoi(value) * 1024;
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
    shm_fd = shm_open(name, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open in mf_connect");
        return NULL;
    }
    printf("shm_fd: %d\n", shm_fd);
    // Map the shared memory object
    shm_ptr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED) {
        perror("mmap in mf_connect");
        close(shm_fd);
        return NULL;
    }
    printf("shm_ptr: %p\n", shm_ptr);
    // Close the file descriptor as it is no longer needed after mmap
    close(shm_fd);

    return shm_ptr;
}


mf_message_queue* mf_get_queue_by_id(int qid){
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

mf_message_queue* mf_get_queue_by_name(char* mqname){
    // first get the shared memory struct
    shared_mem* sh_mem = (shared_mem*) shm_ptr;
    // start checking for qid in the message queues after the shared memory struct
    char *ptr = (char*) shm_ptr + sizeof(shared_mem);
    for(int i = 0; i < sh_mem->mq_count; i++){
        mf_message_queue* mq = (mf_message_queue*) ptr;
        if(strcmp(mq->mq_name, mqname) == 0){
            return mq;
        }
        ptr += sizeof(mf_message_queue) + mq->size;
    }
    return NULL;
}


void update_hole(int index, size_t size, int operation, shared_mem* sh_mem) {
    if (operation == 1) {  // Creating a new message queue
        // Reduce the size of the hole
        sh_mem->holes[index].offset += size;
        sh_mem->holes[index].size -= size;

        // If the hole is empty, remove it
        if (sh_mem->holes[index].size == 0) {
            sh_mem->hole_count--;
            for (int j = index; j < sh_mem->hole_count; j++) {
                sh_mem->holes[j] = sh_mem->holes[j + 1];
                printf("Hole Removed: offset=%zu, size=%zu\n", sh_mem->holes[index].offset, sh_mem->holes[index].size);
            }
        } else{
            printf("Hole updated: offset=%zu, size=%zu\n", sh_mem->holes[index].offset, sh_mem->holes[index].size);
        }
    } else if (operation == 2) {  // Removing a message queue
        printf("Hole Update While Removing a Message Queue");
        size_t mq_offset = sh_mem->holes[index].offset;
        size_t mq_size = size;
        size_t mq_end = mq_offset + mq_size;
        int hole_before = -1;
        int hole_after = -1;

        // Check for existing holes before and after the message queue
        for (int i = 0; i < sh_mem->hole_count; i++) {
            if (sh_mem->holes[i].offset + sh_mem->holes[i].size == mq_offset) {
                hole_before = i;
            }
            if (sh_mem->holes[i].offset == mq_end) {
                hole_after = i;
            }
        }

        // Merge with previous and/or next hole if available
        if (hole_before != -1 && hole_after != -1) {
            sh_mem->holes[hole_before].size += (mq_size + sh_mem->holes[hole_after].size);
            // Remove the hole after as it's now merged
            for (int j = hole_after; j < sh_mem->hole_count - 1; j++) {
                sh_mem->holes[j] = sh_mem->holes[j + 1];
            }
            sh_mem->hole_count--;
            printf("Hole Merged: offset=%zu, size=%zu\n", sh_mem->holes[hole_before].offset, sh_mem->holes[hole_before].size);
        } else if (hole_before != -1) {
            sh_mem->holes[hole_before].size += mq_size;
            printf("Hole updated: offset=%zu, size=%zu\n", sh_mem->holes[hole_before].offset, sh_mem->holes[hole_before].size);
        } else if (hole_after != -1) {
            sh_mem->holes[hole_after].offset = mq_offset;
            sh_mem->holes[hole_after].size += mq_size;
            printf("Hole updated: offset=%zu, size=%zu\n", sh_mem->holes[hole_after].offset, sh_mem->holes[hole_after].size);
        } else {
            // No adjacent holes, create a new hole
            sh_mem->holes[sh_mem->hole_count].offset = mq_offset;
            sh_mem->holes[sh_mem->hole_count].size = mq_size;
            sh_mem->hole_count++;
            printf("New Hole Created: offset=%zu, size=%zu\n", sh_mem->holes[sh_mem->hole_count - 1].offset, sh_mem->holes[sh_mem->hole_count - 1].size);
        }
        printf("Hole updated: offset=%zu, size=%zu\n", sh_mem->holes[index].offset, sh_mem->holes[index].size);
    }
    printf("Hole Count: %d\n", sh_mem->hole_count);
}


int mf_init(){

    //read config.txt file
    memset(&config, 0, sizeof(config));
    // check if the filename exceeds the maximum size
    if(strlen(CONFIG_FILENAME) > MAXFILENAME){
        printf("Config filename exceeds the maximum size\n");
        return MF_ERROR;
    }
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


    shm_fd = shm_open(config.shmem_name, O_CREAT | O_RDWR, 0666);
    if(shm_fd == -1){
        printf("Shared memory failed\n");
        return MF_ERROR;
    }
    printf("shm_fd: %d\n", shm_fd);
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
    //create shared memory
    shared_mem* sh_mem = (shared_mem*) shm_ptr;
    // initialize the shared memory struct - initially there is a single hole that spans the entire shared memory
    printf("shm_ptr: %p\n", shm_ptr);
    //sh_mem->shm_ptr = shm_ptr;
    //printf("shm_ptr only: %p\n", sh_mem->shm_ptr);
    sh_mem->shm_size = config.shmem_size;
    sh_mem->mq_count = 0;
    sh_mem->hole_count = 1;
    sh_mem->holes = (mf_hole*) shm_ptr; // holes must be initialized - buna bakalım tekrardan
    sh_mem->holes[0].offset = sizeof(shared_mem);
    sh_mem->holes[0].size = config.shmem_size - sizeof(shared_mem);

    printf("Hole count: %d\n", sh_mem->hole_count);
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
        printf("shm_fd: %d\n", shm_fd);
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

    printf("shm_fd: %d\n", shm_fd);
    printf("shm_ptr: %p\n", shm_ptr);

    printf("mf destroyed\n");
    return MF_SUCCESS;
}

int mf_connect(){
    // Read the configuration file
    printf("Hole count: %d\n", ((shared_mem*) shm_ptr)->hole_count);
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

    printf("shm_ptr: %p\n", shm_ptr);
    //printf("shm_ptr from shared_mem: %p\n", ((shared_mem*) shm_ptr)->shm_ptr);
    // At this point, shm_ptr points to the beginning of the shared memory segment
    // Here you can perform additional initialization specific to the connecting process
    // For example, initializing pointers to shared structures, semaphores, etc.

    // Store shm_ptr in a global or pass it around as needed for further operations
    printf("mf connected\n");
    printf("Hole count: %d\n", ((shared_mem*) shm_ptr)->hole_count);
    return MF_SUCCESS;
}

int mf_disconnect(){
    printf("In disconnect\n");
    if (shm_ptr == NULL || shm_ptr == MAP_FAILED) {
        printf("Error: process is not connected to shared memory\n");
        return MF_ERROR;
    }
    printf("In disconnect2\n");
    if (munmap(shm_ptr, config.shmem_size) == -1) {
        perror("munmap in mf_disconnect");
        return MF_ERROR;
    }

    shm_ptr = NULL;

    // Check if the shared memory file descriptor is open
    printf("In disconnect3\n");
    if (shm_fd != -1 ) {
        printf("Closing file descriptor: %d\n", shm_fd);
        if (close(shm_fd) == -1) {
            perror("close failed in mf_disconnect");
            return MF_ERROR;
        }
        shm_fd = -1;
    } else {
        printf("File descriptor was already closed or is invalid: %d\n", shm_fd);
    }

    return MF_SUCCESS;
}

int mf_create(char* mqname, int mqsize){

    if (!mqname) {
        printf("Null pointer provided for mqname.\n");
        return MF_ERROR;
    }
    // Check shared memory pointer validity
    if (!shm_ptr || shm_ptr == MAP_FAILED) {
        printf("Invalid shared memory pointer.\n");
        return MF_ERROR;
    }

    if(mqsize > MAX_MQSIZE){
        printf("Message queue size exceeds the maximum size\n");
        return MF_ERROR;
    }
    if(mqsize < MIN_MQSIZE){
        printf("Message queue size is less than the minimum size\n");
        return MF_ERROR;
    }
    if(strlen(mqname) > MAX_MQNAMESIZE){
        printf("Message queue name exceeds the maximum size\n");
        return MF_ERROR;
    }

    printf("MF_CREATE: Attempting to create a message queue '%s' of size %d.\n", mqname, mqsize);

    // first get the shared memory struct
    shared_mem* sh_mem = (shared_mem*) shm_ptr;
    printf("sh_mem: %p\n", sh_mem);

    // mqsize is given as KB, convert it to bytes
    size_t real_mq_size = sizeof(mf_message_queue) + (mqsize * 1024);
    // check if there is enough space in the shared memory for the new message queue
    int hole_count = sh_mem->hole_count;
    for(int i = 0; i < hole_count; i++){
        if(sh_mem->holes[i].size >= real_mq_size){
            // create the message queue
            char* target_address = (char*)shm_ptr + sh_mem->holes[i].offset;
            if (target_address + real_mq_size > (char*)shm_ptr + config.shmem_size) {
                printf("Allocation would exceed shared memory bounds.\n");
                continue;
            }

            size_t mq_offset = sh_mem->holes[i].offset;
            mf_message_queue* mq = (mf_message_queue*) ((char*) shm_ptr + mq_offset);
            // update the number of message queues in the shared memory
            sh_mem->mq_count++;
            // initialize the message queue
            mq->qid = sh_mem->mq_count;
            // copy the message queue name using MAX_MQNAMESIZE
            strncpy(mq->mq_name, mqname, MAX_MQNAMESIZE);
            mq->mq_name[MAX_MQNAMESIZE - 1] = '\0'; // null terminate the string

            mq->reference_count = 0;
            mq->size = mqsize;
            mq->writePos = 0;
            mq->readPos = 0;
            mq->buffer = (char*) mq + sizeof(mf_message_queue);

            mq->offset = mq_offset;
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
                //free(sem_name);
                return MF_ERROR;
            }

            update_hole(i, real_mq_size, 1, sh_mem);
            printf("MF_CREATE: Message queue %s is created\n", mqname);
            printf("MF_CREATE: Message queue created with ID %d at offset %lu.\n", mq->qid, mq->offset);
            return MF_SUCCESS;
        }
    }

    return MF_ERROR;
}

int mf_remove(char* mqname) {

    // first get the shared memory struct
    shared_mem* sh_mem = (shared_mem*) shm_ptr;

    mf_message_queue* mq  = mf_get_queue_by_name(mqname);
    if (mq == NULL) {
        printf("Message queue %s not found\n", mqname);
        return MF_ERROR;
    }
    // Check if the message queue is still in use
    if (mq->reference_count > 0) {
        return MF_ERROR;  // Cannot remove, still in use
    }

    // Close and unlink the semaphore
    sem_close(mq->semaphore);
    char sem_name[MAX_MQNAMESIZE + 5];  // Adjust for prefix length
    sprintf(sem_name, "/sem_%s", mqname);
    sem_unlink(sem_name);

    // Calculate the size of the message queue for memory deallocation
    size_t mq_size = sizeof(mf_message_queue) + mq->size;

    // Adjust the hole for the removed message queue
    update_hole(0, mq_size, 2, sh_mem);
    sh_mem->mq_count--;

    printf("Message queue %s removed successfully\n", mqname);
    return MF_SUCCESS;
}
int mf_open(char* mqname){

    printf("Opening Message queue %s\n", mqname);

    mf_message_queue *mq = mf_get_queue_by_name(mqname);
    if(mq == NULL){
        printf("Message queue %s not found\n", mqname);
        return MF_ERROR;
    }

    printf("Trying to get the semaphore%d\n", getpid());
    if(sem_wait(mq->semaphore) == -1){ // wait for the semaphore
        perror("sem_wait in mf_open");
        return MF_ERROR;
    }

    mq->reference_count++;
    // semaphore
    if(sem_post(mq->semaphore) == -1){ // release the semaphore
        perror("sem_post in mf_open");
        return MF_ERROR;
    }

    printf("Message queue %s opened with id %d\n", mqname, mq->qid);
    return mq->qid;
}

int mf_close(int qid){
    // Ensure the shared memory pointer is valid
    if (shm_ptr == NULL || shm_ptr == MAP_FAILED) {
        perror("Error: shared memory is not properly initialized or connected");
        return -1;
    }

    // get the message queue by id
    mf_message_queue* mq = mf_get_queue_by_id(qid);
    if(mq == NULL){
        printf("Message queue with id %d not found\n", qid);
        return MF_ERROR;
    }

    // safely decrement the reference count
    if (sem_wait(mq->semaphore) == -1) {
        perror("sem_wait failed in mf_close");
        return MF_ERROR;
    }
    if(mq->reference_count <= 0){
        printf("Message queue with id %d is already closed\n", qid);
        return MF_ERROR;
    }

    if (sem_post(mq->semaphore) == -1) {
        perror("sem_post failed in mf_close");
        return MF_ERROR;
    }

    return MF_SUCCESS;
}

int mf_send(int qid, void* bufptr, int datalen){
    if(bufptr == NULL || datalen <= 0){
        printf("Buffer pointer is NULL\n");
        return MF_ERROR;
    }
    if(datalen > MAX_DATALEN){
        printf("Data length exceeds the maximum size\n");
        return MF_ERROR;
    }

    // get the message queue
    mf_message_queue* mq = mf_get_queue_by_id(qid);
    if(mq == NULL){
        printf("Message queue with ID %d not found\n", qid);
        return MF_ERROR;
    }

    // check if the message queue is

    if (sem_wait(mq->semaphore) == -1) {
        perror("sem_wait in mf_send");
        return MF_ERROR;
    }

    size_t available_space = (mq->writePos >= mq->readPos) ? mq->size - (mq->writePos - mq->readPos) : mq->readPos - mq->writePos - 1;

    while(available_space < datalen){
        // wait for the semaphore
        if (sem_post(mq->semaphore) == -1) {
            perror("sem_post in mf_send");
            return MF_ERROR;
        }
        // wait for the semaphore
        if (sem_wait(mq->semaphore) == -1) {
            perror("sem_wait in mf_send");
            return MF_ERROR;
        }

        available_space = (mq->writePos >= mq->readPos) ? mq->size - (mq->writePos - mq->readPos) : mq->readPos - mq->writePos - 1;
    }

    // Copy data to the buffer
    int endPos = (mq->writePos + datalen) % mq->size;
    if (mq->writePos + datalen > mq->size) {
        // Wrap around case
        int firstPartSize = mq->size - mq->writePos;
        memcpy(mq->buffer + mq->writePos, bufptr, firstPartSize);
        memcpy(mq->buffer, (char *)bufptr + firstPartSize, datalen - firstPartSize);
    } else {
        // Straightforward case
        memcpy(mq->buffer + mq->writePos, bufptr, datalen);
    }
    mq->writePos = endPos;

    //Release the semaphore
    if (sem_post(mq->semaphore) == -1) {
        perror("sem_post in mf_send");
        return MF_ERROR;
    }

    return MF_SUCCESS;
}

int mf_recv(int qid, void *bufptr, int bufsize) {
    // Validate input parameters
    if (!bufptr) {
        printf("Buffer pointer is NULL.\n");
        return MF_ERROR;
    }

    if (bufsize < MAX_DATALEN) {
        printf("Buffer size is too small to hold any message.\n");
        return MF_ERROR;
    }

    // Ensure the shared memory pointer is valid
    if (shm_ptr == NULL || shm_ptr == MAP_FAILED) {
        perror("Error: shared memory is not properly initialized or connected");
        return MF_ERROR;
    }

    // Get the message queue by ID
    mf_message_queue *mq = mf_get_queue_by_id(qid);
    if (!mq) {
        printf("Message queue with ID %d not found.\n", qid);
        return MF_ERROR;
    }

    // Wait for the semaphore to ensure exclusive access to the message queue
    if (sem_wait(mq->semaphore) == -1) {
        perror("sem_wait in mf_recv");
        return MF_ERROR;
    }

    // Check if there are any messages in the queue
    while (mq->readPos == mq->writePos) {
        // Release the semaphore if no message is available
        if (sem_post(mq->semaphore) == -1) {
            perror("sem_post in mf_recv");
        }
        printf("No messages available in the queue to read.\n");
        return MF_ERROR;
    }

    // Calculate the start of the message buffer and message size
    char *msg_start = mq->buffer + mq->readPos;
    int message_size = *(int *)msg_start; // Assuming the first bytes indicate the size of the message

    // Ensure the message can fit in the provided buffer
    if (message_size > bufsize) {
        if (sem_post(mq->semaphore) == -1) {
            perror("sem_post in mf_recv");
        }
        printf("Provided buffer is too small for the message.\n");
        return MF_ERROR;
    }

    // Copy the message to the provided buffer
    memcpy(bufptr, msg_start + sizeof(int), message_size);

    // Update the read position
    mq->readPos += sizeof(int) + message_size;
    if (mq->readPos >= mq->size) { // Wrap around if at the end of the buffer
        mq->readPos = 0;
    }

    // Release the semaphore
    if (sem_post(mq->semaphore) == -1) {
        perror("sem_post in mf_recv");
        return MF_ERROR;
    }

    return message_size;
}


int mf_print(){
    // first get the shared memory struct from the shared memory pointer
    shared_mem* sh_mem = (shared_mem*) shm_ptr;
    printf("shm_ptr: %p\n", shm_ptr); // shm_ptr çalışıyo - ama sh_mem çalışmıyo
    //printf("shm_ptr from shared_mem: %p\n", sh_mem->shm_ptr);
    printf("Shared Memory:\n");
    printf("Size: %u\n", sh_mem->shm_size); // print unsigned int with
    printf("Number of message queues: %d\n", sh_mem->mq_count);
    printf("Number of holes: %d\n", sh_mem->hole_count);
    //printf("shm_mem: %p\n", sh_mem->shm_ptr);

    // start checking for qid in the message queues after the shared memory struct
    char *ptr = (char*) shm_ptr + sizeof(shared_mem);
    printf("Message Queues:\n");
    for(int i = 0; i < sh_mem->mq_count; i++){
        mf_message_queue* mq = (mf_message_queue*) ptr;
        printf("Message Queue %d:\n", mq->qid);
        printf("Name: %s\n", mq->mq_name);
        printf("Reference Count: %d\n", mq->reference_count);
        printf("Size: %u\n", mq->size);
        printf("Write Position: %d\n", mq->writePos);
        printf("Read Position: %d\n", mq->readPos);
        printf("Buffer: %p\n", mq->buffer);
        printf("Offset: %lu\n", mq->offset);
        printf("Semaphore address: %p\n", mq->semaphore);
        ptr += sizeof(mf_message_queue) + mq->size;
    }

    // print the holes
    printf("Holes:\n");
    for(int i = 0; i < sh_mem->hole_count; i++){
        printf("Hole %d:\n", i);
        printf("Offset: %lu\n", sh_mem->holes[i].offset);
        printf("Size: %lu\n", sh_mem->holes[i].size);
    }

    return MF_SUCCESS;
}
