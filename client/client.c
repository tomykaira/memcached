/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/* Simple client program for test */
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/time.h>

#include "client.h"
#include "../ib.h"

#define VERBOSE 2

#define TEST_MODE 0
#define BENCH_MODE 1
#define MODE BENCH_MODE
#define MAX_LENGTH 16000
#define GET_ONLY 0x100
#define SET_ONLY 0x101
#define BOTH     0x102
#define BENCH_TYPE GET_ONLY

/* comm.c */
int write_safe(int fd, char *data, int len);
int read_safe(int fd, char **data);
int read_at_least(int fd, char **data, int size);

/* ib.c */
int stringify_my_info(resource_t *res, int verbose, char *response);
int resource_create(resource_t *res, int ib_port);
int connect_qp_with_received_info(resource_t *res, struct remote_info *rinfo, int verbose);
int resource_destroy(resource_t *res);
void ib_read_bytes(char *str, int length, uint8_t *out);

/* ib_client.c */
int client_set(resource_t *res, char *key, uint key_len, uint data_len, char *data);
void client_get(resource_t *res, char *key, uint key_len, uint *data_len, uint8_t **data);
void client_stop(resource_t *res);

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
    char str_gid[128];
    int len;
    struct remote_info *rinfo = calloc(1, sizeof(struct remote_info));

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

    sscanf(recv, "%u %u %s %lu %u",
           &rinfo->qp_num,
           &rinfo->lid,
           str_gid,
           &rinfo->addr,
           &rinfo->key);

    ib_read_bytes(str_gid, strlen(str_gid), rinfo->gid);

    if (connect_qp_with_received_info(res, rinfo, VERBOSE)) {
        fprintf(stderr, "Failed to connect qp\n");
        return 1;
    }

    free(recv);
    return 0;
}

static double get_interval(struct timeval bt, struct timeval et)
{
    double b, e;

    b = bt.tv_sec + (double) bt.tv_usec * 1e-6;
    e = et.tv_sec + (double) et.tv_usec * 1e-6;
    return e - b;
}

static int bench_tcp(resource_t *res, int sfd, int size, int times)
{
    int i;
    char *data = calloc(size, sizeof(char));
    char *recv;
    struct timeval begin, end;
    double elapsed;
    char *command = calloc(size + 1024, sizeof(char));
    int header_length;
    int receive_len;

    sprintf(data, "abcabcabc");
    data[size - 1] = data[size - 2] = data[size - 3] = 'A';

    /* preset data */
    if (BENCH_TYPE == GET_ONLY) {
        for (int i = 0; i < 100; ++i) {
            sprintf(command, "set k%02d 0 0 %u\r\n", i % 100, (uint)size);
            header_length = strlen(command);
            memcpy(command + header_length, data, size);
            sprintf(command + header_length + size, "\r\n");
            if (write_safe(sfd, command, header_length + size + 2) == -1) {
                return -1;
            }
            if (read_safe(sfd, &recv) < 0) {
                return -1;
            }
            free(recv);
        }
    }

    gettimeofday(&begin, NULL);
    for (i = 1; i <= times; ++i) {
        if (BENCH_TYPE == SET_ONLY || BENCH_TYPE == BOTH) {
            sprintf(command, "set k%02d 0 0 %u\r\n", i % 100, (uint)size);
            header_length = strlen(command);
            memcpy(command + header_length, data, size);
            sprintf(command + header_length + size, "\r\n");
            if (write_safe(sfd, command, header_length + size + 2) == -1) {
                return -1;
            }
            if (read_safe(sfd, &recv) < 0) {
                return -1;
            }
            free(recv);
        }
        if (BENCH_TYPE == GET_ONLY || BENCH_TYPE == BOTH) {
            sprintf(command, "get k%02d\r\n", i % 100);
            if (write_safe(sfd, command, strlen(command)) == -1) {
                return -1;
            }
            if ((receive_len = read_at_least(sfd, &recv, size)) < 0) {
                return -1;
            }
            if (receive_len < size) {
                fprintf(stderr, "Short response: expected (%d) received (%d)\n", size, receive_len);
                return -1;
            }
            free(recv);
        }
    }
    gettimeofday(&end, NULL);
    elapsed = get_interval(begin, end);
    printf("tcp, %d, %d, %lf, %lf\n", size, times, elapsed, (double)times/elapsed);

    free(data);
    free(command);
    return 0;
}

static int bench_rdma(resource_t *res, int sfd, int size, int times)
{
    int i;
    uint8_t *recv_data = NULL;
    char *data = calloc(size, sizeof(char));
    struct timeval begin, end;
    double elapsed;
    char key[128] = "k00";
    uint recv_data_len;

    sprintf(data, "abcabcabc");
    data[size - 1] = data[size - 2] = data[size - 3] = 'A';

    if (BENCH_TYPE == GET_ONLY) {
        for (int i = 0; i < 100; ++i) {
            key[1] = i % 10 + '0';
            key[2] = i / 10 + '0';
            if (client_set(res, key, 3, size, data) != 0)
                return 1;
        }
    }

    gettimeofday(&begin, NULL);
    for (i = 1; i <= times; ++i) {
        key[1] = i % 10 + '0';
        key[2] = i / 10 + '0';
        if (BENCH_TYPE == SET_ONLY || BENCH_TYPE == BOTH) {
            if (client_set(res, key, 3, size, data) != 0)
                return 1;
        }
        if (BENCH_TYPE == GET_ONLY || BENCH_TYPE == BOTH) {
            client_get(res, key, 3, &recv_data_len, &recv_data);
        }
    }
    gettimeofday(&end, NULL);
    elapsed = get_interval(begin, end);
    printf("rdma, %d, %d, %lf, %lf\n", size, times, elapsed, (double)times/elapsed);

    free(data);
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
    res.in_buf[0] = 0xff;

    if (connect_qp_client(&res, sfd) != 0) {
        fprintf(stderr, "Failed to connect qp\n");
        rc = EXIT_FAILURE;
        goto end;
    }

    /* send ib request */
    if (MODE == TEST_MODE) {
        char key[] = "hello";
        char data[] = "Hello world";
        uint recv_data_len;
        uint8_t *recv_data = NULL;
        printf("Sending\n");
        client_set(&res, key, strlen(key), strlen(data), data);
        printf("Getting\n");
        client_get(&res, key, strlen(key), &recv_data_len, &recv_data);
        recv_data[recv_data_len] = '\0';
        printf("Response: %s(%d)\n", recv_data, recv_data_len);
        client_stop(&res);
        free(recv_data);
    } else {
        for (int size = 1000; size <= 16000; size += 1000) {
            bench_tcp(&res, sfd, size, 10000);
            bench_rdma(&res, sfd, size, 10000);
        }
        client_stop(&res);
    }

    send_command(sfd, "shutdown");

 end:
    resource_destroy(&res);
    close(sfd);

    return rc;
}
