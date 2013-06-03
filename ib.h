/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#ifndef _IB_H_
#define _IB_H_

#include <stdlib.h>
#include <stdio.h>
#include <infiniband/verbs.h>

#define IB_PORT 1
#define MAX_SQ_CAPACITY 20
#define MAX_RQ_CAPACITY 20
#define MAX_SGE_CAPACITY 20
#define MAX_CQ_CAPACITY 25
#define MAX_MR_NUM 10

/* identifier */
#define SCQ_FLG 1
#define RCQ_FLG 2

/* opcodes */
#define OP_SET 0x01
#define OP_GET 0x02
#define OP_STOP 0x03

/* communication buffer size */
#define BUF_SIZE 0x10000

/*exchange data struct for connectting QPs*/
struct qp_con_data_t{
    uint64_t addr;    /*Buffer address*/
    uint32_t rkey;    /*Remote key*/
    uint32_t qp_num;  /*QP number*/
    uint16_t lid;     /*LID of the IB port*/
    uint8_t gid[16];
};

struct remote_info {
    uint32_t qp_num;
    uint32_t lid;
    uint8_t  gid[16];
    uint64_t addr;
    uint32_t key;
};

typedef struct resource {
    struct ibv_port_attr port_attr;
    struct qp_con_data_t remote_props;
    struct ibv_context  *ib_ctx;
    struct ibv_pd       *pd;
    struct ibv_cq       *scq;
    struct ibv_cq       *rcq;
    struct ibv_qp       *qp;
    int    scq_count;
    int    gid_idx;

    uint8_t *in_buf, *out_buf;
    struct ibv_mr *in_mr, *out_mr;
    struct ibv_send_wr  *send_wr;
    struct ibv_sge      *send_sge;
    struct remote_info  *rinfo;
} resource_t;

#define TEST_Z(x)   do { if ( (x)) { fprintf(stderr, "TEST FAIL: %s failed (returned non-zero) (at %s:%d).\n", #x, __FILE__, __LINE__); exit(1); } } while (0)
#define TEST_NZ(x)  do { if (!(x)) { fprintf(stderr, "TEST FAIL: %s failed (returned zero/null) (at %s:%d).\n", #x, __FILE__, __LINE__); exit(1); } } while (0)
#define POLL_UNTIL(x) while (!(x)) { sched_yield(); }

#endif /* _IB_H_ */
