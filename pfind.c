#define _GNU_SOURCE

#include <sys/types.h>
#include <dirent.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <limits.h>

struct directoryNode {
    char *dirPath;
    // TODO check if PATH_MAX is needed
    struct directoryNode *nextDir;
};

struct threadNode {
    int threadIndex;
    struct threadNode *nextThread;
};

struct dirQueue {
    struct directoryNode *head;
    struct directoryNode *tail;
};

struct threadsQueue {
    struct threadNode *head; // index of thread that went to sleep first
    struct threadNode *tail; // index of thread that went to sleep last
    int numOfSleepingThreads;
};

pthread_mutex_t slock, dqlock, tqlock;
pthread_cond_t startCV;
pthread_cond_t *cvs;
atomic_int fileCounter;
int startFlag = 0;
struct dirQueue *dirQueue;
struct threadsQueue *threadsQueue;
char *searchTerm;

void threadEnqueue(int index) {
    struct threadNode *threadNode = (struct threadNode *)calloc(1, sizeof(struct threadNode));
    threadNode->threadIndex = index;
    
    pthread_mutex_lock(&tqlock);
    printf("added thread %d to tQueue\n", index);
    // Adding thread Node to queue
    if (threadsQueue->head == NULL) {
        threadsQueue->head = threadNode;
        threadsQueue->tail = threadNode;
    }
    else {
        threadsQueue->tail->nextThread = threadNode;
        threadsQueue->tail = threadNode;
    }
    pthread_mutex_unlock(&tqlock);
}

struct threadNode* threadDequeue() {
    struct threadNode* firstThreadInQueue = NULL;
    pthread_mutex_lock(&tqlock);

    // Remove first thread from queue
    firstThreadInQueue = threadsQueue->head;
    threadsQueue->head = firstThreadInQueue->nextThread;
    firstThreadInQueue->nextThread = NULL;

    pthread_mutex_unlock(&tqlock);
    printf("got thread %d out of tQueue", firstThreadInQueue->threadIndex);
    return firstThreadInQueue;
}

void dirEnqueue(char *path, char *name) {
    pthread_mutex_lock(&dqlock);
    // Creating directory node
    struct directoryNode* D = (struct directoryNode *)calloc(1, sizeof(struct directoryNode));
    D->dirPath = (char *)calloc(1, PATH_MAX);
    strcat(D->dirPath, path);
    strcat(D->dirPath, "/");
    strcat(D->dirPath, name);
    printf("in dirEnqueue, adding path %s\n", D->dirPath);
    // Adding dir to queue
    if (dirQueue->head == NULL) {
        dirQueue->head = D;
        dirQueue->tail = D;
    }
    else {
        dirQueue->tail->nextDir = D;
        dirQueue->tail = D;
    }
    if ((threadsQueue->head) != NULL) {
        int indexToWake = threadsQueue->head->threadIndex;
        printf("signaling thread %d\n", indexToWake);
        pthread_cond_signal(&cvs[indexToWake]);
        threadsQueue->numOfSleepingThreads--;
        threadsQueue->head = threadsQueue->head->nextThread;
    }
    pthread_mutex_unlock(&dqlock);
}

struct directoryNode* dirDequeue(int threadIndex) {
    struct directoryNode * firstDirInQueue = NULL;
    pthread_mutex_lock(&dqlock);
    // while queue is empty
    if ((dirQueue->head)==NULL) {
        threadEnqueue(threadIndex);
        while (pthread_cond_wait(&cvs[threadIndex], &dqlock)) {
        }
    }
    // Remove first dir from queue
    firstDirInQueue = dirQueue->head;
    dirQueue->head = firstDirInQueue->nextDir;
    firstDirInQueue->nextDir = NULL;
    pthread_mutex_unlock(&dqlock);
    printf("in dirDequeue, got path %s\n", firstDirInQueue->dirPath);
    return firstDirInQueue;
}

