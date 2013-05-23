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

resource_t res;

/** file scope variables **/
const static char B64TABLE[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/=";

int stringify_my_info(resource_t *res, int verbose, char *response);
int connect_qp_with_received_info(resource_t *res, char **args, int verbose);
int resource_create(resource_t *res, int ib_port, int verbose);
int resource_destroy(resource_t *res);

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

    if (res->ready)
        return 0;

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

    res->mr_list = malloc(sizeof(struct ibv_mr*) * MAX_MR_NUM);
    res->mr_size = 0;

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

    res->ready = 1;
    /* EXIT */
 err_exit:
    if(rc){
        /* Error encountered, cleanup */
        if(res->qp){
            ibv_destroy_qp(res->qp);
            res->qp = NULL;
        }
        if(res->mr_list && res->mr_size > 0){
            int i;
            for(i=0; i<res->mr_size; i++){
                ibv_dereg_mr(res->mr_list[i]);
                res->mr_list[i] = NULL;
            }
            free(res->mr_list);
        }
        if(res->scq){
            ibv_destroy_cq(res->scq);
            res->scq = NULL;
        }
        if(res->rcq){
            ibv_destroy_cq(res->rcq);
            res->rcq = NULL;
        }
        if(res->comp_ch){
            ibv_destroy_comp_channel(res->comp_ch);
            res->comp_ch = NULL;
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

    if (!res->ready)
        return 0;

    res->ready = 0;

    // Delete QP
    if (res->qp && ibv_destroy_qp(res->qp)){
        fprintf(stderr, "failed to destroy QP\n");
        rc = 1;
    }
    // Deregister MR
    if(res->mr_list && res->mr_size > 0){
        int i;
        for(i=0; i<res->mr_size; i++){
            ibv_dereg_mr(res->mr_list[i]);
        }
        free(res->mr_list);
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
    if(res->comp_ch && ibv_destroy_comp_channel(res->comp_ch)){
        fprintf(stderr, "failed to destroy Complete CH\n");
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

static unsigned int
ib_read_int(char *str) {
    unsigned int val;
    sscanf(str, "%x", &val);
    return val;
}

static void
ib_write_int(uint32_t val, char *str) {
    sprintf(str, "%08x ", val);
}

static void
ib_read_bytes(char *str, int original_length, uint8_t *out) {
    int i, j = 0;
    uint8_t i0, i1, i2, i3;
    for (i = 0; i < original_length; i += 4) {
        i0 = *(str + i + 0) == '=' ? 0 : (uint8_t)(strchr(B64TABLE, *(str + i + 0)) - B64TABLE);
        i1 = *(str + i + 1) == '=' ? 0 : (uint8_t)(strchr(B64TABLE, *(str + i + 1)) - B64TABLE);
        i2 = *(str + i + 2) == '=' ? 0 : (uint8_t)(strchr(B64TABLE, *(str + i + 2)) - B64TABLE);
        i3 = *(str + i + 3) == '=' ? 0 : (uint8_t)(strchr(B64TABLE, *(str + i + 3)) - B64TABLE);
        *(out + j + 0) = ((i0 & 0x3f) << 2) | (((i1 & 0x30)) >> 4);
        *(out + j + 1) = ((i1 & 0x0f) << 2) | (((i2 & 0x3c)) >> 4);
        *(out + j + 2) = ((i2 & 0x03) << 2) | (((i3 & 0x3f)) >> 4);
        j += 3;
    }
}

static void
ib_write_bytes(uint8_t *data, int original_length, char *out) {
    int i, j = 0;
    uint8_t i0, i1, i2, c0, c1, c2, c3;

    for (i = 0; i < original_length; i += 3) {
        i0 = (i + 0 < original_length) ? *(data + i + 0) : '\0';
        i1 = (i + 1 < original_length) ? *(data + i + 1) : '\0';
        i2 = (i + 2 < original_length) ? *(data + i + 2) : '\0';

        c0 = i0 >> 2;
        c1 = (i0 & 0x03) << 4 | (i1 >> 4);
        c2 = (i1 & 0x0f) << 2 | (i2 >> 6);
        c3 = i2 & 0x3f;

        *(out + j + 0) = (i + 0 < original_length) ? B64TABLE[c0] : '=';
        *(out + j + 1) = (i + 0 < original_length) ? B64TABLE[c1] : '=';
        *(out + j + 2) = (i + 1 < original_length) ? B64TABLE[c2] : '=';
        *(out + j + 3) = (i + 2 < original_length) ? B64TABLE[c3] : '=';
        j += 4;
    }
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
    struct ibv_qp_attr	attr;
    int		flags;
    int		rc;

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
    struct ibv_qp_attr	attr;
    int		flags;
    int		rc;

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
    }

    ib_write_int(res->qp->qp_num, response);
    ib_write_int(res->port_attr.lid, response + 9);
    ib_write_bytes((uint8_t *)&my_gid, 16, response + 18);

    return 0;
}

int connect_qp_with_received_info(resource_t *res, char **args, int verbose) {
    uint32_t remote_qp_num;
    uint16_t remote_lid;
    uint8_t  remote_gid[16];
    int rc = 0;

    remote_qp_num = ib_read_int(args[0]);
    remote_lid    = ib_read_int(args[1]);
    ib_read_bytes(args[2], 16, remote_gid);

    if (verbose > 1) {
        uint8_t *p;
        printf("remote_qp_num(%d) remote_lid(%d)\n", remote_qp_num, remote_lid);
        p = remote_gid;
        printf("Remote GID = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
               p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
    }

    rc = modify_qp_to_init(res->qp, IB_PORT);
    if (rc){
        fprintf(stderr, "change QP state to INIT failed\n");
        return rc;
    }

    rc = modify_qp_to_rtr(res->qp, remote_qp_num, remote_lid, remote_gid, IB_PORT, res->gid_idx);
    if (rc){
        fprintf(stderr, "failed to modify QP state to RTR\n");
        return rc;
    }

    rc = modify_qp_to_rts(res->qp);
    if (rc) {
        fprintf(stderr, "failed to modify QP state to RTR\n");
        return rc;
    }

    return 0;
}
