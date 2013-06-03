/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *  Authors:
 *      tomykaira <tomykaira@gmail.com>
 */

#include "ib.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/** file scope variables **/
static char *base64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int stringify_my_info(resource_t *res, int verbose, char *response);
int connect_qp_with_received_info(resource_t *res, struct remote_info *rinfo, int verbose);
int resource_create(resource_t *res, int ib_port, int verbose);
int resource_destroy(resource_t *res);
void ib_read_bytes(char *str, int length, uint8_t *out);
int clear_cq(resource_t *res, int cq_flag);

static void
buffer_create(resource_t *res)
{
    TEST_NZ(res->in_buf  = calloc(BUF_SIZE, sizeof(char)));
    TEST_NZ(res->out_buf = calloc(BUF_SIZE, sizeof(char)));

    TEST_NZ(res->in_mr  = ibv_reg_mr(res->pd, res->in_buf,  BUF_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
    TEST_NZ(res->out_mr = ibv_reg_mr(res->pd, res->out_buf, BUF_SIZE, 0));

    TEST_NZ(res->send_wr  = calloc(1, sizeof(*res->send_wr)));
    TEST_NZ(res->send_sge = calloc(1, sizeof(*res->send_sge)));

    res->send_sge->addr   = (intptr_t)res->out_mr->addr;
    res->send_sge->length = res->out_mr->length;
    res->send_sge->lkey   = res->out_mr->lkey;

    res->send_wr->sg_list = res->send_sge;
    res->send_wr->num_sge = 1;
    res->send_wr->opcode  = IBV_WR_RDMA_WRITE;
}

static void
buffer_destroy(resource_t *res)
{
    if (res->send_wr) {
        free(res->send_wr);
        res->send_wr = NULL;
    }
    if (res->send_sge) {
        free(res->send_sge);
        res->send_sge = NULL;
    }
    if (res->in_mr) {
        ibv_dereg_mr(res->in_mr);
        res->in_mr = NULL;
    }
    if (res->out_mr) {
        ibv_dereg_mr(res->out_mr);
        res->out_mr = NULL;
    }
    if (res->in_buf) {
        free(res->in_buf);
        res->in_buf = NULL;
    }
    if (res->out_buf) {
        free(res->out_buf);
        res->out_buf = NULL;
    }
}

int
resource_create(resource_t *res, int ib_port, int verbose)
{
    struct ibv_device  **dev_list = NULL;
    struct ibv_qp_init_attr qp_init_attr;
    struct ibv_device  *ib_dev = NULL;
    char *dev_name = NULL;
    int  i;
    int  cq_size = MAX_CQ_CAPACITY;
    int  dev_numm;
    int  rc = 0;

    /* Init structure */
    memset(res, 0, sizeof(resource_t));

    /* Get the device list */
    dev_list = ibv_get_device_list(&dev_numm);
    if(!dev_list) {
        fprintf(stderr, "Failed to get IB devices list\n");
        return 1;
    }
    if(!dev_numm) {
        fprintf(stderr, "No IB device is found\n");
        rc = 1;
        goto err_exit;
    }
    if (verbose > 1)
        fprintf(stderr, "found %d IB device(s)\n", dev_numm);
    /* Open the requested device */
    for(i = 0; i < dev_numm; i ++){
        dev_name = strdup(ibv_get_device_name(dev_list[i]));
        if (verbose > 1)
            fprintf(stderr, "IB device name: %s\n", dev_name);
        ib_dev = dev_list[i];
        break;
    }
    if (!ib_dev){
        fprintf(stderr, "IB device %s wasn't found\n", dev_name);
        rc = 1;
        goto err_exit;
    }
    res->ib_ctx = ibv_open_device(ib_dev);
    if (!res->ib_ctx) {
        fprintf(stderr, "Failed to open device %s\n", dev_name);
        rc = 1;
        goto err_exit;
    }
    if (verbose > 1)
        fprintf(stderr, "IB context = %lx\n", (uintptr_t)res->ib_ctx);
    ibv_free_device_list(dev_list);
    dev_list = NULL;
    ib_dev = NULL;

    // query port properties
    rc = ibv_query_port(res->ib_ctx, ib_port, &res->port_attr);
    if (rc != 0){
        fprintf(stderr, "ibv_query_port on port %u failed\n", ib_port);
        goto err_exit;
    }

    /* Create a PD */
    res->pd = ibv_alloc_pd(res->ib_ctx);
    if (!res->pd){
        fprintf(stderr, "ibv_alloc_pd failed\n");
        rc = 1;
        goto err_exit;
    }

    /* Create send/recv CQ
     *  inputs:
     *    device handle
     *    CQ capacity
     *  Output:
     *    CQ handle
     */
    res->scq = ibv_create_cq(res->ib_ctx, cq_size, NULL, NULL, 0);
    res->rcq = ibv_create_cq(res->ib_ctx, cq_size, NULL, NULL, 0);
    if (!res->scq){
        fprintf(stderr, "failed to create SCQ with %u entries\n", cq_size);
        rc = 1;
        goto err_exit;
    }
    if (!res->rcq){
        fprintf(stderr, "failed to create RCQ with %u entries\n", cq_size);
        rc = 1;
        goto err_exit;
    }
    res->scq_count = 0;

    /* Create QP */
    // inputs:
    // PD
    // CQs for SQ,RQ
    // capacity of SQ,RQ
    // Outputs:
    // QP handle
    memset(&qp_init_attr, 0, sizeof(qp_init_attr));
    qp_init_attr.qp_type = IBV_QPT_RC;
    qp_init_attr.sq_sig_all = 1;
    qp_init_attr.send_cq = res->scq;
    qp_init_attr.recv_cq = res->rcq;
    // max SR/RR num in SQ/RQ
    qp_init_attr.cap.max_send_wr = MAX_SQ_CAPACITY ;
    qp_init_attr.cap.max_recv_wr = MAX_RQ_CAPACITY;
    // max SGE num
    qp_init_attr.cap.max_send_sge = MAX_SGE_CAPACITY;
    qp_init_attr.cap.max_recv_sge = MAX_SGE_CAPACITY;
    qp_init_attr.cap.max_inline_data = 256;

    res->qp = ibv_create_qp(res->pd, &qp_init_attr);
    if (!res->qp){
        fprintf(stderr, "failed to create QP\n");
        rc = 1;
        goto err_exit;
    }
    if (verbose > 1)
        fprintf(stderr, "QP was created, QP number=0x%x\n", res->qp->qp_num);

    buffer_create(res);

    /* EXIT */
 err_exit:
    if(rc){
        /* Error encountered, cleanup */
        if(res->qp){
            ibv_destroy_qp(res->qp);
            res->qp = NULL;
        }
        if(res->scq){
            ibv_destroy_cq(res->scq);
            res->scq = NULL;
        }
        if(res->rcq){
            ibv_destroy_cq(res->rcq);
            res->rcq = NULL;
        }
        if(res->pd){
            ibv_dealloc_pd(res->pd);
            res->pd = NULL;
        }
        if (res->ib_ctx) {
            ibv_close_device(res->ib_ctx);
            res->ib_ctx = NULL;
        }
        if (dev_list) {
            ibv_free_device_list(dev_list);
            dev_list = NULL;
        }
    }
    return rc;
}

int
resource_destroy(resource_t *res)
{
    int rc = 0;

    if (!res) return 0;

    buffer_destroy(res);

    /* Clear queue */
    if (res->scq_count > 0) {
        clear_cq(res, SCQ_FLG);
        res->scq_count = 0;
    }
    // Delete QP
    if (res->qp && ibv_destroy_qp(res->qp)){
        fprintf(stderr, "failed to destroy QP\n");
        rc = 1;
    }
    // Delete CQ
    if (res->scq && ibv_destroy_cq(res->scq)){
        fprintf(stderr, "failed to destroy SCQ\n");
        rc = 1;
    }
    if (res->rcq && ibv_destroy_cq(res->rcq)){
        fprintf(stderr, "failed to destroy RCQ\n");
        rc = 1;
    }
    // Deallocate PD
    if (res->pd && ibv_dealloc_pd(res->pd)){
        fprintf(stderr, "failed to deallocate PD\n");
        rc = 1;
    }
    if (res->ib_ctx && ibv_close_device(res->ib_ctx)){
        fprintf(stderr, "failed to close device context\n");
        rc = 1;
    }
    return rc;
}

void
ib_read_bytes(char *str, int length, uint8_t *out) {
    int i, j = 0;
    uint32_t v = 0;

    for (i = 0; str[i] != '=' && str[i] != '\0'; ++i) {
        v = (v << 6) | ((strchr(base64, str[i]) - base64) & 0x3f);

        if (i % 4 == 3) {
            for (j = 2; j >= 0; j--) {
                *out = (v >> (j*8)) & 0xff;
                out++;
            }
            v = 0;
        }
    }
}

static int encode_char(unsigned long bb, int srclen, char *dest, int j)
{
    int x, i, base;

    for ( i = srclen; i < 2; i++ )
        bb <<= 8;

    for ( base = 18, x = 0; x < srclen + 2; x++, base -= 6)
        dest[j++] = base64[ (bb>>base) & 0x3F ];

    for ( i = x; i < 4; i++ )
        dest[j++] = '=';

    return j;
}

static void ib_write_bytes(const uint8_t *src, int length, char *dest)
{
    unsigned long bb = (unsigned long)0;
    int     i = 0, j = 0;

    for (int c = 0; c < length; ++c) {
        bb <<= 8;
        bb |= (unsigned long)src[c];

        if (i == 2) {
            encode_char(bb, i, dest, j);

            j = j + 4;
            i = 0;
            bb = 0;
        } else
            i++;
    }

    if (i)
        j = encode_char(bb, i - 1, dest, j);

    dest[j] = '\0';
}

static int
gid_by_hostname()
{
    char hostname[256];
    if (gethostname(hostname, 256) == -1) {
        perror("gethostname");
        return -1;
    }
    return (uint8_t)hostname[2];
}

static int modify_qp_to_init(struct ibv_qp *qp, int ib_port)
{
    struct ibv_qp_attr attr;
    int flags;
    int rc;

    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_INIT;
    attr.port_num = ib_port;
    attr.pkey_index = 0;
    attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ
        | IBV_ACCESS_REMOTE_WRITE;
    flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
    rc = ibv_modify_qp(qp, &attr, flags);
    if (rc) {
        fprintf(stderr, "failed to modify QP state to INIT\n");
    }
    return rc;
}

static int
modify_qp_to_rtr(struct ibv_qp *qp, uint32_t remote_qpn, uint16_t dlid,
                 uint8_t *dgid, int ib_port, int gid_idx)
{
    struct ibv_qp_attr attr;
    int flags;
    int rc;

    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = IBV_MTU_1024;
    attr.dest_qp_num = remote_qpn;
    attr.rq_psn = 0;
    attr.max_dest_rd_atomic = 1;
    attr.min_rnr_timer = 12; /* Ready NAK timer, usually 12 is default value */
    attr.ah_attr.is_global = 0;
    attr.ah_attr.dlid = dlid;
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = ib_port;

    if (gid_idx >= 0){
        attr.ah_attr.is_global = 1;
        attr.ah_attr.port_num = 1;
        memcpy(&attr.ah_attr.grh.dgid, dgid, 16);
        attr.ah_attr.grh.flow_label = 0;
        attr.ah_attr.grh.hop_limit = 1;
        attr.ah_attr.grh.sgid_index = gid_idx;
        attr.ah_attr.grh.traffic_class = 0;
    }

    flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN
        | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
    rc = ibv_modify_qp(qp, &attr, flags);
    if (rc){
        fprintf(stderr, "failed to modify QP state to RTR\n");
    }
    return rc;
}

static int
modify_qp_to_rts(struct ibv_qp *qp)
{
    struct ibv_qp_attr attr;
    int flags;
    int rc;

    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = 20; /* timeout */
    attr.retry_cnt = 7;
    attr.rnr_retry = 7;
    attr.sq_psn = 0;
    attr.max_rd_atomic = 1;

    flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT
        | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
    rc = ibv_modify_qp(qp, &attr, flags);
    if (rc){
        fprintf(stderr, "failed to modify QP state to RTS\n");
    }
    return rc;
}

int stringify_my_info(resource_t *res, int verbose, char *response) {
    int rc = 0;
    union ibv_gid my_gid;
    int gid_idx = gid_by_hostname();
    char b64_gid[30];

    res->gid_idx = gid_idx;

    rc = ibv_query_gid(res->ib_ctx, IB_PORT, gid_idx, &my_gid);
    if (rc){
        fprintf(stderr, "could not get gid for port %d, index %d\n", IB_PORT, gid_idx);
        return rc;
    }

    if (verbose > 1) {
        fprintf(stderr, "qp_num(%d) lid(%d)\n",
                res->qp->qp_num, res->port_attr.lid);
        uint8_t *p = (uint8_t*) &my_gid;
        printf("Local GID = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
               p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
        fprintf(stderr, "remote_key(%d) remote_addr(%ld)\n", res->in_mr->rkey, (uintptr_t)res->in_mr->addr);
    }

    ib_write_bytes((uint8_t *)&my_gid, 16, b64_gid);;

    sprintf(response, "%u %u %s %lu %u",
            res->qp->qp_num,
            res->port_attr.lid,
            b64_gid,
            (uintptr_t)res->in_mr->addr,
            res->in_mr->rkey);
    return 0;
}

int connect_qp_with_received_info(resource_t *res, struct remote_info *rinfo, int verbose) {
    int rc = 0;

    res->rinfo = rinfo;

    if (verbose > 1) {
        uint8_t *p;
        fprintf(stderr, "remote_qp_num(%d) remote_lid(%d)\n", rinfo->qp_num, rinfo->lid);
        p = rinfo->gid;
        fprintf(stderr, "Remote GID = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
               p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
        fprintf(stderr, "remote_key(%u) remote_addr(%lu)\n", rinfo->key, rinfo->addr);
    }

    rc = modify_qp_to_init(res->qp, IB_PORT);
    if (rc){
        fprintf(stderr, "change QP state to INIT failed\n");
        return rc;
    }

    rc = modify_qp_to_rtr(res->qp,
                          rinfo->qp_num, rinfo->lid, rinfo->gid,
                          IB_PORT, res->gid_idx);
    if (rc){
        fprintf(stderr, "failed to modify QP state to RTR\n");
        return rc;
    }

    rc = modify_qp_to_rts(res->qp);
    if (rc) {
        fprintf(stderr, "failed to modify QP state to RTR\n");
        return rc;
    }

    res->send_wr->wr.rdma.remote_addr = rinfo->addr;
    res->send_wr->wr.rdma.rkey        = rinfo->key;

    return 0;
}

static struct ibv_wc *dummy_wc = NULL;

int clear_cq(resource_t *res, int cq_flag)
{
    int   rc = 0;
    struct ibv_cq *target = NULL;

    if (!dummy_wc) {
	dummy_wc = calloc(MAX_CQ_CAPACITY, sizeof(struct ibv_wc));
    }

    if(cq_flag == SCQ_FLG && res->scq != NULL) {
        target = res->scq;
    } else if (cq_flag == RCQ_FLG && res->rcq != NULL){
        target = res->rcq;
    }

    rc = ibv_poll_cq(target, MAX_CQ_CAPACITY, dummy_wc);
    if (rc < 0) {
	    fprintf(stderr, "ibv_poll_cq failed");
	    return rc;
    }
    return rc < MAX_CQ_CAPACITY ? MAX_CQ_CAPACITY - rc : 0;
}