void *searchTermInDir(void *i) {
    printf("in function, thread %d\n", *((int *) i));
    // Waiting for signal from main thread
    while (startFlag == 0) {
        printf("thread %d is waiting\n",  *((int *) i));
        pthread_cond_wait(&startCV, &slock);
    }
    printf("thread %d is starting\n",  *((int *) i));
    int threadIndex = *((int *) i);

    while (1) {
        while ((dirQueue->head) != NULL) {
            // Take head directory from queue (including waiting to be not empty)
            struct directoryNode *d = dirDequeue(threadIndex);
            struct dirent *entry;
            DIR *dir = opendir(d->dirPath); 
            printf("thread number %d is searching dir %s\n", threadIndex, d->dirPath);
            while ((entry = readdir(dir)) != NULL) {
                char *entryName = entry->d_name;
                printf("thread number %d is searching folder %s entry %s\n", threadIndex, d->dirPath, entryName);
                if ((strcmp(entryName, ".") == 0) || (strcmp(entryName, "..") == 0)) {
                    continue;
                }

                // If it's a directory
                if (entry->d_type == DT_DIR) {
                    // Checking if directory can't be searched
                    if (opendir(d->dirPath) == NULL) {
                        // TODO make sure it's a full path
                        printf("Directory %s: Permission denied.\n", d->dirPath);
                        continue;
                    }
                    // Directory can be searched
                    else {                        
                        dirEnqueue(d->dirPath, entryName);
                    }
                }

                // If it's not a directory
                else {
                    // Entry name contains search term
                    if (strstr(entryName, searchTerm) != NULL) {
                        // TODO make sure it's a full path
                        printf("%s/%s\n", d->dirPath, entryName);
                        fileCounter++;
                    }
                }
            }
            printf("finished dir %s\n", d->dirPath);
            closedir(dir);
        }
        threadEnqueue(threadIndex);
        pthread_mutex_lock(&dqlock);
        pthread_cond_wait(&cvs[threadIndex], &dqlock);
    }
}

int main(int argc, char *argv[]) {
    int numOfThreads, returnVal, i; //numOfValidThreads;
    pthread_t *threadsArr;
    char *rootDirPath;

    // Checking number of arguments
    if (argc != 4) {
        perror("Number of cmd args is not 3");
	    exit(1);
    }

    // Parsing arguments
    // printf("parsing\n");
    rootDirPath = argv[1];
    searchTerm = argv[2];
    sscanf(argv[3],"%d",&numOfThreads);
    // numOfValidThreads = numOfThreads;
    threadsArr = (pthread_t *)calloc(numOfThreads, sizeof(pthread_t));
    cvs = (pthread_cond_t *)calloc(numOfThreads, sizeof(pthread_cond_t));

    // Creating directory queue
    // printf("creating dQueue\n");
    dirQueue = (struct dirQueue*)calloc(1, sizeof(struct dirQueue));
    
    // Creating threads sleeping queue
    // printf("creating tQueue\n");
    threadsQueue = (struct threadsQueue*)calloc(1, sizeof(struct threadsQueue));

    // Checking that search root directory can be searched
    // printf("trying to open root\n");
    if (opendir(rootDirPath) == NULL) {
        perror("Can't search in root directory");
        exit(1);
    }

    // Adding root directory to dirQueue
    // printf("Creating root directory and put it in queue\n");
    // printf("before dir enqueue\n");
    struct directoryNode* D = (struct directoryNode *)calloc(1, sizeof(struct directoryNode));
    D->dirPath = rootDirPath;
    dirQueue->head = D;
    dirQueue->tail = D;

    // Initializing start lock, dirQueue lock and threadsQueue lock
    // printf("init locks\n");
    returnVal = pthread_mutex_init(&slock, NULL);
    if (returnVal) {
        perror("Can't initialize slock mutex");
        exit(1);
    }
    returnVal = pthread_mutex_init(&dqlock, NULL);
    if (returnVal) {
        perror("Can't initialize dqlock mutex");
        exit(1);
    }
    returnVal = pthread_mutex_init(&tqlock, NULL);
    if (returnVal) {
        perror("Can't initialize tqlock mutex");
        exit(1);
    }
    
    // Creating start CV
    pthread_cond_init(&startCV, NULL);
    pthread_mutex_lock(&slock);

    // Creating threads and cvs
    // printf("Creating threads and cvs\n");
    for (i = 0; i < numOfThreads; ++i) {
        returnVal = pthread_create(&threadsArr[i], NULL, searchTermInDir, (void *)&i);
        if (returnVal) {
            perror("Can't create thread");
            exit(1);
        }
        pthread_cond_init(&cvs[i], NULL);
    }

    // while (waitingForStartCounter < numOfThreads) {
    //     printf("nubmer of waiting is %d\n", waitingForStartCounter);
    // }

    // Signaling the threads to start
    startFlag = 1;
    printf("flag is 1\n");
    pthread_cond_broadcast(&startCV);

    // Destroying locks
    pthread_mutex_destroy(&slock);
    pthread_mutex_destroy(&tqlock);
    pthread_mutex_destroy(&dqlock);

    // Destroying cvs
    for (i = 0; i < numOfThreads; ++i) {
        pthread_cond_destroy(&cvs[i]);
    }

    // Printing message
    printf("Done searching, found %d files\n", fileCounter);

    exit(0);
}