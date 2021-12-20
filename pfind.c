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
#include <unistd.h>

struct directoryNode {
    char *dirPath;
    // TODO check if PATH_MAX is needed
    struct directoryNode *nextDir;
};

// 
struct threadObj {
    int threadIndex;
    pthread_t thread;
    char *dirPath;
};

struct threadNode {
    int threadIndex;
    struct threadNode *nextThreadNode;
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

pthread_mutex_t slock, dqlock, tqlock, elock;
pthread_cond_t startCV, endCV;
pthread_cond_t *cvs;
atomic_int fileCounter = 0, numOfSleepingThreads = 0;
int numOfThreads = 0, startFlag = 0, endFlag = 0;
struct dirQueue *dirQueue;
struct threadsQueue *threadsQueue;
struct threadObj **threadsArr;
char *searchTerm;

void threadEnqueue(int threadIndex) {
    struct threadNode *threadNode = (struct threadNode *)calloc(1, sizeof(struct threadNode));
    threadNode->threadIndex = threadIndex;
    
    pthread_mutex_lock(&tqlock);
    // printf("%d locked tlock\n", threadIndex);
    
    // Adding thread Node to queue
    if (threadsQueue->head == NULL) {
        threadsQueue->head = threadNode;
        threadsQueue->tail = threadNode;
    }
    else {
        threadsQueue->tail->nextThreadNode = threadNode;
        threadsQueue->tail = threadNode;
    }
    numOfSleepingThreads++;
    printf("%d added thread %d to tQueue. sleeping count is: %d\n", threadIndex, threadIndex, numOfSleepingThreads);
    if ((numOfSleepingThreads == numOfThreads) && ((dirQueue->head) == NULL)) {
        printf("%d waking main for end\n", threadIndex);
        endFlag = 1;
        // printf("%d locked elock\n", threadIndex);
        pthread_cond_signal(&endCV);
        // printf("%d unlocked elock\n", threadIndex);
    }
    pthread_mutex_unlock(&tqlock);
    // printf("%d unlocked tlock\n", threadIndex);
}

struct threadNode* threadDequeue(int threadIndex) {
    struct threadNode* firstThreadInQueue = NULL;
    pthread_mutex_lock(&tqlock);
    // printf("%d locked tlock\n", threadIndex);

    // Remove first thread from queue
    firstThreadInQueue = threadsQueue->head;
    threadsQueue->head = firstThreadInQueue->nextThreadNode;
    firstThreadInQueue->nextThreadNode = NULL;
    numOfSleepingThreads--;

