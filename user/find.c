#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

void
find(char *path, char *file)
{
  int fd;
  struct dirent dir_entry;
  struct stat file_stat;

  // Open the directory specified by path.
  fd = open(path, O_RDONLY);
  if (fd < 0) {
    fprintf(STDERR_FILENO, "find: cannot open %s\n", path);
    close(fd);
    return;
  }

  if (fstat(fd, &file_stat) < 0) {
    fprintf(STDERR_FILENO, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch (file_stat.type) {
  case T_DEVICE:
  case T_FILE:
    if (strcmp(path, file) == 0) {
      printf("%s\n", path);
    }
    close(fd);
    return;
  case T_DIR:
    char buf[512];
    char *ptr;

    // Construct the full path for the directory entries.
    strcpy(buf, path);
    ptr = buf + strlen(buf);
    *ptr++ = '/';

    while (read(fd, &dir_entry, sizeof(dir_entry)) == sizeof(dir_entry)) {
      // Skip empty directory entries.
      if (dir_entry.inum == 0) {
        continue;
      }

      // Skip the . and .. directory.
      if (strcmp(dir_entry.name, ".") == 0 || strcmp(dir_entry.name, "..") == 0) {
        continue;
      }

      // Construct the full path to the file.
      memmove(ptr, dir_entry.name, DIRSIZ);
      ptr[DIRSIZ] = 0;

      if (strcmp(dir_entry.name, file) == 0) {
        printf("%s\n", buf);
      }

      if (stat(buf, &file_stat) < 0) {
        fprintf(STDERR_FILENO, "find: cannot stat %s\n", buf);
        continue;
      }

      // If the entry is a directory, recursively search it.
      if (file_stat.type == T_DIR) {
        find(buf, file);
      }
    }
  }
}

int
main(int argc, char *argv[])
{
  char *path, *file;

  if (argc != 3) {
    fprintf(STDERR_FILENO, "Usage: find <directory> <filename>\n");
    exit(1);
  }

  path = argv[1];
  file = argv[2];

  find(path, file);

  exit(0);
}
