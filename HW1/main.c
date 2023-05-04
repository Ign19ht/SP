#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include "libcoro.h"
#include "limits.h"
#include <time.h>
#include "string.h"

// arguments for coroutine
typedef struct arguments {
    char **filenames;
    int *current_file_i;
    int files_amount;
    int **arrays;
    int *sizes;
    int name;
}arguments;

static u_int64_t yield_time; // timestamp of last yield
static int coro_target_latency; // target latency / number of coroutines
static u_int64_t *coro_live_time;
static u_int64_t *coro_start_time;

// get current timestamp in microseconds
u_int64_t GetTimeStamp() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec*1000000+ts.tv_nsec/1000;
}

// make yield if target latency end
void my_yield(int coro_name) {
    u_int64_t current_time = GetTimeStamp();
    if (current_time >= yield_time + coro_target_latency) {
//        printf("Yield at timestamp: %lu\n", current_time);
        yield_time = current_time;
        coro_live_time[coro_name] += current_time - coro_start_time[coro_name];
        coro_yield();
        coro_start_time[coro_name] = GetTimeStamp();
    }
}

// part of quick sort
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

// quick sort implementation
void sort(int *arr, int low, int high, int coro_name) {
    if (low < high) {
        int pi = partition(arr, low, high);
        my_yield(coro_name);
        sort(arr, low, pi - 1, coro_name);
        sort(arr, pi + 1, high, coro_name);
    }
}

// read data from file
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

// coroutine function
// char **filenames, int *current_file_i, int files_amount, int **arrays, int *sizes
int worker(void *context) {
    arguments *args = context;
    coro_start_time[args->name] = GetTimeStamp();
    while (1) {
        int current_i = (*args->current_file_i)++;
        if (current_i >= args->files_amount) break;
        read_file(args->arrays, current_i, args->filenames[current_i], &args->sizes[current_i]);
        printf("%s file read by coroutine %d\n", args->filenames[current_i], args->name);
        sort(args->arrays[current_i], 0, args->sizes[current_i] - 1, args->name);
        printf("%s file sorted by coroutine %d\n", args->filenames[current_i], args->name);
    }
    coro_live_time[args->name] += GetTimeStamp() - coro_start_time[args->name];
    printf("Coroutine %d finished. Its switch count: %lld , work time: %llu us\n", args->name,
          coro_switch_count(coro_this()), coro_live_time[args->name]);
    return 0;
}

// write output data
void write_file(int *arr, int n) {
    FILE *file;
    file = fopen("output.txt", "w");
    for (int i = 0; i < n; i++) {
        fprintf(file, "%d ", arr[i]);
    }
    fclose(file);
}

// merge sorted arrays
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
    u_int64_t start_time = GetTimeStamp();
    // take data from system arguments
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

    // initialize input and output data for coroutines
    coro_target_latency = target_latency / cor_nums;
    int current_file_i = 0;
    int sizes[files_amount];
    int *arrays[files_amount];
    for (int i = 0; i < files_amount; i++) {
        arrays[i] = (int*)calloc(10, sizeof(int));
    }

    coro_sched_init();

    // collect arguments for coroutines
    arguments worker_args[cor_nums];

    // set time and start coroutines
    yield_time = GetTimeStamp();
    coro_live_time = calloc(cor_nums, sizeof(u_int64_t));
    coro_start_time = calloc(cor_nums, sizeof(u_int64_t));
    for (int i = 0; i < cor_nums; ++i) {
        worker_args[i].filenames = filenames;
        worker_args[i].current_file_i = &current_file_i;
        worker_args[i].files_amount = files_amount;
        worker_args[i].arrays = arrays;
        worker_args[i].sizes = sizes;
        worker_args[i].name = i;
        coro_live_time[i] = 0;
        coro_start_time[i] = 0;
        coro_new(worker, &worker_args[i]);
    }

    // wait the end of coroutines and delete their
    struct coro *c;
    while ((c = coro_sched_wait()) != NULL) {
        coro_delete(c);
    }

    // merge data
    merge(arrays, sizes, files_amount);
    printf("All files merged in output.txt\n");

    // free space of calloc
    for (int i = 0; i < files_amount; i++) {
        free(arrays[i]);
    }
    free(coro_start_time);
    free(coro_live_time);

    printf("Total work time: %llu us", GetTimeStamp() - start_time);
    return 0;
}
