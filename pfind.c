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
};

pthread_mutex_t slock, dqlock, tqlock, talock;
pthread_cond_t startCV, endCV;
pthread_cond_t *cvs;
atomic_int fileCounter = 0, numOfThreads = 0, numOfSleepingThreads = 0, numOfErroredThreads = 0;
int startFlag = 0, endFlag = 0, returnVal = 0;
struct dirQueue *dirQueue;
struct threadsQueue *threadsQueue;
struct threadObj **threadsArr;
char *searchTerm;

// Executing when there is an error in searching thread
void threadError(int threadIndex) {
    numOfThreads--;
    numOfErroredThreads++;
    perror("Error in thread, exiting thread");
    pthread_exit(NULL);
}

// Adding a thread to sleeping threads queue
void threadEnqueue(int threadIndex) {
    // Creating threadNode object
    struct threadNode *threadNode = (struct threadNode *)calloc(1, sizeof(struct threadNode));
    if (threadNode == NULL) {
        threadError(threadIndex);
    }
    threadNode->threadIndex = threadIndex;
    
    pthread_mutex_lock(&tqlock);
    
    // Adding threadNode to queue
    if (threadsQueue->head == NULL) {
        threadsQueue->head = threadNode;
        threadsQueue->tail = threadNode;
    }
    else {
        threadsQueue->tail->nextThreadNode = threadNode;
        threadsQueue->tail = threadNode;
    }

    // Checking if we finished and waking up all threads to exit
    if (numOfSleepingThreads+1 == numOfThreads) {
        endFlag = 1;
        for (int i = 0; i < numOfThreads; i++) {
            if (i == threadIndex) {
                continue;
            }
            pthread_cond_signal(&cvs[i]);
        }
        // Exiting this thread also
        pthread_mutex_unlock(&tqlock);
        pthread_exit(NULL);
    }
    
    // If we didn't finish, going to sleep
    numOfSleepingThreads++;
    pthread_cond_wait(&cvs[threadIndex], &tqlock);
    pthread_mutex_unlock(&tqlock);

    // Threads woke up and it's time to exit
    if (endFlag == 1) 
    {
        pthread_exit(NULL);
    }    
}

// Removing a thread from sleeping threads queue
struct threadNode* threadDequeue(int threadIndex) {
    struct threadNode* firstThreadInQueue = NULL;
    // Not using locks because the call for this function 
    // is inside a critical code of tqlock already

    // Remove first thread from queue
    firstThreadInQueue = threadsQueue->head;
    threadsQueue->head = firstThreadInQueue->nextThreadNode;
    firstThreadInQueue->nextThreadNode = NULL;
    numOfSleepingThreads--;

    return firstThreadInQueue;
}

// Adding a directory to directories queue (or assigning to thread)
void dirEnqueue(int threadIndex, char *dirPath) {
    pthread_mutex_lock(&tqlock);

    // If there is a sleeping thread, assign directory to it and wake it up
    if (threadsQueue->head != NULL) {
        struct threadNode* threadToWake = threadDequeue(threadIndex);
        pthread_mutex_unlock(&tqlock);
        int indexToWake = threadToWake->threadIndex;
        pthread_mutex_lock(&talock);
        (threadsArr[indexToWake])->dirPath = dirPath;
        pthread_mutex_unlock(&talock);
        pthread_cond_signal(&cvs[indexToWake]);
    }

    // If there is no sleeping threads, add dir to queue
    else {
        pthread_mutex_unlock(&tqlock);
        // Creating directory node
        struct directoryNode* D = (struct directoryNode *)calloc(1, sizeof(struct directoryNode));
        if (D == NULL) {
            threadError(threadIndex);            
        }
        D->dirPath = dirPath;

        pthread_mutex_lock(&dqlock);
        // Adding dir to queue
        if (dirQueue->head == NULL) {
            dirQueue->head = D;
            dirQueue->tail = D;
        }
        else {
            dirQueue->tail->nextDir = D;
            dirQueue->tail = D;
        }
        pthread_mutex_unlock(&dqlock);
    }
}

// Removing a directory from directories queue
struct directoryNode* dirDequeue(int threadIndex) {
    struct directoryNode *firstDirInQueue = NULL;   
    // Not using locks because the call for this function 
    // is inside a critical code of dqlock already

    // Remove first dir from queue
    firstDirInQueue = dirQueue->head;
    dirQueue->head = firstDirInQueue->nextDir;
    firstDirInQueue->nextDir = NULL;

    return firstDirInQueue;
}

