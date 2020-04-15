#include <getopt.h>  // getopt, optarg
#include <stdlib.h>  // exit, atoi, malloc, free
#include <stdio.h>   // printf, fprintf, stderr, fopen, fclose, FILE
#include <limits.h>  // ULONG_MAX
#include <string.h>  // strcmp, strerror
#include <errno.h>   // errno

/* fast base-2 integer logarithm */
#define INT_LOG2(x) (31 - __builtin_clz(x))
#define NOT_POWER2(x) (__builtin_clz(x) + __builtin_ctz(x) != 31)

/* tag_bits = ADDRESS_LENGTH - set_bits - block_bits */
#define ADDRESS_LENGTH 64

/**
 * Print program usage (no need to modify).
 */
static void print_usage() {
    printf("Usage: csim [-hv] -S <num> -K <num> -B <num> -p <policy> -t <file>\n");
    printf("Options:\n");
    printf("  -h           Print this help message.\n");
    printf("  -v           Optional verbose flag.\n");
    printf("  -S <num>     Number of sets.           (must be > 0)\n");
    printf("  -K <num>     Number of lines per set.  (must be > 0)\n");
    printf("  -B <num>     Number of bytes per line. (must be > 0)\n");
    printf("  -p <policy>  Eviction policy. (one of 'FIFO', 'LRU')\n");
    printf("  -t <file>    Trace file.\n\n");
    printf("Examples:\n");
    printf("  $ ./csim    -S 16  -K 1 -B 16 -p LRU -t traces/yi.trace\n");
    printf("  $ ./csim -v -S 256 -K 2 -B 16 -p LRU -t traces/yi.trace\n");
}

/* Parameters set by command-line args (no need to modify) */
int verbose = 0;   // print trace if 1
int S = 0;         // number of sets
int K = 0;         // lines per set
int B = 0;         // bytes per line

typedef enum { FIFO = 1, LRU = 2 } Policy;
Policy policy;     // 0 (undefined) by default

FILE *trace_fp = NULL;



/**
 * Parse input arguments and set verbose, S, K, B, policy, trace_fp.
 *
 * TODO: Finish implementation
 */
static void parse_arguments(int argc, char **argv) {
    char c;
    while ((c = getopt(argc, argv, "S:K:B:p:t:vh")) != -1) {
        switch(c) {
            case 'S':
                S = atoi(optarg);
                if (NOT_POWER2(S)) {
                    fprintf(stderr, "ERROR: S must be a power of 2\n");
                    exit(1);
                }
                break;
            case 'K':
                K = atoi(optarg);
                break;
            case 'B':
                B = atoi(optarg);
                break;
            case 'p':
                if (!strcmp(optarg, "FIFO")) {
                    policy = FIFO;
                }
                // LRU
                else if(!strcmp(optarg, "LRU")){
                    policy = LRU;
                }
                // Error
                else {
                    fprintf(stderr, "ERROR: Invalid policy type");
                    exit(1);
                }

                break;
            case 't':
                // TODO: open file trace_fp for reading
                trace_fp = fopen(optarg, "r");

                if (!trace_fp) {
                    fprintf(stderr, "ERROR: %s: %s\n", optarg, strerror(errno));
                    exit(1);
                }
                break;
            case 'v':
                verbose = 1;
                break;
            case 'h':
                print_usage();
                exit(0);
            default:
                print_usage();
                exit(1);
        }
    }

    /* Make sure that all required command line args were specified and valid */
    if (S <= 0 || K <= 0 || B <= 0 || policy == 0 || !trace_fp) {
        printf("ERROR: Negative or missing command line arguments\n");
        print_usage();
        if (trace_fp) {
            fclose(trace_fp);
        }
        exit(1);
    }

    /* Other setup if needed */



}

/**
 * Cache data structures
 * TODO: Define your own!
 */


typedef enum {
    Modify,
    Load,
    Save
} Operation;

typedef struct {

    unsigned long address;
    unsigned long tag;
    int occupied;
    int LRUCount, FIFOCount;

} Block;

typedef struct {

    Block* blocks;

} Line;

typedef struct {

    Line* lines;

} Cache;

Cache cache;

int LRUCount = 0;
int FIFOCount = 0;

/**
 * Allocate cache data structures.
 *
 * This function dynamically allocates (with malloc) data structures for each of
 * the `S` sets and `K` lines per set.
 *
 * TODO: Implement
 */
static void allocate_cache() {

    cache.lines = malloc(S * sizeof(Line)); // sets * size of pointer addresses

    for(int i = 0; i < S; i++){

        cache.lines[i].blocks = malloc(K * sizeof(Block));

        for(int j = 0; j < K; j++){
            cache.lines[i].blocks[j].LRUCount = 0;
            cache.lines[i].blocks[j].occupied = 0;
            cache.lines[i].blocks[j].tag = 0;
            cache.lines[i].blocks[j].FIFOCount = INT_MAX;
        }

    }

}

/**
 * Deallocate cache data structures.
 *
 * This function deallocates (with free) the cache data structures of each
 * set and line.
 *
 * TODO: Implement
 */
static void free_cache() {

    for(int i = 0; i < S; i++){
        free(cache.lines[i].blocks);
    }

    free(cache.lines);

}

/* Counters used to record cache statistics */
int miss_count     = 0;
int hit_count      = 0;
int eviction_count = 0;

/**
 * Simulate a memory access.
 *
 * If the line is already in the cache, increase `hit_count`; otherwise,
 * increase `miss_count`; increase `eviction_count` if another line must be
 * evicted. This function also updates the metadata used to implement eviction
 * policies (LRU, FIFO).
 *
 * TODO: Implement
 */

// For calculating set / offset bits
int Log2(int x){

    int result = 0;

    while(x >>= 1){
        result++;
    }

    return result;
}

