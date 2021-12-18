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
}

struct dirQueue {
    struct directoryNode *head;
    struct directoryNode *tail;
};

struct threadsQueue {
    struct threadNode *head; // index of thread that went to sleep first
    struct threadNode *tail; // index of thread that went to sleep last
};

pthread_mutex_t qlock;
pthread_cond_t startCV;
atomic_int counter;
struct dirQueue *dirQueue;
struct threadsQueue *threadsQueue;
char *searchTerm;

void dirEnqueue(struct directoryNode *dir) {
    pthread_mutex_lock(&qlock);
    // Adding dir to queue
    if (dirQueue->head == NULL) {
        dirQueue->head = dir;
        dirQueue->tail = dir;
    }
    else {
        dirQueue->tail->nextDir = dir;
        dirQueue->tail = dir;
    }
    int indexToWake = threadsQueue->head->threadIndex;
    pthread_cond_signal(&cvs[indexToWake]);
    pthread_mutex_unlock(&qlock);
}

struct directoryNode* dirDequeue(int threadIndex) {
    struct directoryNode * firstDirInQueue = NULL;
    pthread_mutex_lock(&qlock);
    // while queue is empty
    if ((dirQueue->head)==NULL) {// dirQueue is empty
        threadEnqueue(threadIndex);
        while (pthread_cond_wait(&cvs[i], &qlock)) {
            // Remove first dir from queue
            firstDirInQueue = dirQueue->head;
            dirQueue->head = firstDirInQueue->nextDir;
        }
    }
    pthread_mutex_unlock(&qlock);
    return firstDirInQueue;
}

void threadEnqueue(int index) {
    struct threadNode *threadNode = (struct threadNode *)calloc(1, sizeof(struct threadNode));
    threadNode->threadIndex = index;

    // Adding thread Node to queue
    if (threadsQueue->head == NULL) {
        threadsQueue->head = threadNode;
        threadsQueue->tail = threadNode;
    }
    else {
        threadsQueue->tail->nextThread = threadNode;
        threadsQueue->tail = threadNode;
    }
}

void *searchTermInDir(int i) {
    // Waiting for signal from main thread
    pthread_cond_wait(&startCV, qlock);

    // Take head directory from queue (including waiting to be not empty)
    struct directoryNode *d = dequeue(i);
    DIR *dir = opendir(d->dirPath);    
    struct dirent *entry = readdir(dir);
    while (entry != NULL) {
        char *entryName = entry->d_name;
        if (strcmp(entryName, ".") || strcmp(entryName, "..")) {
            continue;
        }

        // Checking entry type
        struct stat entryStat;
        stat(d->dirPath, &entryStat);

        // If it's a directory
        if (S_ISDIR(entryStat.st_mode)) {
            // Checking if directory can't be searched
            if ((dir = opendir(d->dirPath)) == NULL) {
                // TODO make sure it's a full path
                printf("Directory %s: Permission denied.\n", d->dirPath);
                continue;
            }
            // Directory can be searched
            else {
                enqueue(d);
            }
        }

        // If it's not a directory
        else {
            // Entry name contains search term
            if (strstr(entryName, searchTerm) != NULL) {
                // TODO make sure it's a full path
                printf("%s\n", d->dirPath);
                counter++;
            }
        }
        closedir(d->dirPath);
        entry = readdir(dir);
    }
    // TODO add a loop here to dequeue again    
    return 0;
}

int main(int argc, char *argv[]) {
    int numOfThreads, returnVal, i;
    struct threadNode *threadsArr;
    pthread_cond_t *cvs;
    char *rootDirPath;

    // Checking number of arguments
    if (argc != 3) {
        perror("Number of cmd args is not 3");
	    exit(1);
    }

    // Parsing arguments
    rootDirPath = argv[1];
    searchTerm = argv[2];
    sscanf(argv[3],"%d",&numOfThreads);
    threadsArr = (pthread_t *)calloc(numOfThreads, sizeof(pthread_t));
    cvs = (pthread_cond_t *)calloc(numOfThreads, sizeof(pthread_cond_t));

    // Creating directory queue
    dirQueue = (struct dirQueue*)calloc(1, sizeof(struct dirQueue));
    
    // Creating threads sleeping queue
    threadsQueue = (struct threadsQueue*)calloc(1, sizeof(struct threadsQueue));

    // Checking that search root directory can be searched
    if (opendir(rootDirPath) == NULL) {
        perror("Can't search in root directory");
        exit(1);
    }

    // Creating root directory and put it in queue
    struct directoryNode* D = (struct directoryNode *)calloc(1, sizeof(struct directoryNode));
    if (D == NULL) {
        perror("Can't allocate memory for root directory struct");
        exit(1);
    }
    D->dirPath = rootDirPath;
    dirEnqueue(D);

    // Initializing mutex
    returnVal = pthread_mutex_init(&qlock, NULL);
    if (returnVal) {
        perror("Can't initialize mutex");
        exit(1);
    }
    
    // Creating start CV
    pthread_cond_init(&startCV, NULL);

    // Creating threads and cvs
    for (i = 0; i < numOfThreads; ++i) {
        returnVal = pthread_create(&threadsArr[i], NULL, searchTermInDir, i);
        if (returnVal) {
            perror("Can't create thread");
            exit(1);
        }
        pthread_cond_init(cvs[i], NULL);
    }

    // Signaling the threads to start
    pthread_cond_broadcast(&startCV);

    pthread_mutex_destroy(&qlock);
    pthread_exit(NULL);
}