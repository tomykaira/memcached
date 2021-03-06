/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *  Authors:
 *      tomykaira <tomykaira@gmail.com>
 */

#include "ib.h"
#include <string.h>

/* ib.c */
int clear_cq(resource_t *res, int cq_flag);
int resource_destroy(resource_t *res);

int client_set(resource_t *res, char *key, uint key_len, uint data_len, char *data);
void client_get(resource_t *res, char *key, uint key_len, uint *data_len, uint8_t **data);
void client_stop(resource_t *res);

/*
  opcode(1) key_length(1) data_length(4)
  last_byte(1) key... data...
 */

static void
encode_binary_set(char *key, uint key_len, uint data_len, char *data, uint8_t *out)
{
    out[0] = OP_SET;
    out[1] = key_len & 0xff;
    out[2] = data_len  >> 24;
    out[3] = (data_len >> 16) & 0xff;
    out[4] = (data_len >>  8) & 0xff;
    out[5] = data_len & 0xff;
    out[6] = data[data_len-1];
    memcpy(out + 7, key, key_len);
    memcpy(out + 7 + key_len, data, data_len);
}

static void
encode_binary_get(char *key, uint key_len, uint8_t *out)
{
    out[0] = OP_GET;
    out[1] = key_len & 0xff;
    out[2] = out[3] = out[4] = out[5] = 0;
    out[6] = key[key_len-1];
    memcpy(out + 7, key, key_len);
}

static void
encode_binary_stop(uint8_t *out)
{
    out[0] = OP_STOP;
}

static void
decode_get_response(uint8_t *in, uint *data_len, uint8_t **data)
{
    int last = 0;
    POLL_UNTIL(in[0] != 0xff);
    *data_len = (in[0] << 24) | (in[1] << 16) | (in[2] << 8) | in[3];
    in[0] = 0xff;
    if (*data_len == 0) {
        *data = NULL;
        return;
    }
    last = 5 + *data_len - 1;
    POLL_UNTIL(in[last] == in[4]);
    *data = in + 5;
}

static void
rdma_request(resource_t *res)
{
    struct ibv_send_wr *bad_wr = NULL;
    TEST_Z(ibv_post_send(res->qp, res->send_wr, &bad_wr));
    res->scq_count ++;
    if (res->scq_count >= MAX_CQ_CAPACITY) {
        res->scq_count = clear_cq(res, SCQ_FLG);
    }
}

int client_set(resource_t *res, char *key, uint key_len, uint data_len, char *data)
{
    int return_code;
    encode_binary_set(key, key_len, data_len, data, res->out_buf);
    rdma_request(res);
    POLL_UNTIL(res->in_buf[0] != 0xff);
    return_code = res->in_buf[0];
    res->in_buf[0] = 0xff;
    if (return_code != 0)
      fprintf(stderr, "ERROR in client_set\n");
    return return_code;
}

/*
  output: data_len, data
  'data' should be NULL or malloced buffer, and be freed after use
 */
void client_get(resource_t *res, char *key, uint key_len, uint *data_len, uint8_t **data)
{
    encode_binary_get(key, key_len, res->out_buf);
    rdma_request(res);
    decode_get_response(res->in_buf, data_len, data);
}

void client_stop(resource_t *res)
{
    encode_binary_stop(res->out_buf);
    rdma_request(res);
}
