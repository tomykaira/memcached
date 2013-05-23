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
#include "../ib.h"

#define VERBOSE 2

/* comm.c */
int write_safe(int fd, char *data, int len);
int read_safe(int fd, char **data);

/* ib.c */
int stringify_my_info(resource_t *res, int verbose, char *response);
int connect_qp_with_received_info(resource_t *res, char **args, int verbose);
int resource_create(resource_t *res, int ib_port);
int resource_destroy(resource_t *res);

resource_t res;

static struct addrinfo *resolve_host(const char *host, const char *port)
{
  struct addrinfo hints = { 0 }, *result;
  int s;

  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags    = 0;
  hints.ai_protocol = 0;

  s = getaddrinfo(host, port, &hints, &result);

  if (s != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
    return NULL;
  }

  return result;
}

static int connect_memcached(const char *hostaddr, const char *port)
{
  struct addrinfo *host, *rp;
  int sfd;

  host = resolve_host(hostaddr, port);

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

  return sfd;
}

static int send_command(int sfd, const char *command)
{
  char send[1024], *recv;
  int len;

  sprintf(send, "%s\r\n", command);
  if (write_safe(sfd, send, strlen(send)) == -1) {
    return -1;
  }
  if ((len = read_safe(sfd, &recv)) < 0) {
    return -1;
  }
  write_safe(1, recv, len);
  free(recv);
  return 0;
}

static int connect_qp_client(resource_t *res, int sfd)
{
  char send[1024] = "setup_ib ";
  char *recv;
  char *args[3];
  int len;

  if (stringify_my_info(res, VERBOSE, send + strlen(send))) {
    fprintf(stderr, "Failed to stringify my info\n");
    return 1;
  }
  strcat(send, "\r\n");
  if (VERBOSE) {
    printf("Sending %s", send);
  }
  write_safe(sfd, send, strlen(send));
  len = read_safe(sfd, &recv);

  if (VERBOSE)
    write_safe(1, recv, len);

  args[0] = recv;
  args[1] = strchr(recv, ' ') + 1;
  *(args[1]-1) = '\0';
  args[2] = strchr(args[1], ' ') + 1;
  *(args[2]-1) = '\0';

  if (connect_qp_with_received_info(res, args, VERBOSE)) {
    fprintf(stderr, "Failed to connect qp\n");
    return 1;
  }

  free(recv);
  return 0;
}

static int request_rdma_set(resource_t *res, int sfd)
{
  struct ibv_mr *mr;
  char content[1024] = "Hello RDMA world!";
  char command[1024];

  mr = ibv_reg_mr(res->pd, content, strlen(content), IBV_ACCESS_REMOTE_READ);

  if (mr == 0) {
    fprintf(stderr, "failed to register MR\n");
    return 1;
  }

  if (VERBOSE) {
    printf("key %x addr %lx len %d\n", mr->rkey, (uintptr_t)mr->addr, (uint)strlen(content));
  }

  sprintf(command, "mset test 0 0 %u %lu %u", (uint)strlen(content), (uintptr_t)mr->addr, mr->rkey);
  send_command(sfd, command);

  ibv_dereg_mr(mr);

  return 0;
}

int main(int argc, char *argv[])
{
  int sfd = -1;
  int rc = EXIT_SUCCESS;

  sfd = connect_memcached(argc > 1 ? argv[1] : "localhost", argc > 2 ? argv[2] : "11211");
  if (sfd < 0) {
    rc = EXIT_FAILURE;
    goto end;
  }

  /* start local ib */
  if (resource_create(&res, IB_PORT) != 0) {
    fprintf(stderr, "Failed to initialize res\n");
    rc = EXIT_FAILURE;
    goto end;
  }

  if (connect_qp_client(&res, sfd) != 0) {
    fprintf(stderr, "Failed to connect qp\n");
    rc = EXIT_FAILURE;
    goto end;
  }

  /* send ib request */
  request_rdma_set(&res, sfd);
  send_command(sfd, "get test");

  /* let server to discard resource  */
  send_command(sfd, "disconnect_ib");

  send_command(sfd, "shutdown");

 end:
  resource_destroy(&res);
  close(sfd);

  return rc;
}