    pthread_mutex_unlock(&tqlock);
    // printf("%d unlocked tlock\n", threadIndex);
    printf("%d got thread %d out of tQueue. sleeping count is: %d\n", threadIndex, firstThreadInQueue->threadIndex, numOfSleepingThreads);
    return firstThreadInQueue;
}

void dirEnqueue(int threadIndex, char *path, char *name) {
    char *dirPath = (char *)calloc(1, PATH_MAX);
    strcat(dirPath, path);
    strcat(dirPath, "/");
    strcat(dirPath, name);
    pthread_mutex_lock(&tqlock);
    if (threadsQueue->head != NULL) {
        struct threadNode* threadToWake = threadDequeue(threadIndex);
        pthread_mutex_unlock(&tqlock);
        int indexToWake = threadToWake->threadIndex;
        struct threadObj *thread = threadsArr[indexToWake];
        thread->dirPath = dirPath;
        printf("assigning path %s to thread %d\n", dirPath, threadIndex);
        printf("%d signaling thread to wake %d\n", threadIndex, indexToWake);
        pthread_cond_signal(&cvs[indexToWake]);
    }
    else {
        // Creating directory node
        struct directoryNode* D = (struct directoryNode *)calloc(1, sizeof(struct directoryNode));
        D->dirPath = dirPath;
        printf("%d in dirEnqueue, adding path %s\n", threadIndex, D->dirPath);
        pthread_mutex_lock(&dqlock);
        // printf("%d locked dlock\n", threadIndex);
        // Adding dir to queue
        if (dirQueue->head == NULL) {
            dirQueue->head = D;
            dirQueue->tail = D;
        }
        else {
            dirQueue->tail->nextDir = D;
            dirQueue->tail = D;
        }
        // printf("%d about to wake a thread\n", threadIndex);
        // if ((threadsQueue->head) != NULL) {
        //     int indexToWake = threadsQueue->head->threadIndex;
        //     printf("%d signaling thread %d\n", threadIndex, indexToWake);
        //     threadDequeue(threadIndex);
        //     pthread_cond_signal(&cvs[indexToWake]);
        // }
        pthread_mutex_unlock(&dqlock);
        // printf("%d unlocked dlock\n", threadIndex);
    }
}

struct directoryNode* dirDequeue(int threadIndex) {
    struct directoryNode *firstDirInQueue = NULL;
    pthread_mutex_lock(&dqlock);
    // printf("%d locked dlock\n", threadIndex);
    // while queue is empty
    if ((dirQueue->head) == NULL) {
        threadEnqueue(threadIndex);
    }
    while ((dirQueue->head) == NULL) {
        pthread_cond_wait(&cvs[threadIndex], &dqlock);
        printf("%d woke up\n", threadIndex);
    }
    // Remove first dir from queue
    firstDirInQueue = dirQueue->head;
    dirQueue->head = firstDirInQueue->nextDir;
    firstDirInQueue->nextDir = NULL;
    pthread_mutex_unlock(&dqlock);
    // printf("%d unlocked dlock\n", threadIndex);
    printf("%d in dirDequeue, got path %s\n", threadIndex, firstDirInQueue->dirPath);
    return firstDirInQueue;
}

void *searchTermInDir(void *threadObj) {
    DIR *dir;
    struct dirent *entry;
    char *dirPath;
    int threadIndex = ((struct threadObj *)threadObj)->threadIndex;
    printf("%d in function\n", threadIndex);
    // Waiting for signal from main thread
    pthread_mutex_lock(&slock);
    while (startFlag == 0) {
        printf("thread %d is waiting\n", threadIndex);
        pthread_cond_wait(&startCV, &slock);
    }
    pthread_mutex_unlock(&slock);
    printf("%d is starting\n", threadIndex);
    
    while (1) {
        // Take head directory from queue (including waiting to be not empty)
        if ((threadsArr[threadIndex]->dirPath) != NULL) {
            dirPath = threadsArr[threadIndex]->dirPath;
            threadsArr[threadIndex]->dirPath = NULL;
        }
        else {
            struct directoryNode *d = dirDequeue(threadIndex);
            dirPath = d->dirPath;
        }
        if ((dir = opendir(dirPath)) != NULL) {
            // printf("thread number %d is searching dir %s\n", threadIndex, d->dirPath);
            while ((entry = readdir(dir)) != NULL) {
                char *entryName = entry->d_name;
                //printf("thread number %d is searching folder %s entry %s\n", threadIndex, d->dirPath, entryName);
                if ((strcmp(entryName, ".") == 0) || (strcmp(entryName, "..") == 0)) {
                    continue;
                }

                // If it's a directory
                if (entry->d_type == DT_DIR) {                      
                    dirEnqueue(threadIndex, dirPath, entryName);
                }

                // If it's not a directory
                else {
                    // Entry name contains search term
                    if (strstr(entryName, searchTerm) != NULL) {
                        // TODO make sure it's a full path
                        printf("%s/%s\n", dirPath, entryName);
                        fileCounter++;
                    }
                }
            }
            printf("%d finished dir %s\n", threadIndex, dirPath);
            closedir(dir);
        }
        else {
            printf("Directory %s: Permission denied.\n", dirPath);
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    int returnVal, i = 0; //numOfValidThreads;
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
    threadsArr = (struct threadObj **)calloc(numOfThreads, sizeof(struct threadObj *));
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

    // Initializing start lock, and lock, dirQueue lock and threadsQueue lock
    // printf("init locks\n");
    returnVal = pthread_mutex_init(&slock, NULL);
    if (returnVal) {
        perror("Can't initialize slock mutex");
        exit(1);
    }
    returnVal = pthread_mutex_init(&elock, NULL);
    if (returnVal) {
        perror("Can't initialize elock mutex");
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
    
    // Creating start and end CV
    pthread_cond_init(&startCV, NULL);
    pthread_cond_init(&endCV, NULL);
    pthread_mutex_lock(&slock);
    pthread_mutex_lock(&elock);

    // Creating threads and cvs
    // printf("Creating threads and cvs\n");
    for (i = 0; i < numOfThreads; i++) {
        struct threadObj *threadObj = (struct threadObj *)calloc(1, sizeof(struct threadObj));
        threadsArr[i] = threadObj;
        threadObj->threadIndex = i;
        printf("index is %d\n", i);
        returnVal = pthread_create(&(threadsArr[i]->thread), NULL, searchTermInDir, (void *)threadObj);
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
    // TODO delete
    sleep(2);
    startFlag = 1;
    printf("flag is 1\n");
    pthread_cond_broadcast(&startCV);
    pthread_mutex_unlock(&slock);

    while (endFlag == 0) {
        printf("waiting for end\n");
        pthread_cond_wait(&endCV, &elock);
    }

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