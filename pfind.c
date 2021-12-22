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

// Directory node for dirQueue nodes
struct directoryNode {
    char *dirPath;
    // TODO check if PATH_MAX is needed
    struct directoryNode *nextDir;
};

// Thread Object for threadsArr
struct threadObj {
    int threadIndex;
    pthread_t thread;
    char *dirPath;
};

// Thread node for threadsQueue nodes
struct threadNode {
    int threadIndex;
    struct threadNode *nextThreadNode;
};

// Directories Queue
struct dirQueue {
    struct directoryNode *head;
    struct directoryNode *tail;
};

// Threads Queue
struct threadsQueue {
    struct threadNode *head; // index of thread that went to sleep first
    struct threadNode *tail; // index of thread that went to sleep last
    int numOfSleepingThreads;
};

pthread_mutex_t slock, dqlock, tqlock, elock, stlock;
pthread_cond_t startCV, endCV;
pthread_cond_t *cvs;
atomic_int fileCounter = 0, numOfSleepingThreads = 0;
int numOfThreads = 0, startFlag = 0, endFlag = 0, allGoodFlag = 0; returnVal;
struct dirQueue *dirQueue;
struct threadsQueue *threadsQueue;
struct threadObj **threadsArr;
char *searchTerm;

void threadError(int threadIndex) {
    numOfThreads--;
    perror("Error in thread, exiting thread");
    pthread_exit(NULL);
}

void threadEnqueue(int threadIndex) {
    // Creating threadNode object
    struct threadNode *threadNode = (struct threadNode *)calloc(1, sizeof(struct threadNode));
    if (threadNode == NULL) {
        threadError(threadIndex);
    }
    threadNode->threadIndex = threadIndex;
    
    pthread_mutex_lock(&tqlock);
    printf("***%d locked tqlock in threadEnqueue\n", threadIndex);
    
    // Adding threadNode to queue
    if (threadsQueue->head == NULL) {
        threadsQueue->head = threadNode;
        threadsQueue->tail = threadNode;
    }
    else {
        threadsQueue->tail->nextThreadNode = threadNode;
        threadsQueue->tail = threadNode;
    }
    numOfSleepingThreads++;
    printf("***%d added thread %d to tQueue. sleeping count is: %d\n", threadIndex, threadIndex, numOfSleepingThreads);
    pthread_mutex_unlock(&tqlock);

    // Checking if we finished and signaling main if so
    if (numOfSleepingThreads == numOfThreads) {
        printf("***%d waking main for end\n", threadIndex);
        endFlag = 1;
        allGoodFlag = 1;
        for (int i = 0; i < numOfThreads; i++) {
            if (i == threadIndex) {
                continue;
            }
            pthread_cond_broadcast(&cvs[i]);
        }
        pthread_exit(NULL);
    }

    pthread_mutex_lock(&tqlock);
    pthread_cond_wait(&cvs[threadIndex], &tqlock);
    printf("***%d woke up\n", threadIndex);
    pthread_mutex_unlock(&tqlock);
    printf("***%d unlocked tqlock in threadEnqueue\n", threadIndex);

    // Time to finish
    if (endFlag == 1) 
    {
        printf("***%d finished\n", threadIndex);
        pthread_exit(NULL);
    }    
}

struct threadNode* threadDequeue(int threadIndex) {
    struct threadNode* firstThreadInQueue = NULL;
    // Not using locks because the call for this function 
    // is inside a critical code of tqlock already

    // Remove first thread from queue
    firstThreadInQueue = threadsQueue->head;
    threadsQueue->head = firstThreadInQueue->nextThreadNode;
    firstThreadInQueue->nextThreadNode = NULL;
    numOfSleepingThreads--;

    printf("***%d got thread %d out of tQueue. sleeping count is: %d\n", threadIndex, firstThreadInQueue->threadIndex, numOfSleepingThreads);
    return firstThreadInQueue;
}

void dirEnqueue(int threadIndex, char *path, char *name) {
    // Creating dir full path
    char *dirPath = (char *)calloc(1, PATH_MAX);
    if (dirPath == NULL) {
        threadError(threadIndex);
    }
    sprintf(dirPath, "%s/%s", path, name);

    pthread_mutex_lock(&tqlock);
    printf("***%d locked tlock in dirEnqueue\n", threadIndex);
    // If there is a sleeping thread, assign dir to it and wake it up
    if (threadsQueue->head != NULL) {
        struct threadNode* threadToWake = threadDequeue(threadIndex);
        pthread_mutex_unlock(&tqlock);
        printf("***%d unlocked tlock in dirEnqueue\n", threadIndex);
        int indexToWake = threadToWake->threadIndex;
        (threadsArr[indexToWake])->dirPath = dirPath;
        printf("***%d assigned path %s to thread %d\n", threadIndex, dirPath, indexToWake);
        printf("***%d signaling thread %d to wake up\n", threadIndex, indexToWake);
        pthread_cond_signal(&cvs[indexToWake]);
    }
    // If there is no sleeping threads, adding dir to queue
    else {
        pthread_mutex_unlock(&tqlock);
        printf("***%d unlocked tlock in dirEnqueue\n", threadIndex);
        // Creating directory node
        struct directoryNode* D = (struct directoryNode *)calloc(1, sizeof(struct directoryNode));
        if (D == NULL) {
            threadError(threadIndex);            
        }
        D->dirPath = dirPath;
        printf("***%d in dirEnqueue, adding path %s\n", threadIndex, D->dirPath);
        pthread_mutex_lock(&dqlock);
        printf("***%d locked dlock in dirEnqueue\n", threadIndex);
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
        printf("***%d unlocked dlock in dirEnqueue\n", threadIndex);
    }
}

