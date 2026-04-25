#include <sys/stat.h>
#include <errno.h>

int _stat(const char *path, struct stat *buf)
{
  (void)path;
  (void)buf;
  errno = ENOENT;
  return -1;
}
