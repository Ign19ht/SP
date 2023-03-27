#include "userfs.h"
#include <malloc.h>
#include <string.h>

enum {
    BLOCK_SIZE = 512,
    MAX_FILE_SIZE = 1024 * 1024 * 100,
};

/** Global error code. Set from any function on any error. */
static enum ufs_error_code ufs_error_code = UFS_ERR_NO_ERR;

struct block {
    /** Block memory. */
    char *memory;
    /** How many bytes are occupied. */
    int occupied;
    /** Next block in the file. */
    struct block *next;
    /** Previous block in the file. */
    struct block *prev;

    /* PUT HERE OTHER MEMBERS */
};

struct file {
    /** Double-linked list of file blocks. */
    struct block *block_list;
    /**
     * Last block in the list above for fast access to the end
     * of file.
     */
    struct block *last_block;
    /** How many file descriptors are opened on the file. */
    int refs;
    /** File name. */
    const char *name;
    /** Files are stored in a double-linked list. */
    struct file *next;
    struct file *prev;

    /* PUT HERE OTHER MEMBERS */
    int removed;
    int block_count;
};

/** List of all files. */
static struct file *file_list = NULL;

struct filedesc {
    struct file *file;
    /* PUT HERE OTHER MEMBERS */
    struct block *current_block;
    int offset;
    int permission;
};

/**
 * An array of file descriptors. When a file descriptor is
 * created, its pointer drops here. When a file descriptor is
 * closed, its place in this array is set to NULL and can be
 * taken by next ufs_open() call.
 */
static struct filedesc **file_descriptors = NULL;
static int file_descriptor_count = 0;
static int file_descriptor_capacity = 0;

enum ufs_error_code
ufs_errno() {
    return ufs_error_code;
}

void encrease_fd() {
    file_descriptor_capacity += 5;
    if (!file_descriptors) {
        file_descriptors = malloc(sizeof(struct filedesc) * file_descriptor_capacity);
    } else {
        file_descriptors = realloc(file_descriptors, sizeof(struct filedesc) * file_descriptor_capacity);
    }
    for (int i = 0; i < 5; i++) {
        file_descriptors[file_descriptor_capacity - 5 + i] = NULL;
    }
}

struct file *add_file(const char *filename) {
    struct file *new_file = malloc(sizeof(struct file));
    char *filename_copy = malloc(strlen(filename) + 1);
    strcpy(filename_copy, filename);
    new_file->name = filename_copy;
    new_file->refs = 0;
    new_file->removed = 0;
    new_file->block_count = 0;
    new_file->block_list = NULL;
    new_file->last_block = NULL;
    new_file->next = NULL;
    new_file->prev = NULL;

    if (!file_list) file_list = new_file;
    else {
        struct file *current_file = file_list;
        for (;;) {
            if (current_file->next) current_file = current_file->next;
            else break;
        }
        current_file->next = new_file;
        new_file->prev = current_file;
    }
    return new_file;
}

void free_file(struct file *file) {
    struct block *current_block = file->block_list;
    for (;;) {
        if (!current_block) break;
        if (current_block->memory) free(current_block->memory);
        if (current_block->prev) free(current_block->prev);
        current_block = current_block->next;
    }
    free(file);
}

struct block *add_block(struct file *file) {
    struct block *new_block = malloc(sizeof(struct block));
    new_block->memory = calloc(BLOCK_SIZE, sizeof(char));
    new_block->occupied = 0;
    new_block->prev = file->last_block;
    new_block->next = NULL;
    if (file->last_block) file->last_block->next = new_block;
    if (!file->block_list) file->block_list = new_block;
    file->last_block = new_block;
    file->block_count++;
    return new_block;
}

int
ufs_open(const char *filename, int flags) {
    if (file_descriptor_count == file_descriptor_capacity) encrease_fd();
    int fd = 0;
    for (; fd < file_descriptor_capacity; fd++) {
        if (!file_descriptors[fd]) break;
    }
    int create = flags % 2;
    int permission = flags - create;
    if (permission == 0) permission = UFS_READ_WRITE;

    struct file *current_file = file_list;
    for (;;) {
        if (current_file) {
            if (strcmp(current_file->name, filename) == 0) break;
            current_file = current_file->next;
        } else {
            if (create == 1) {
                current_file = add_file(filename);
                break;
            } else {
                ufs_error_code = UFS_ERR_NO_FILE;
                return -1;
            }
        }
    }

    file_descriptors[fd] = malloc(sizeof(struct filedesc));

    file_descriptors[fd]->file = current_file;
    file_descriptors[fd]->file->refs++;
    file_descriptors[fd]->current_block = current_file->block_list;
    file_descriptors[fd]->offset = 0;
    file_descriptors[fd]->permission = permission;

    file_descriptor_count++;
    ufs_error_code = UFS_ERR_NO_ERR;
    return fd + 1;
}

