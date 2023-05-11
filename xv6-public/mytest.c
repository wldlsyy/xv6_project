#include "types.h"
#include "user.h"
#include "stat.h"

int main(){
  int pid1, pid2;

  pid1 = fork();
  pid2 = fork();

  if (pid1 < 0) {
          printf(1, "fork failed\n");
          exit();
  }
  else if (pid1 == 0){ // 자식 프로세스 1
          int cpid = getpid();
          int n=0;
          setnice(cpid, 5); // 프로세스 1의 우선 순위를 높여줌
          while(n < 10000000000) {
                  n++;
          }
          exit();
  }

  if (pid2 < 0) {
          printf(1, "fork failed\n");
          exit();
  }
  else if (pid2 == 0){ // 자식 프로세스 2
          int cpid = getpid();
          int n=0;
          setnice(cpid, 30); // 프로세스 2의 우선 순위를 낮춰줌
          while(n < 10000000000) {
                n++;
          }
          exit();
  }

  // 부모 프로세스
  int n=0;
  while(n < 20000000000){
        n++;
        if (n % 1000000 == 0)
                ps(0);
  }
  exit();
}

