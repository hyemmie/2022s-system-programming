#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include "cachelab.h"

typedef struct cache_line {
    int valid, lru_counter;
    unsigned long long tag;
} line;

line **init_cache(int s, int E);
void free_cache(line **cache, FILE *pTrace, int s);

int main(int argc, char **argv)
{
    int opt, s, E, b;
    int set_mask;
    char trc[20] = {0};
    FILE *pTrace;

    while ((opt = getopt(argc, argv, "s:E:b:t:")) != -1) {
        switch (opt) {
        case 's':
            s = atoi(optarg);
            set_mask = ~(~0 << s);
            break;
        case 'E':
            E = atoi(optarg);
            break;
        case 'b':
            b = atoi(optarg);
            break;
        case 't':
            strcpy(trc, optarg);
            pTrace = fopen(trc, "r+");
            break;
        default:
            break;
        }
    }

    line **cache = init_cache(s, E);

    char id;
    unsigned long long addr;
    int sz;

    int hits = 0, misses = 0, evictions = 0;

    while (fscanf(pTrace, " %c %llx,%d", &id, &addr, &sz) > 0) {
        if (id == 'I' || id == ' ')
            continue;
        else {
            int set = (addr >> b) & set_mask;
            unsigned long long tag = addr >> (b + s);

            int is_hit = 0;

            int largest_lru_count = -1;
            int lru_index = 0;
            int empty_index = 0;

            for (int i = 0; i < E; ++i) {

                /* for valid line, check cache hit and update lru count */
                if (cache[set][i].valid) {
                    if (cache[set][i].tag == tag) {
                        hits++;
                        if (id == 'M') {
                            hits++;
                        }
                        is_hit = 1;
                        cache[set][i].lru_counter = -1;
                    }
                    empty_index++;
                    cache[set][i].lru_counter++;

                    if (largest_lru_count < cache[set][i].lru_counter) {
                        largest_lru_count = cache[set][i].lru_counter;
                        lru_index = i;
                    }
                }

                /* handle cache miss */
                else if (!is_hit) {
                    cache[set][empty_index].valid = 1;
                    cache[set][empty_index].lru_counter = 0;
                    cache[set][empty_index].tag = tag;
                    misses++;
                    if (id == 'M') {
                        hits++;
                    }
                    is_hit = 1;
                    break;
                }
            }

            /* handle eviction */
            if (!is_hit && empty_index == E) {
                misses++;
                cache[set][lru_index].tag = tag;
                cache[set][lru_index].lru_counter = 0;
                evictions++;
                if (id == 'M') {
                    hits++;
                }
            }
        }
    }

    free_cache(cache, pTrace, s);

    printSummary(hits, misses, evictions);
    return 0;
}

line **init_cache(int s, int E) {
    line **cache;
    cache = (line **)malloc(sizeof(line *) * (1 << s));
    for (int i = 0; i < 1 << s; ++i) {
        cache[i] = (line *)malloc(sizeof(line) * E);
    }

    return cache;
}

void free_cache(line **cache, FILE *pTrace, int s) {
    for (int i = 0; i < 1 << s; ++i) {
        free(cache[i]);
    }
    free(cache);
    fclose(pTrace);
}