struct directoryNode* dirDequeue(int threadIndex) {
    struct directoryNode *firstDirInQueue = NULL;
    // pthread_mutex_lock(&dqlock);
    // printf("***%d locked dlock in dirDequeue\n", threadIndex);
    
    // Not using locks because the call for this function 
    // is inside a critical code of dqlock already

    // Remove first dir from queue
    firstDirInQueue = dirQueue->head;
    dirQueue->head = firstDirInQueue->nextDir;
    firstDirInQueue->nextDir = NULL;
    // pthread_mutex_unlock(&dqlock);
    // printf("***%d unlocked dlock in dirDequeue\n", threadIndex);

    printf("***%d in dirDequeue, got path %s\n", threadIndex, firstDirInQueue->dirPath);
    return firstDirInQueue;
}

void searchTermInDir(int threadIndex, char *dirPath) {
    DIR *dir;
    struct dirent *entry;
    // If directory can be searched, read entries
    if ((dir = opendir(dirPath)) != NULL) {
        // printf("thread number %d is searching dir %s\n", threadIndex, d->dirPath);
        while ((entry = readdir(dir)) != NULL) {
            char *entryName = entry->d_name;
            //printf("thread number %d is searching folder %s entry %s\n", threadIndex, d->dirPath, entryName);
            // Skipping . and .. entries
            if ((strcmp(entryName, ".") == 0) || (strcmp(entryName, "..") == 0)) {
                continue;
            }

            // Assabsle entry's full path
            char *entryPath = (char *)calloc(1, PATH_MAX);
            if (entryPath == NULL) {
                threadError(threadIndex);
            }
            sprintf(entryPath, "%s/%s", dirPath, entryName);
            
            // Checking entry type
            // TODO change back to stat
            struct stat entryStat;
            returnVal = lstat(entryPath, &entryStat);
            if  (returnVal != 0) {
                threadError(threadIndex);
            }

            // If it's a directory
            if (S_ISDIR(entryStat.st_mode)) {
            // If it's a directory, add it to dirQueue (or assign it to sleeping thread)
            // TODO check about links
            //if (entry->d_type == DT_DIR) {                      
                dirEnqueue(threadIndex, dirPath, entryName);
            }

            // If it's not a directory
            else {
                // If entry name contains search term, found one
                if (strstr(entryName, searchTerm) != NULL) {
                    // TODO make sure it's a full path
                    printf("%s/%s\n", dirPath, entryName);
                    fileCounter++;
                }
            }
        }
        // Finished dir
        printf("***%d finished dir %s\n", threadIndex, dirPath);
        returnVal = closedir(dir);
        if (returnVal != 0) {
            threadError(threadIndex);
        }
    }
    // Directory can not be searched
    else {
        printf("Directory %s: Permission denied.\n", dirPath);
    }
}

