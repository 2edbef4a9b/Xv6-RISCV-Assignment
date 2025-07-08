#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int ticks;

  if (argc < 2) {
    fprintf(STDERR_FILENO, "sleep: missing operand\n");
    exit(1);
  }

  ticks = atoi(argv[1]);
  if (ticks < 0) {
    ticks = 0;
  }
  sleep(ticks);

  exit(0);
}
