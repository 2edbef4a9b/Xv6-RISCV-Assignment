#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int pipe1[2]; // Parent to child.
  int pipe2[2]; // Child to parent.
  int pid;
  char sent_byte = '0';
  char received_byte;

  if (pipe(pipe1) < 0 || pipe(pipe2) < 0) {
    fprintf(STDERR_FILENO, "pingpong: pipe failed\n");
    exit(1);
  }

  pid = fork();
  if (pid < 0) {
    fprintf(STDERR_FILENO, "pingpong: fork failed\n");
    exit(1);
  } else if (pid == 0) { // Child process.
    // Close unused ends of pipes.
    if (close(pipe1[1]) < 0 || close(pipe2[0]) < 0) {
      fprintf(STDERR_FILENO, "pingpong: close failed\n");
      exit(1);
    }

    // Child reads from pipe1 and writes to pipe2.
    if (read(pipe1[0], &received_byte, 1) != 1) {
      fprintf(STDERR_FILENO, "pingpong: read failed\n");
      exit(1);
    }

    printf("%d: received ping\n", getpid());

    // Child sends a response back to parent.
    if (write(pipe2[1], &sent_byte, 1) != 1) {
      fprintf(STDERR_FILENO, "pingpong: write failed\n");
      exit(1);
    }

    // Close the pipes after use.
    if (close(pipe1[0]) < 0 || close(pipe2[1]) < 0) {
      fprintf(STDERR_FILENO, "pingpong: close failed\n");
      exit(1);
    }
  } else { // Parent process.
    // Close unused ends of pipes.
    if (close(pipe1[0]) < 0 || close(pipe2[1]) < 0) {
      fprintf(STDERR_FILENO, "pingpong: close failed\n");
      exit(1);
    }

    // Parent sends a byte to child.
    if (write(pipe1[1], &sent_byte, 1) != 1) {
      fprintf(STDERR_FILENO, "pingpong: write failed\n");
      exit(1);
    }

    // Parent reads response from child.
    if (read(pipe2[0], &received_byte, 1) != 1) {
      fprintf(STDERR_FILENO, "pingpong: read failed\n");
      exit(1);
    }

    printf("%d: received pong\n", getpid());

    // Close the pipes after use.
    if (close(pipe1[1]) < 0 || close(pipe2[0]) < 0) {
      fprintf(STDERR_FILENO, "pingpong: close failed\n");
      exit(1);
    }

    // Wait for child to finish.
    if (wait(0) < 0) {
      fprintf(STDERR_FILENO, "pingpong: wait failed\n");
      exit(1);
    }
  }

  exit(0);
}
