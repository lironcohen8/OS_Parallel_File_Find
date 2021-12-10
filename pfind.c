#include <sys/types.h>
#include <dirent.h>

int main(int argc, char *argv[]) {
    int n;
    // Checking number of arguments
    if (argc != 3) {
        perror("Number of cmd args is not 3");
	    exit(1);
    }

    // Parsing arguments
    char *D = argv[1];
    char *T = argv[2];
    sscanf(argv[3],"%d",&n);

    // Checking that search root directory can be searched
    if (opendir(D) == NULL) {
        perror("Can't search in root directory")
        exit(1);
    }
    

}