// Searching methodology in a specific directory
void searchTermInDir(int threadIndex, char *dirPath) {
    DIR *dir;
    struct dirent *entry;

    // If directory can be searched, read entries
    if ((dir = opendir(dirPath)) != NULL) {
        while ((entry = readdir(dir)) != NULL) {
            char *entryName = entry->d_name;
            // Skipping "." and ".." entries
            if ((strcmp(entryName, ".") == 0) || (strcmp(entryName, "..") == 0)) {
                continue;
            }

            // Assamble entry's full path
            char *entryPath = (char *)calloc(1, PATH_MAX);
            if (entryPath == NULL) {
                threadError(threadIndex);
            }
            sprintf(entryPath, "%s/%s", dirPath, entryName);
            
            // Checking entry type
            struct stat entryStat;
            returnVal = stat(entryPath, &entryStat);
            if  (returnVal != 0) {
                threadError(threadIndex);
            }

            // If it's a directory
            if (S_ISDIR(entryStat.st_mode)) {
            // Add it to dirQueue or assign it to sleeping thread
                dirEnqueue(threadIndex, entryPath);
            }

            // If it's not a directory
            else {
                // If entry name contains search term, found one
                if (strstr(entryName, searchTerm) != NULL) {
                    printf("%s\n", entryPath);
                    fileCounter++;
                }
            }
        }
        // Finished dir
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

// Main function for searching threads
void *searchTermFunc(void *threadObj) {
    char *dirPath;
    int threadIndex = ((struct threadObj *)threadObj)->threadIndex;

    // Waiting for signal from main thread
    pthread_mutex_lock(&slock);
    while (startFlag == 0) {
        pthread_cond_wait(&startCV, &slock);
    }
    pthread_mutex_unlock(&slock);
    
    // In practice, will stop when program is finished
    while (1) {
        // If directory is assigned, search in it
        pthread_mutex_lock(&talock);
        if ((threadsArr[threadIndex]->dirPath) != NULL) {
            dirPath = threadsArr[threadIndex]->dirPath;
            threadsArr[threadIndex]->dirPath = NULL;
            pthread_mutex_unlock(&talock);
            searchTermInDir(threadIndex, dirPath);
        }
        else {
            pthread_mutex_unlock(&talock);
        }

        // At this point, assigned directory was searched (if existed)
        // Taking next directory from dirQueue
        pthread_mutex_lock(&dqlock);
        while ((dirQueue->head) != NULL) {
            dirPath = (dirDequeue(threadIndex))->dirPath;
            pthread_mutex_unlock(&dqlock);
            searchTermInDir(threadIndex, dirPath);
            pthread_mutex_lock(&dqlock);
        }
        pthread_mutex_unlock(&dqlock);
        
        // dirQueue is empty, going to sleep
        threadEnqueue(threadIndex);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    int i = 0; 

    // Checking number of arguments
    if (argc != 4) {
        perror("Number of cmd args is not 3");
	    exit(1);
    }

    // Parsing arguments
    char *rootDirPath = argv[1];
    searchTerm = argv[2];
    sscanf(argv[3],"%d",&numOfThreads);

    // Checking that search root directory can be searched
    if (opendir(rootDirPath) == NULL) {
        perror("Can't search in root directory");
        exit(1);
    }

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

    // Creating directories queue
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
    pthread_mutex_init(&dqlock, NULL);
    pthread_mutex_init(&tqlock, NULL);
    pthread_mutex_init(&talock, NULL);
    
    // Creating start and end CV and locking their locks
    pthread_cond_init(&startCV, NULL);
    pthread_mutex_lock(&slock);
    pthread_cond_init(&endCV, NULL);

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

    // Signaling searching threads to start
    startFlag = 1;
    pthread_cond_broadcast(&startCV);
    pthread_mutex_unlock(&slock);

    // Waiting for all threads to exit
    for (i = 0; i < numOfThreads; i++) {
        returnVal = pthread_join(threadsArr[i]->thread, NULL);
        if (returnVal != 0) {
            perror("Error in joining main to threads");
            printf("Done searching, found %d files\n", fileCounter);
            exit(1);
        }
    }

    // Finished and there was an error in a thread
    if (numOfErroredThreads > 0) {
        printf("Done searching, found %d files\n", fileCounter);
        exit(1);
    }

    // Destroying locks
    pthread_mutex_destroy(&slock);
    pthread_mutex_destroy(&tqlock);
    pthread_mutex_destroy(&dqlock);
    pthread_mutex_destroy(&talock);

    // Destroying cvs
    for (i = 0; i < numOfThreads; i++) {
        pthread_cond_destroy(&cvs[i]);
    }

    // Printing message
    printf("Done searching, found %d files\n", fileCounter);
    exit(0);
}