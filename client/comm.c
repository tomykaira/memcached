#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>

#include "client.h"

int write_safe(int fd, char *data, int len)
{
  int wrote_len = 0;
  while (wrote_len < len) {
    int ret = write(fd, (void*) data + wrote_len, len - wrote_len);

    if (ret == -1) {
      if (errno == EINTR || errno == EAGAIN)
        continue;
      perror("write_safe");
      return -1;
    }

    wrote_len += ret;
  }
  return 0;
}

int read_safe(int fd, char **data)
{
  int received_len = 0, current_size = BUFSIZE;
  char *buf = malloc(BUFSIZE);

  while (1) {
    int max = current_size - received_len;
    assert(max > 0);
    int ret = read(fd, buf + received_len, max);

    if (ret == -1) {
      if (errno == EINTR)
        continue;
      perror("read_safe");
      return -1;
    }

    received_len += ret;
    if (ret == max) {
      if (current_size <= received_len) {
        buf = realloc(buf, current_size*2);
        current_size *= 2;
      }
    }

    break;
  }

  *data = buf;
  return received_len;
}