ssize_t
ufs_write(int fd, const char *buf, size_t size) {
    if (fd <= 0 || fd > file_descriptor_capacity || !file_descriptors[--fd]) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }
    struct filedesc *filedesc = file_descriptors[fd];
    struct file *file = filedesc->file;

    if (!(filedesc->permission == UFS_READ_WRITE || filedesc->permission == UFS_WRITE_ONLY)) {
        ufs_error_code = UFS_ERR_NO_PERMISSION;
        return -1;
    }

    if (!filedesc->current_block && file->block_list) filedesc->current_block = file->block_list;

    ssize_t writen_size = 0;
    for (;;) {
        if (writen_size == size) break;
        if (!filedesc->current_block) {
            if (file->block_count >= MAX_FILE_SIZE / BLOCK_SIZE) {
                ufs_error_code = UFS_ERR_NO_MEM;
                return -1;
            }

            filedesc->current_block = add_block(file);
            filedesc->offset = 0;
        }
        ssize_t size_write = BLOCK_SIZE - filedesc->offset;
        if (size_write <= 0) {
            filedesc->current_block = filedesc->current_block->next;
            filedesc->offset = 0;
            continue;
        }
        if (size_write > size - writen_size) size_write = size - writen_size;
        memcpy(filedesc->current_block->memory + filedesc->offset, buf + writen_size, size_write);
        writen_size += size_write;
        filedesc->offset += size_write;
        if (filedesc->current_block->occupied < filedesc->offset)
            filedesc->current_block->occupied = filedesc->offset;
    }

    ufs_error_code = UFS_ERR_NO_ERR;
    return writen_size;
}

ssize_t
ufs_read(int fd, char *buf, size_t size) {
    if (fd <= 0 || fd > file_descriptor_capacity || !file_descriptors[--fd]) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }
    struct filedesc *filedesc = file_descriptors[fd];

    if (!(filedesc->permission == UFS_READ_WRITE || filedesc->permission == UFS_READ_ONLY)) {
        ufs_error_code = UFS_ERR_NO_PERMISSION;
        return -1;
    }

    if (!filedesc->current_block && filedesc->file->block_list) filedesc->current_block = filedesc->file->block_list;

    ssize_t read_size = 0;
    for (;;) {
        if (read_size == size) break;
        if (!filedesc->current_block) break;
        ssize_t block_read_size = BLOCK_SIZE - filedesc->offset;
        if (block_read_size <= 0) {
            filedesc->current_block = filedesc->current_block->next;
            filedesc->offset = 0;
            continue;
        }
        if (block_read_size > size - read_size) block_read_size = size - read_size;
        if (block_read_size > filedesc->current_block->occupied - filedesc->offset)
            block_read_size = filedesc->current_block->occupied - filedesc->offset;
        if (block_read_size <= 0) break;
        memcpy(buf + read_size, filedesc->current_block->memory + filedesc->offset, block_read_size);
        read_size += block_read_size;
        filedesc->offset += block_read_size;
    }

    ufs_error_code = UFS_ERR_NO_ERR;
    return read_size;
}

int
ufs_close(int fd) {
    if (fd <= 0 || fd > file_descriptor_capacity || !file_descriptors[--fd]) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }
    struct filedesc *filedesc = file_descriptors[fd];

    filedesc->file->refs--;
    if (filedesc->file->removed == 1 && filedesc->file->refs == 0) free_file(filedesc->file);
    free(filedesc);
    file_descriptors[fd] = NULL;

    ufs_error_code = UFS_ERR_NO_ERR;
    return 0;
}

int
ufs_delete(const char *filename) {
    struct file *current_file = file_list;
    for (;;) {
        if (strcmp(current_file->name, filename) == 0) {
            if (current_file->prev) current_file->prev->next = current_file->next;
            else file_list = current_file->next;
            if (current_file->next) current_file->next->prev = current_file->prev;
            if (current_file->refs == 0) free_file(current_file);
            else current_file->removed = 1;
            ufs_error_code = UFS_ERR_NO_ERR;
            return 0;
        }
        if (current_file->next) current_file = current_file->next;
        else {
            ufs_error_code = UFS_ERR_NO_FILE;
            return -1;
        }
    }
}

int
ufs_resize(int fd, size_t new_size) {
    if (fd <= 0 || fd > file_descriptor_capacity || !file_descriptors[--fd]) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    if (new_size > MAX_FILE_SIZE) {
        ufs_error_code = UFS_ERR_NO_MEM;
        return -1;
    }

    struct filedesc *filedesc = file_descriptors[fd];
    struct file *file = filedesc->file;

    int new_block_count = new_size / BLOCK_SIZE;
    if (new_size % BLOCK_SIZE > 0) new_block_count++;

    for (int i = file->block_count; i < new_block_count; i++) {
        add_block(file);
    }
    for (int i = new_block_count; i < file->block_count; i++) {
        if (file->last_block->prev) {
            file->last_block = file->last_block->prev;
            free(file->last_block->next->memory);
            free(file->last_block->next);
            file->last_block->next = NULL;
        } else {
            free(file->last_block->memory);
            free(file->last_block);
            file->last_block = NULL;
            file->block_list = NULL;
        }
        file->block_count--;
        filedesc->current_block = file->last_block;
        filedesc->offset = BLOCK_SIZE;
    }

    return 0;
}