void *searchTermFunc(void *threadObj) {
    char *dirPath;
    int threadIndex = ((struct threadObj *)threadObj)->threadIndex;

    // Waiting for signal from main thread
    pthread_mutex_lock(&slock);
    // printf("***%d locked slock in searchTermFunc\n", threadIndex);
    while (startFlag == 0) {
        pthread_cond_wait(&startCV, &slock);
    }
    pthread_mutex_unlock(&slock);
    // printf("***%d unlocked slock in searchTermFunc\n", threadIndex);
    // printf("%d is starting\n", threadIndex);
    
    // In practice, will stop when program is finished
    while (1) {
        // If directory is assigned, search in it
        if ((threadsArr[threadIndex]->dirPath) != NULL) {
            dirPath = threadsArr[threadIndex]->dirPath;
            threadsArr[threadIndex]->dirPath = NULL;
            searchTermInDir(threadIndex, dirPath);
        }

        // At this point, assigned directory was searched (if existed)
        // Taking next directory from dirQueue
        pthread_mutex_lock(&dqlock);
        printf("***%d locked dqlock in searchTermFunc\n", threadIndex);
        while ((dirQueue->head) != NULL) {
            dirPath = (dirDequeue(threadIndex))->dirPath;
            pthread_mutex_unlock(&dqlock);
            printf("***%d unlocked dqlock in searchTermFunc\n", threadIndex);
            searchTermInDir(threadIndex, dirPath);
            pthread_mutex_lock(&dqlock);
            printf("***%d locked dqlock in searchTermFunc\n", threadIndex);
        }
        pthread_mutex_unlock(&dqlock);
        printf("***%d unlocked dqlock in searchTermFunc\n", threadIndex);
        
        // dirQueue is empty, going to sleep
        threadEnqueue(threadIndex);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    int i = 0; //numOfValidThreads;
    char *rootDirPath;

    // Checking number of arguments
    if (argc != 4) {
        perror("Number of cmd args is not 3");
	    exit(1);
    }

    // Parsing arguments
    rootDirPath = argv[1];
    searchTerm = argv[2];
    sscanf(argv[3],"%d",&numOfThreads);
    // numOfValidThreads = numOfThreads;
    threadsArr = (struct threadObj **)calloc(numOfThreads, sizeof(struct threadObj *));
    if (threadsArr == NULL) {
        perror("Can't allocate memory for threads array");
	    exit(1);
    }
    cvs = (pthread_cond_t *)calloc(numOfThreads, sizeof(pthread_cond_t));
    if (cvs == NULL) {
        perror("Can't allocate memory for cvs array");
	    exit(1);
    }

    // Creating directory queue
    dirQueue = (struct dirQueue*)calloc(1, sizeof(struct dirQueue));
    if (dirQueue == NULL) {
        perror("Can't allocate memory for directory queue");
	    exit(1);
    }
    // Creating threads sleeping queue
    threadsQueue = (struct threadsQueue*)calloc(1, sizeof(struct threadsQueue));
    if (threadsQueue == NULL) {
        perror("Can't allocate memory for threads queue");
	    exit(1);
    }

    // Checking that search root directory can be searched
    if (opendir(rootDirPath) == NULL) {
        perror("Can't search in root directory");
        exit(1);
    }

    // Creating root directory and adding it to dirQueue
    struct directoryNode* D = (struct directoryNode *)calloc(1, sizeof(struct directoryNode));
    if (D == NULL) {
        perror("Can't allocate memory for root directory node struct");
	    exit(1);
    }
    D->dirPath = rootDirPath;
    dirQueue->head = D;
    dirQueue->tail = D;

    // Initializing start lock, end lock, dirQueue lock, threadsQueue lock and sleepingThread lock
    pthread_mutex_init(&slock, NULL);
    pthread_mutex_init(&elock, NULL);
    pthread_mutex_init(&dqlock, NULL);
    pthread_mutex_init(&tqlock, NULL);
    pthread_mutex_init(&stlock, NULL);
    
    // Creating start and end CV and locking their locks
    pthread_cond_init(&startCV, NULL);
    pthread_mutex_lock(&slock);
    printf("***main locked slock in main\n");
    pthread_cond_init(&endCV, NULL);
    pthread_mutex_lock(&elock);
    printf("***main locked elock in main\n");

    // Creating threads and cvs
    for (i = 0; i < numOfThreads; i++) {
        // Creating thread Object
        struct threadObj *threadObj = (struct threadObj *)calloc(1, sizeof(struct threadObj));
        if (threadObj == NULL) {
            perror("Can't allocate memory for thread object");
	        exit(1);
        }
        threadsArr[i] = threadObj;
        threadObj->threadIndex = i;
        // Creating thread and assign it to search function
        returnVal = pthread_create(&(threadsArr[i]->thread), NULL, searchTermFunc, (void *)threadObj);
        if (returnVal) {
            perror("Can't create thread");
            exit(1);
        }
        // Creating relevant CV
        pthread_cond_init(&cvs[i], NULL);
    }

    // Signaling the threads to start
    startFlag = 1;
    pthread_cond_broadcast(&startCV);
    pthread_mutex_unlock(&slock);
    printf("***main unlocked slock in main\n");

    // Waiting for all threads to exit
    for (i = 0; i < numOfThreads; i++) {
        returnVal = pthread_join(threadsArr[i]->thread, NULL);
        if (returnVal != 0) {
            perror("error in joining main to threads");
            printf("Done searching, found %d files\n", fileCounter);
            exit(1);
        }
    }

    // while (endFlag == 0) {
    //     pthread_cond_wait(&endCV, &elock);
    // }

    // Finished due to an error in all threads
    if (allGoodFlag == 0) {
        perror("Error in all threads");
        printf("Done searching, found %d files\n", fileCounter);
        exit(1);
    }

    // Destroying locks
    pthread_mutex_destroy(&slock);
    pthread_mutex_destroy(&tqlock);
    pthread_mutex_destroy(&dqlock);
    pthread_mutex_destroy(&elock);
    pthread_mutex_destroy(&stlock);

    // Destroying cvs
    for (i = 0; i < numOfThreads; i++) {
        pthread_cond_destroy(&cvs[i]);
    }

    // Printing message
    printf("Done searching, found %d files\n", fileCounter);
    exit(0);
}