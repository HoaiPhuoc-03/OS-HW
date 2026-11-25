#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  char *args[] = { "echo", "hello", 0 };

  if(argc > 1){
    // chạy "vmprinttest x": exec thường, KHÔNG in page table
    exec("echo", args);
  } else {
    // chạy "vmprinttest": execp, có in page table
    execp("echo", args);
  }
  fprintf(2, "exec failed\n");
  exit(1);
}