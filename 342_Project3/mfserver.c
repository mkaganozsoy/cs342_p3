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
    mf_init();
    // do some initialization if needed
    while (1) {
        sleep(1000);
    }
    exit(0);
}
