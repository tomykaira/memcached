/* Simple client program for test */
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>

#include "client.h"

int write_safe(int fd, char *data, int len);
int read_safe(int fd, char **data);

struct addrinfo *resolve_host(char *host)
{
  struct addrinfo hints = { 0 }, *result;
  int s;

  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags    = 0;
  hints.ai_protocol = 0;

  s = getaddrinfo(host, "11211", &hints, &result);

  if (s != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
    return NULL;
  }

  return result;
}

int main(int argc, char *argv[])
{
  struct addrinfo *host, *rp;
  int sfd, len;
  char *recv_buf;
  char send_buf[BUFSIZE] = "stats\r\n";

  host = resolve_host(argc > 1 ? argv[1] : "localhost");

  if (!host) {
    exit(EXIT_FAILURE);
  }

  for (rp = host; rp != NULL; rp = rp->ai_next) {
    sfd = socket(rp->ai_family, rp->ai_socktype,
                 rp->ai_protocol);
    if (sfd == -1)
      continue;

    if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
      break;                  /* Success */

    close(sfd);
  }

  if (rp == NULL) {
    fprintf(stderr, "no server found\n");
    exit(EXIT_FAILURE);
  }

  freeaddrinfo(host);

  if (write_safe(sfd, send_buf, strlen(send_buf)) == -1)
    exit(EXIT_FAILURE);
  if ((len = read_safe(sfd, &recv_buf)) < 0) {
    exit(EXIT_FAILURE);
  }
  write_safe(1, recv_buf, len);

  close(sfd);
  free(recv_buf);

  return 0;
}