void printCache(){

    for(int i = 0; i < S; i++){

        for(int j = 0; j < K; j++){
            printf("[%lu,%d],", cache.lines[i].blocks[j].tag, cache.lines[i].blocks[j].occupied);
        }

        printf("\n");

    }

}

static void access_data(unsigned long addr) {

    unsigned long sum = (Log2(S)) + (Log2(B)); // Used in calculations
    int evicted = 0; // in case of eviction, stores the index
    unsigned long tag = addr >> sum; // the tag of a line
    unsigned long setTag = (addr >> (Log2(B))) & ((1 << (Log2(S))) - 1); // tag of a set

    // Visit all lines in set
    for(int i = 0; i < K; i++){

        // Check valid bit / correct tag
		if (cache.lines[setTag].blocks[i].occupied != 0 && cache.lines[setTag].blocks[i].tag == tag){

            // Woohoo! We have a hit!
			hit_count++;

            // Update line to be most recently used
            if(policy == LRU){
			    cache.lines[setTag].blocks[i].LRUCount = LRUCount++;
            }

            // FIFO value shouldn't update bc no insertion

            // Print
            if (verbose == 1){
                printf("hit \n");
            }

			return;

		}

    }

    // Miss :(
    miss_count++;

    if(verbose == 1){
        printf("miss ");
    }

    int min = INT_MAX;
    evicted = 0;

    // put a while loop here
    // while not all bytes have been stored

    // Loop over every index and see if one isn't occupied
    for(int i = 0; i < K; i++){

        // for-loop here to check if there are enough bytes in a row for storage

        // Not occupied - place there
        if(cache.lines[setTag].blocks[i].occupied == 0){

            // Fill line
            cache.lines[setTag].blocks[i].occupied = 1;
            cache.lines[setTag].blocks[i].tag = tag;

            // Update when it was accessed / set
            if(policy == LRU){
                cache.lines[setTag].blocks[i].LRUCount = LRUCount++;
            }
            // Update FIFO value
            else if(policy == FIFO){
                cache.lines[setTag].blocks[i].FIFOCount = FIFOCount++;
            }

            if(verbose == 1){
                printf("\n");
            }

            return;

        }

    }

    if(verbose){
        printf("eviction ");
    }

    // This you only do if the cache is full
    // This is successful too many times for FIFO
    for(int i = 0; i < K; i++){

        // Update the best option for LRU
        if(policy == LRU && cache.lines[setTag].blocks[i].occupied == 1 && cache.lines[setTag].blocks[i].LRUCount < min){
            min = cache.lines[setTag].blocks[i].LRUCount;
            evicted = i;
        }

        // Update the best option for FIFO
        else if(policy == FIFO && cache.lines[setTag].blocks[i].occupied == 1 && cache.lines[setTag].blocks[i].FIFOCount < min){
            min = cache.lines[setTag].blocks[i].FIFOCount;
            evicted = i;
        }

    }

    // KICK 'em out
    if(cache.lines[setTag].blocks[evicted].occupied == 1){
        eviction_count++;
    }

    // Handle multiple bytes of data - potentially more than one (third argument passed in)

    // Fill the evicted line
    cache.lines[setTag].blocks[evicted].occupied = 1;
    cache.lines[setTag].blocks[evicted].tag = tag;

    // Update policy counters
    if(policy == LRU){
        cache.lines[setTag].blocks[evicted].LRUCount = LRUCount++;
    }

    else if(policy == FIFO){
        cache.lines[setTag].blocks[evicted].FIFOCount = FIFOCount++;
    }

    if(verbose == 1){
        printf("\n");
    }

}

/**
 * Replay the input trace.
 *
 * This function:
 * - reads lines (e.g., using fgets) from the file handle `trace_fp` (a global variable)
 * - skips lines not starting with ` S`, ` L` or ` M`
 * - parses the memory address (unsigned long, in hex) and len (unsigned int, in decimal)
 *   from each input line
 * - calls `access_data(address)` for each access to a cache line
 *
 * TODO: Implement
 */
static void replay_trace() {

    char type;
    unsigned long address;
    unsigned int len;

    char line[1000];

    // Read in from file
    while(fgets(line, 1000, trace_fp))
    {
        // Skip instruction loads
        if (line[1] == 'I')
        {
            continue;
        }

        // Pull data
        sscanf(line," %c %lx,%d", &type, &address, &len);

        if(line[1] == 'M' || line[1] == 'S' || line[1] == 'L'){

            // If multiple blocks accessed, have to access multiple times
            unsigned long start = address;
            unsigned long end = address + len;

            // Round off start and end bits
            while (start % B != 0) {
                start--;
            }
            while (end % B != 0) {
                end++;
            }

            // Print data
            if(verbose == 1){
                printf("%c %lx,%u ", type, address, len);
            }

            // First accesses - loop over every access
            for (unsigned long i=start; i<end; i+=B) {
                access_data(i);
            }
            //printCache();

            // Store data back in cache for modify
            if(type == 'M'){
                for (unsigned long i=start; i<end; i+=B) {
                    access_data(i);
                }
                //printCache();
            }
        }

    }

}

/**
 * Print cache statistics (DO NOT MODIFY).
 */
static void print_summary(int hits, int misses, int evictions) {
    printf("hits:%d misses:%d evictions:%d\n", hits, misses, evictions);
}

int main(int argc, char **argv) {
    parse_arguments(argc, argv);  // set global variables used by simulation
    allocate_cache();             // allocate data structures of cache
    replay_trace();               // simulate the trace and update counts
    free_cache();                 // deallocate data structures of cache
    fclose(trace_fp);             // close trace file
    print_summary(hit_count, miss_count, eviction_count);  // print counts
    return 0;
}
