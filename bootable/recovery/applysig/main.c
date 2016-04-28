#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "applysig.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        printf("usage: %s <sig-file> <partition name>\n", argv[0]);
        return -3;
    }
    if (access(argv[1], F_OK) != 0){
      printf("Fail to access %s, %s", argv[1], strerror(errno));
      return errno;
    }
    /* So far only support bootimg and recovery partition */
    if (!(!strcmp(argv[2], "boot") || !strcmp(argv[2], "bootimg") || !strcmp(argv[2], "recovery"))){
      printf("%s partition is not supported\n", argv[2]);
      return -1;
    }
    return applysignature(argv[1], argv[2]);
}
