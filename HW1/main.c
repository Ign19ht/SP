#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdint-gcc.h>
#include "libcoro.h"
#include "limits.h"
#include "string.h"

typedef struct arguments {
    char **filenames;
    int *current_file_i;
    int files_amount;
    int **arrays;
    int *sizes;
}arguments;

static uint64_t yield_time;
static int coro_target_latency;

uint64_t GetTimeStamp() {
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return tv.tv_sec*(uint64_t)1000000+tv.tv_usec;
}

void my_yield() {
    uint64_t current_time = GetTimeStamp();
    if (current_time >= yield_time + coro_target_latency) {
//        printf("Yield at timestamp: %lu\n", current_time);
        yield_time = current_time;
        coro_yield();
    }
}

int partition(int *arr, int low, int high) {
    int pivot = arr[high];
    int i = low - 1;
    for (int j = low; j <= high - 1; j++) {
        if (arr[j] < pivot) {
            i++;
            int temp = arr[j];
            arr[j] = arr[i];
            arr[i] = temp;
        }
    }
    i++;
    int temp = arr[high];
    arr[high] = arr[i];
    arr[i] = temp;
    return i;
}

void sort(int *arr, int low, int high) {
    if (low < high) {
        int pi = partition(arr, low, high);
        my_yield();
        sort(arr, low, pi - 1);
        sort(arr, pi + 1, high);
    }
}

void read_file(int **arrays, int array_i, char *filename, int *size) {
    FILE *file;
    file = fopen(filename, "r");
    if (file == NULL) {
        return;
    }

    int i = 0;
    while(!feof(file)) {
        if (i > 0 && i % 10 == 0) {
            arrays[array_i] = realloc(arrays[array_i], (i + 10) * sizeof(int));
        }
        fscanf(file, "%d", &arrays[array_i][i++]);
    }
    *size = i;
    fclose(file);
}
// char **filenames, int *current_file_i, int files_amount, int **arrays, int *sizes
int worker(void *context) {
    arguments *args = context;
    while (1) {
        int current_i = (*args->current_file_i)++;
        if (current_i >= args->files_amount) break;
        read_file(args->arrays, current_i, args->filenames[current_i], &args->sizes[current_i]);
        printf("%s file read\n", args->filenames[current_i]);
        sort(args->arrays[current_i], 0, args->sizes[current_i] - 1);
        printf("%s file sorted\n", args->filenames[current_i]);
    }
    return 0;
}

void write_file(int *arr, int n) {
    FILE *file;
    file = fopen("output.txt", "w");
    for (int i = 0; i < n; i++) {
        fprintf(file, "%d ", arr[i]);
    }
    fclose(file);
}

void merge(int *arrays[], int *sizes, int n) {
    int total_size = 0;
    int iterators[n];
    for (int i = 0; i < n; i++) {
        iterators[i] = 0;
        total_size += sizes[i];
    }
    int result[total_size];
    int result_i = 0;

    while (1) {
        int min = INT_MAX;
        int min_i = -1;

        for (int i = 0; i < n; i++) {
            if (iterators[i] == sizes[i]) continue;
            if (arrays[i][iterators[i]] < min) {
                min = arrays[i][iterators[i]];
                min_i = i;
            }
        }

        if (min_i == -1) break;

        while (min == arrays[min_i][iterators[min_i]] && iterators[min_i] < sizes[min_i]) {
            result[result_i++] = arrays[min_i][iterators[min_i]++];
        }
    }
    write_file(result, result_i);
}


int main(int argc, char *argv[]) {
    uint64_t start_time = GetTimeStamp();
    int cor_nums = 3;
    int target_latency = 50;
    char **filenames = calloc(argc - 1, sizeof(int*));
    int files_amount = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0) {
            cor_nums = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-l") == 0) {
            target_latency = atoi(argv[++i]);
        } else {
            filenames[files_amount++] = argv[i];
        }
    }

    coro_target_latency = target_latency / cor_nums;
    int current_file_i = 0;
    int sizes[files_amount];
    int *arrays[files_amount];
    for (int i = 0; i < files_amount; i++) {
        arrays[i] = (int*)calloc(10, sizeof(int));
    }

    coro_sched_init();

    arguments worker_args = {filenames, &current_file_i, files_amount, arrays, sizes};

    yield_time = GetTimeStamp();
    uint64_t coro_start_time = GetTimeStamp();
    for (int i = 0; i < cor_nums; ++i) {
        coro_new(worker, &worker_args);
    }

    struct coro *c;
    while ((c = coro_sched_wait()) != NULL) {
        printf("Coroutine finished. Its switch count: %lld , work time: %lu\n",
               coro_switch_count(c), GetTimeStamp() - coro_start_time);
        coro_delete(c);
    }

    merge(arrays, sizes, argc - 1);
    printf("All files merged in output.txt\n");
    for (int i = 0; i < files_amount; i++) {
        free(arrays[i]);
    }
    printf("Total work time: %lu mcs", GetTimeStamp() - start_time);
    return 0;
}
