#include <stdio.h>
#include <string.h>
#include "userfs.h"

int main(void) {
    ufs_open("Hello", 1);
    ufs_open("Hello2", 1);
}