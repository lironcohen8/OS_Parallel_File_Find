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
atomic_int counter;
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

void dirEnqueue(struct directoryNode *dir) {
    pthread_mutex_lock(&dqlock);
    printf("in dirEnqueue, adding path %s\n", dir->dirPath);
    // Adding dir to queue
    if (dirQueue->head == NULL) {
        dirQueue->head = dir;
        dirQueue->tail = dir;
        if ((threadsQueue->head) != NULL) {
            int indexToWake = threadsQueue->head->threadIndex;
            pthread_cond_signal(&cvs[indexToWake]);
            threadsQueue->numOfSleepingThreads--;
            threadsQueue->head = threadsQueue->head->nextThread;
        }
    }
    else {
        dirQueue->tail->nextDir = dir;
        dirQueue->tail = dir;
    }
    
    pthread_mutex_unlock(&dqlock);
}

struct directoryNode* dirDequeue(int threadIndex) {
    struct directoryNode * firstDirInQueue = NULL;
    printf("thread %d in dirDequeue\n", threadIndex);
    pthread_mutex_lock(&dqlock);
    printf("thread %d in critical dirDequeue\n", threadIndex);
    // while queue is empty
    if ((dirQueue->head)==NULL) {// dirQueue is empty
    printf("in if, thread is %d\n", threadIndex);
        threadEnqueue(threadIndex);
        while (pthread_cond_wait(&cvs[threadIndex], &dqlock)) {
        }
    }
    // Remove first dir from queue
    firstDirInQueue = dirQueue->head;
    dirQueue->head = firstDirInQueue->nextDir;
    firstDirInQueue->nextDir = NULL;
    pthread_mutex_unlock(&dqlock);
    printf("thread %d out of critical dirDequeue\n", threadIndex);
    return firstDirInQueue;
}

void *searchTermInDir(void *i) {
    printf("in function, thread %d\n", *((int *) i));
    // Waiting for signal from main thread
    // pthread_cond_wait(&startCV, &slock);
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
                printf("thread number %d is searching entry name %s\n", threadIndex, entryName);
                if ((strcmp(entryName, ".") == 0) || (strcmp(entryName, "..") == 0)) {
                    printf("in continue, and entry name is %s\n", entryName);
                    continue;
                }

                // Checking entry type
                struct stat entryStat;
                stat(d->dirPath, &entryStat);

                // If it's a directory
                if (entry->d_type == DT_DIR) {
                    printf("thread number %d found that %s is a dir\n", threadIndex, entryName);
                    // Checking if directory can't be searched
                    if ((dir = opendir(d->dirPath)) == NULL) {
                        // TODO make sure it's a full path
                        printf("Directory %s: Permission denied.\n", d->dirPath);
                        continue;
                    }
                    // Directory can be searched
                    else {
                        dirEnqueue(d);
                    }
                }

                // If it's not a directory
                else {
                    printf("thread number %d found that %s is not a dir\n", threadIndex, entryName);
                    // Entry name contains search term
                    if (strstr(entryName, searchTerm) != NULL) {
                        // TODO make sure it's a full path
                        printf("%s\n", d->dirPath);
                        counter++;
                    }
                }
            }
            closedir(dir);
        }
        threadEnqueue(threadIndex);
        threadsQueue->numOfSleepingThreads++;
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
    printf("parsing\n");
    rootDirPath = argv[1];
    searchTerm = argv[2];
    sscanf(argv[3],"%d",&numOfThreads);
    // numOfValidThreads = numOfThreads;
    threadsArr = (pthread_t *)calloc(numOfThreads, sizeof(pthread_t));
    cvs = (pthread_cond_t *)calloc(numOfThreads, sizeof(pthread_cond_t));

    // Creating directory queue
    printf("creating dQueue\n");
    dirQueue = (struct dirQueue*)calloc(1, sizeof(struct dirQueue));
    
    // Creating threads sleeping queue
    printf("creating tQueue\n");
    threadsQueue = (struct threadsQueue*)calloc(1, sizeof(struct threadsQueue));

    // Checking that search root directory can be searched
    printf("trying to open root\n");
    if (opendir(rootDirPath) == NULL) {
        perror("Can't search in root directory");
        exit(1);
    }

    // Creating root directory node and put it in queue
    printf("Creating root directory and put it in queue\n");
    struct directoryNode* D = (struct directoryNode *)calloc(1, sizeof(struct directoryNode));
    if (D == NULL) {
        perror("Can't allocate memory for root directory struct");
        exit(1);
    }
    D->dirPath = rootDirPath;
    printf("before dir enqueue\n");
    dirEnqueue(D);

    // Initializing start lock, dirQueue lock and threadsQueue lock
    printf("init locks\n");
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

    // Creating threads and cvs
    printf("Creating threads and cvs\n");
    for (i = 0; i < numOfThreads; ++i) {
        returnVal = pthread_create(&threadsArr[i], NULL, searchTermInDir, (void *)&i);
        if (returnVal) {
            perror("Can't create thread");
            exit(1);
        }
        pthread_cond_init(&cvs[i], NULL);
    }

    printf("start\n");

    // Signaling the threads to start
    pthread_cond_broadcast(&startCV);

    // while (threadsQueue->numOfSleepingThreads < numOfValidThreads) {
    // }

    // Destroying locks
    pthread_mutex_destroy(&slock);
    pthread_mutex_destroy(&tqlock);
    pthread_mutex_destroy(&dqlock);

    // Destroying cvs
    for (i = 0; i < numOfThreads; ++i) {
        pthread_cond_destroy(&cvs[i]);
    }

    // Printing message
    printf("Done searching, found %d files\n", counter);

    exit(0);
}