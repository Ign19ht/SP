#include <stdio.h>
#include <malloc.h>
#include "libcoro.h"
#include "limits.h"

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
        sort(arr, low, pi - 1);
        sort(arr, pi + 1, high);
    }
}

void read_file(int *arr, char *filename, int *size) {
    FILE *file;
    file = fopen(filename, "r");
    if (file == NULL) {
        return;
    }

    int i = 0;
    while(!feof(file)) {
        if (i > 0 && i % 10 == 0) {
            arr = realloc(arr, (i + 10) * sizeof(int));
        }
        fscanf(file, "%d", &arr[i++]);
    }
    *size = i;
    fclose(file);
}

int worker(char **filenames, int *current_file_i, int files_amount, int **arrays, int *sizes) {
    while (1) {
        int current_i = (*current_file_i)++;
        if (current_i == files_amount) break;
        read_file(arrays[current_i], filenames[current_i], &sizes[current_i]);
        sort(arrays[current_i], 0, sizes[current_i] - 1);
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
    char **filenames;
    filenames = &argv[1];
    int files_amount = argc - 1;
    int current_file_i = 0;
    int sizes[files_amount];
    int *arrays[files_amount];
    for (int i = 0; i < files_amount; i++) {
        arrays[i] = (int*)calloc(10, sizeof(int));
    }

    worker(filenames, &current_file_i, files_amount, arrays, sizes);

    merge(arrays, sizes, argc - 1);
    for (int i = 0; i < files_amount; i++) {
        free(arrays[i]);
    }
    return 0;
}
