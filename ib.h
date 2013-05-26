/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#ifndef _IB_H_
#define _IB_H_

#include <infiniband/verbs.h>

#define IB_PORT 1
#define MAX_SQ_CAPACITY 20
#define MAX_RQ_CAPACITY 20
#define MAX_SGE_CAPACITY 20
#define MAX_CQ_CAPACITY 20
#define MAX_MR_NUM 10

/*exchange data struct for connectting QPs*/
struct qp_con_data_t{
    uint64_t addr;	/*Buffer address*/
    uint32_t rkey;	/*Remote key*/
    uint32_t qp_num;	/*QP number*/
    uint16_t lid;	/*LID of the IB port*/
    uint8_t gid[16];
};

struct rdma_pointer {
	uint64_t addr;
	uint32_t key;
	size_t len;
};

typedef struct resources {
    int ready;
    struct ibv_port_attr port_attr; /* IB port attributes */
    struct qp_con_data_t remote_props;
    struct ibv_context  *ib_ctx; /*device handle*/
    struct ibv_pd       *pd;
    struct ibv_cq       *scq;
    struct ibv_cq       *rcq;
    struct ibv_qp       *qp;
    struct ibv_mr       *local_mr;
    char                *local_data;
    struct rdma_pointer *remote_ptr;
    struct ibv_comp_channel *comp_ch;
    int    gid_idx;
} resource_t;

extern resource_t res;

#endif /* _IB_H_ */
