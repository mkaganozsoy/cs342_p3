#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <malloc.h>
#include <assert.h>
#include <string.h>
#include "mf.h"

// signal handler function that calls mf_destroy() and exits

void signal_handler(int sig) {
    printf("Signal %d received\n", sig);
    mf_destroy();
    exit(0);
}

int main() {
    printf("mfserver pid=%d\n", (int)getpid());

    // Initialize MF library
    printf("-----------MF_INIT TEST-----------\n");
    mf_init();
    printf("-----------MF INIT COMPLETED-----------\n\n");
    // Connect using the MF library
    printf("-----------MF_CONNECT TEST-----------\n");
    int status = mf_connect();
    if (status == 0) {
        printf("Successfully connected using mf_connect().\n");
    } else {
        printf("Failed to connect using mf_connect().\n");
    }
    printf("-----------MF_CONNECT COMPLETED-----------\n\n");
    mf_print();
    // Test mf_create function
    printf("-----------MF_CREATE TEST-----------\n");
    status = mf_create("test_queue", 128);
    if (status == MF_SUCCESS) {
        printf("MF_CREATE: Message queue created successfully.\n");
    } else {
        printf("MF_CREATE: Failed to create message queue.\n");
    }
    printf("-----------MF_CREATE COMPLETED-----------\n\n");



    printf("-----------MF_DISCONNECT TEST-----------\n");
    status = mf_disconnect();
    printf("%u\n",status);
    if (status == 0) {
        printf("Successfully disconnected using mf_disconnect().\n");
    } else {
        printf("Failed to disconnect using mf_disconnect().\n");
    }
    printf("-----------MF_DISCONNECT COMPLETED-----------\n\n");



    // Clean up resources before exiting
    printf("-----------MF DESTROY TEST-----------\n");
    mf_destroy();
    printf("-----------MF_DESTROY COMPLETED-----------\n\n");
    return 0;
}

