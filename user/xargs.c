#include "kernel/param.h"
#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  const int MAX_LENGTH = 1024;
  char buf[MAX_LENGTH];

  if (argc < 2) {
    fprintf(2, "Usage: xargs <command> [args...]\n");
    exit(1);
  }

  while (gets(buf, MAX_LENGTH) > 0) {
    char *args[MAXARG];
    int arg_index, pid;

    if (buf[0] == '\0' || buf[0] == '\n') {
      // No more input, exit the loop.
      break;
    }

    // Add base command arguments.
    for (arg_index = 0; arg_index < argc - 1; arg_index++) {
      args[arg_index] = argv[arg_index + 1];
    }

    char *arg, *ptr;
    arg = buf;

    // Parse the input buffer into arguments.
    for (ptr = buf; *ptr != '\0'; ptr++) {
      if (*ptr == ' ' || *ptr == '\n' || *ptr == '\r' || *ptr == '\t' || *ptr == '\0') {
        *ptr = '\0';
        if (arg < ptr) {
          args[arg_index++] = arg;
        }
        arg = ptr + 1;
      }
    }

    // Handle the last argument if it exists.
    if (arg < ptr) {
      args[arg_index++] = arg;
    }
    args[arg_index++] = 0; // Null-terminate the argument list.

    // Fork a new process to execute the command.
    pid = fork();
    if (pid < 0) {
      fprintf(STDERR_FILENO, "xargs: fork failed\n");
      exit(1);
    } else if (pid == 0) {
      // Child process.
      exec(argv[1], args);
      fprintf(STDERR_FILENO, "xargs: exec %s failed\n", argv[1]);
      exit(1);
    } else {
      // Parent process.
      wait(0);
    }
  }

  exit(0);
}
