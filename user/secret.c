#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"


int
main(int argc, char *argv[])
{
  if(argc != 2){
    printf("Usage: secret the-secret\n");
    exit(1);
  }
  char *end = sbrk(PGSIZE*32);
  end = end + 9 * PGSIZE;
  strcpy(end, "my very very very secret pw is:   ");
  printf("Virtual address of the pattern var: %p\n", end);
  printf("Virtual address of the secret var: %p\n", end + 32);
  strcpy(end+32, argv[1]);
  printf("Pattern and secret in dec:\n");
  for(int i = 0; i < 40; i++){
    printf("%d ", (unsigned char)end[i]);
  }
  printf("\n");
  exit(0);
}

