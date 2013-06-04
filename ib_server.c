/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *  Authors:
 *      tomykaira <tomykaira@gmail.com>
 */

#include <string.h>

/* item */
#include "memcached.h"
#include "ib.h"

/* ib.c */
int resource_destroy(resource_t *res);
int clear_cq(resource_t *res, int cq_flag);

void rdma_process_loop(resource_t *res, int verbose);

static void
decode_binary_set(uint8_t *in, char **key, uint *key_len, uint *data_len, char **data)
{
    int last = 0;
    uint8_t last_byte = in[6];
    *key_len = (uint)in[1];
    *data_len = (in[2] << 24) | (in[3] << 16) | (in[4] << 8) | in[5];
    last = 7 + *key_len + *data_len - 1;
    POLL_UNTIL(in[last] == last_byte);
    *key = (char *)in + 7;
    *data = (char *)in + 7 + *key_len;
}

static void
decode_binary_get(uint8_t *in, char **key, uint *key_len)
{
    int last = 0;
    uint8_t last_byte = in[6];
    *key_len = (uint)in[1];
    last = 7 + *key_len - 1;
    POLL_UNTIL(in[last] == last_byte);
    *key = (char *)in + 7;
}

static void
encode_get_response(uint data_len, char *data, uint8_t *out)
{
    out[0] = data_len  >> 24;
    out[1] = (data_len >> 16) & 0xff;
    out[2] = (data_len >>  8) & 0xff;
    out[3] = data_len & 0xff;
    if (data == NULL) {
        out[4] = 0;
    } else {
        out[4] = data[data_len-1];
        memcpy(out + 5, data, data_len);
    }
}

static int do_op_set(uint8_t *request)
{
    char *key, *data;
    uint nkey, vlen;
    item *it, *old_it;
    uint32_t hv;

    decode_binary_set(request, &key, &nkey, &vlen, &data);
    hv = hash(key, nkey, 0);

    /* key is memcpyed in do_item_alloc */
    it = item_alloc(key, nkey, 0, 0, vlen);
    old_it = do_item_get(key, nkey, hv);

    if (it == 0) {
        if (! item_size_ok(nkey, 0, vlen))
            fprintf(stderr, "SERVER_ERROR object too large for cache\n");
        else
            fprintf(stderr, "SERVER_ERROR out of memory storing object\n");

        /* Avoid stale data persisting in cache because we failed alloc.
         * Unacceptable for SET. Anywhere else too? */
        it = item_get(key, nkey);
        if (it) {
            item_unlink(it);
            item_remove(it);
        }

        return 1;
    }
    memcpy(ITEM_data(it), data, vlen);
    if (old_it != NULL) {
        item_replace(old_it, it, hv);
        do_item_remove(old_it);         /* release our reference */
    } else {
        item_link(it);
    }
    return 0;
}

static void do_op_get(uint8_t *request, uint8_t *response)
{
    char *key;
    uint nkey;
    item *it;

    decode_binary_get(request, &key, &nkey);
    /* key is memcpyed in do_item_alloc */
    it = item_get(key, nkey);
    if (it == 0) {
        encode_get_response(0, NULL, response);
    } else {
        encode_get_response(it->nbytes, ITEM_data(it), response);
    }
}

static void
rdma_response(resource_t *res)
{
    struct ibv_send_wr *bad_wr = NULL;
    TEST_Z(ibv_post_send(res->qp, res->send_wr, &bad_wr));
    res->scq_count ++;
    if (res->scq_count >= MAX_CQ_CAPACITY) {
        res->scq_count = clear_cq(res, SCQ_FLG);
    }
}

void rdma_process_loop(resource_t *res, int verbose)
{
    int opcode = 0;
    int stop = 0;

    if (verbose > 1)
        fprintf(stderr, "Starting thread\n");
    while (!stop) {
        POLL_UNTIL(res->in_buf[0] != 0);
        opcode = res->in_buf[0];
        res->in_buf[0] = 0;
        switch (opcode) {
        case OP_SET:
            if (verbose > 1)
                fprintf(stderr, "SET\n");
            res->out_buf[0] = do_op_set(res->in_buf);
            rdma_response(res);
            break;
        case OP_GET:
            if (verbose > 1)
                fprintf(stderr, "GET\n");
            do_op_get(res->in_buf, res->out_buf);
            rdma_response(res);
            break;
        case OP_STOP:
            if (verbose > 1)
                fprintf(stderr, "STOP\n");
            stop = 1;
            break;
        }
    }
    if (verbose > 1)
        fprintf(stderr, "stopping thread\n");
    resource_destroy(res);
}
