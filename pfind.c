#include <sys/types.h>
#include <dirent.h>
#include <pthread.h>
#include <stdatomic.h>

struct directory {
    char *dirPath;
    struct directory *nextDir;
}

struct dirQueue {
    struct directory *head;
    struct directory *tail;
}

pthread_mutex_t qlock;
pthread_cond_t notEmpty;
atomic_int counter = 0;


void enqueue(struct directory *d) {
    pthread_mutex_lock(&qlock);
    /* … add x to queue … */
    pthread_cond_signal(&notEmpty);
    pthread_mutex_unlock(&qlock);
}

struct directory dequeue() {
    pthread_mutex_lock(&qlock);
    // while queue is empty
    while pthread_cond_wait(&notEmpty,&qlock) {
    /* … remove item from queue … */
    }
    pthread_mutex_unlock(&qlock);
    /* .. return removed item */
}

void searchTermInDir(char *) {

}

int main(int argc, char *argv[]) {
    int numOfThreads, returnVal, i;
    pthread_t *threads;
    char *rootDirPath, *searchTerm;

    // Checking number of arguments
    if (argc != 3) {
        perror("Number of cmd args is not 3");
	    exit(1);
    }

    // Parsing arguments
    *rootDirPath = argv[1];
    *searchTerm = argv[2];
    sscanf(argv[3],"%d",&numOfThreads);
    threads = (pthread_t *)calloc(numOfThreads, sizeof(pthread_t));

    // Checking that search root directory can be searched
    if (opendir(rootDirPath) == NULL) {
        perror("Can't search in root directory");
        exit(1);
    }

    // Creating root directory and put it in queue
    struct directory* D = (struct directory *)calloc(1, sizeof(struct directory));
    if (D == NULL) {
        perror("Can't search in root directory");
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
        returnVal = pthread_create(&threads[i], NULL, searchTermInDir, (void *)t);
        if (rc) {
        printf("ERROR in pthread_create():"
                " %s\n",
                strerror(rc));
        exit(-1);
        }
    }
}