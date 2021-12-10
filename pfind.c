#include <sys/types.h>
#include <dirent.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <limits.h>

struct directory {
    char *dirPath;
    // TODO check if PATH_MAX is needed
    struct directory *nextDir;
};

struct queue {
    struct directory *head;
    struct directory *tail;
};

pthread_mutex_t qlock;
pthread_cond_t notEmpty;
atomic_int counter;
struct queue *dirQueue;
char *searchTerm;

void enqueue(struct directory *dir) {
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
    pthread_cond_signal(&notEmpty);
    pthread_mutex_unlock(&qlock);
}

struct directory* dequeue() {
    struct directory * firstDirInQueue = NULL;
    pthread_mutex_lock(&qlock);
    // while queue is empty
    while (pthread_cond_wait(&notEmpty,&qlock)) {
        // Remove first dir from queue
        firstDirInQueue = dirQueue->head;
        dirQueue->head = firstDirInQueue->nextDir;
    }
    pthread_mutex_unlock(&qlock);
    return firstDirInQueue;
}

void *searchTermInDir() {
    // Waiting for all threads to be created

    // Waiting for signal from main thread

    // Take head directory from queue (including waiting to be not empty)
    struct directory *d = dequeue();
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

        // If it's not a directort
        else {
            // Entry name contains search term
            if (strstr(entryName, searchTerm) != NULL) {
                // TODO make sure it's a full path
                printf("%s\n", d->dirPath);
                counter++;
            }
        }
    }
    // TODO add a loop here to dequeue again    
    return 0;
}

int main(int argc, char *argv[]) {
    int numOfThreads, returnVal, i;
    pthread_t *threads;
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
    threads = (pthread_t *)calloc(numOfThreads, sizeof(pthread_t));

    // Creating queue
    dirQueue = (struct queue*)calloc(1, sizeof(struct queue));

    // Checking that search root directory can be searched
    if (opendir(rootDirPath) == NULL) {
        perror("Can't search in root directory");
        exit(1);
    }

    // Creating root directory and put it in queue
    struct directory* D = (struct directory *)calloc(1, sizeof(struct directory));
    if (D == NULL) {
        perror("Can't allocate memory for root directory struct");
        exit(1);
    }
    D->dirPath = rootDirPath;
    enqueue(D);

    // Initializing mutex
    returnVal = pthread_mutex_init(&qlock, NULL);
    if (returnVal) {
        perror("Can't initialize mutex");
        exit(1);
    }

    // Creating threads
    for (i = 0; i < numOfThreads; ++i) {
        returnVal = pthread_create(&threads[i], NULL, searchTermInDir, NULL);
        if (returnVal) {
            perror("Can't create thread");
            exit(1);
        }
    }

    // Signaling the threads to start
    // TODO add

    pthread_mutex_destroy(&qlock);
    pthread_exit(NULL);
}