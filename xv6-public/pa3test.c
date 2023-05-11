#include "types.h"
#include "user.h"
#include "stat.h"
#include "fcntl.h"
#include "param.h"
#include "mmu.h"

int main(){
    int fd, cnt;
    uint anony_va, va_start;

    cnt = freemem();
    printf(1, "Current number of free memory pages: %d\n", cnt);

    // Test anonymous mapping using mmap()
    anony_va = mmap(0, 8192, PROT_READ, MAP_POPULATE|MAP_ANONYMOUS, -1, 0);
    if (anony_va == 0){
        printf(1, "Anonymous Mapping Failed\n");
        return -1;
    }
    printf(1, "Anonymous Mapping Succeed\n");

    // Test file mapping using mmap()
    fd = open("test.txt", O_WRONLY|O_CREATE); // create a file to write
    if(write(fd, "file mapping\n", 13) < 0){ 
        printf(1, "File write failed\n");
        close(fd);
        exit();
    }
    close(fd);

    if((fd = open("test.txt", O_RDONLY)) < 0){ // open the file
        printf(1, "File open failed\n");
        return -1;
    }

    va_start = mmap(2*PGSIZE, 8192, PROT_READ, MAP_POPULATE, fd, 0);
    if (va_start == 0){
        printf(1, "File Mapping Failed\n");
        return -1;
    }
    printf(1, "File Mapping Succeed\n");

    // Check free memory pages after file mapping
    cnt = freemem();
    printf(1, "Current number of free memory pages after file mapping: %d\n", cnt);

    // Now unmap corresponding mapped area
    if(((munmap(anony_va)) < 0)||((munmap(va_start)) < 0)){
        printf(1, "Unmap Failed\n");
        return -1;
    }
    printf(1, "Unmap Succeed\n");

    // Check free memory pages after file mapping
    cnt = freemem();
    printf(1, "Current number of free memory pages after unmapping file mapping: %d\n", cnt);

    close(fd); // close file
    exit();
}