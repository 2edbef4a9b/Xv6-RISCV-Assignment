#include "kernel/types.h"
#include "user/user.h"

void
sieve(int input_pipe[2]) __attribute__((noreturn));

void
sieve(int input_pipe[2])
{
  int pid, prime, number;
  int output_pipe[2];

  // Close the unused write end of the input pipe in the child process first.
  close(input_pipe[1]);

  // Check if there is a prime number to read.
  if (read(input_pipe[0], &prime, sizeof(int)) == 0) {
    // No prime number found, close the read end of the input pipe and exit.
    close(input_pipe[0]);
    exit(0);
  }

  printf("prime %d\n", prime);

  pipe(output_pipe);
  pid = fork();
  if (pid < 0) {
    fprintf(STDERR_FILENO, "primes: fork failed\n");
    exit(1);
  } else if (pid == 0) { // Child process.
    // Release the input pipe as the child will not use it.
    close(input_pipe[0]);
    sieve(output_pipe);
  } else { // Parent process.
    // Close the unused read end of the output pipe in the parent process.
    close(output_pipe[0]);
    while (read(input_pipe[0], &number, sizeof(int)) > 0) {
      if (number % prime != 0) {
        // Send the number to the output pipe if it is not divisible by the prime.
        write(output_pipe[1], &number, sizeof(int));
      }
    }

    // Close the read end of the input pipe after reading all numbers.
    close(input_pipe[0]);

    // Close the write end of the output pipe to signal no more numbers.
    close(output_pipe[1]);

    // Wait for the child process to finish.
    wait(0);
    exit(0);
  }
}

int
main(int argc, char *argv[])
{
  int pid;
  int output_pipe[2];

  pipe(output_pipe);
  pid = fork();
  if (pid < 0) {
    fprintf(STDERR_FILENO, "primes: fork failed\n");
    exit(1);
  } else if (pid == 0) { // Child process.
    sieve(output_pipe);
  } else { // Parent process as number generator.
    // Close the unused read end of the output pipe in the parent process.
    close(output_pipe[0]);

    int number;
    for (number = 2; number <= 280; ++number) {
      // Send each number to the output pipe.
      write(output_pipe[1], &number, sizeof(int));
    }
    // Close the write end of the output pipe to signal no more numbers.
    close(output_pipe[1]);

    // Wait for the child process to finish processing.
    wait(0);
    exit(0);
  }

  exit(0);
}
