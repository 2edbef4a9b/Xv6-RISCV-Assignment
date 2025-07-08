#include "kernel/param.h"
#include "kernel/types.h"
#include "user/user.h"

#define EOF -1

int
getchar()
{
  char input_char;
  if (read(STDIN_FILENO, &input_char, 1) != 1) {
    return -1; // EOF or error.
  }
  return input_char;
}

int
getarg(int max_length, char *buf)
{
  int input_char, length;
  char *ptr = buf;

  length = 0;
  while ((input_char = getchar()) != EOF) {
    if (input_char == '\n' || input_char == '\t' || input_char == '\r' || input_char == ' '
        || length >= max_length - 1) {
      break; // Stop on whitespace or reaching max length.
    }
    *ptr++ = (char)input_char;
    length++;
  }
  *ptr = '\0'; // Null-terminate the string.
  return input_char == EOF ? EOF : length;
}

int
main(int argc, char *argv[])
{
  const int MAX_LENGTH = 1024;
  char buf[MAX_LENGTH];
  char *args[MAXARG];
  int length, arg_index;

  if (argc < 2) {
    fprintf(2, "Usage: xargs <command> [args...]\n");
    exit(1);
  }

  for (arg_index = 0; arg_index < argc - 1; arg_index++) {
    args[arg_index] = argv[arg_index + 1];
  }

  while ((length = getarg(MAX_LENGTH, buf)) != EOF) {
    if (length == 0) {
      continue; // Skip empty input.
    }
    args[arg_index] = malloc(strlen(buf) + 1);
    strcpy(args[arg_index], buf);
    arg_index++;
    if (arg_index >= MAXARG) {
      fprintf(2, "xargs: too many arguments\n");
      exit(1);
    }
  }
  args[arg_index++] = 0;
  exec(argv[1], args);
  fprintf(STDERR_FILENO, "xargs: exec %s failed\n", argv[1]);

  exit(0);
}
