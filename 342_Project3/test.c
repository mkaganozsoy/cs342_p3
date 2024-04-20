#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include "mf.h"
#define COUNT 5
char *semname1 = "/semaphore1";
char *semname2 = "/semaphore2";
sem_t *sem1, *sem2;
char *mqname1 = "/msgqueue1";
int
main(int argc, char **argv) {
    int ret, i, qid;
    sem1 = sem_open(semname1, O_CREAT, 0666, 0); // init sem
    sem2 = sem_open(semname2, O_CREAT, 0666, 0); // init sem
    ret = fork();
    if (ret > 0) {
// parent process - P1
        char *bufptr = (char *) malloc(MAX_DATALEN);
        sem1 = sem_open(semname1, 0);
        sem2 = sem_open(semname2, 0);
        mf_connect();
        mf_create(mqname1, 16); // create mq; 16KB
        qid = mf_open(mqname1);
        sem_post(sem1);

        for (i = 0; i < COUNT; ++i) {
            sprintf(bufptr, "%s-%d", "MessageData", i);
            mf_send(qid, (void *) bufptr, strlen(bufptr) + 1);
        }
        free(bufptr);
        mf_close(qid);
        mf_disconnect();
        sem_wait(sem2);
        mf_remove(mqname1); // remove mq
    } else if (ret == 0) {
        // child process - P2
        char *bufptr = (char *) malloc(MAX_DATALEN);
        sem1 = sem_open(semname1, 0);
        sem2 = sem_open(semname2, 0);
        sem_wait(sem1);
        mf_connect();
        qid = mf_open(mqname1);
        for (i = 0; i < COUNT; ++i) {
            mf_recv(qid, (void *) bufptr, MAX_DATALEN);
            printf("%s\n", bufptr);
        }
        free(bufptr);
        mf_close(qid);
        mf_disconnect();
        sem_post(sem2);
    }
    return (0);

}

