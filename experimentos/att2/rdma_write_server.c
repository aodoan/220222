#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <byteswap.h>
#include <rdma/rdma_cma.h> 
#include "utils.h"

enum { 
    RESOLVE_TIMEOUT_MS = 5000,
    BUFSIZE = DEFAULT_BUF_SIZE, 
};

struct pdata { 
    uint64_t buf_va; 
    uint32_t buf_rkey; 
};

int prepare_recv_notify_before_using_rdma_write(struct rdma_cm_id *cm_id, struct ibv_pd *pd)
{   
    uint8_t *buf = calloc(1, sizeof(uint8_t));
	struct ibv_mr *mr = ibv_reg_mr(pd, buf, sizeof(uint8_t), IBV_ACCESS_LOCAL_WRITE); 
	if (!mr) 
		return 1;

    struct ibv_sge notify_sge = {
        .addr = (uintptr_t)buf,
        .length = sizeof(uint32_t),
        .lkey = mr->lkey,
    };

    struct ibv_recv_wr notify_wr = {
        .wr_id = 0,
        .sg_list = &notify_sge,
        .num_sge = 1,
        .next = NULL,
    };

    struct ibv_recv_wr *bad_recv_wr;
    if (ibv_post_recv(cm_id->qp,&notify_wr,&bad_recv_wr))
		return 1;

    return 0;
}

int check_notify_before_using_rdma_write(struct ibv_comp_channel *comp_chan, struct ibv_cq *cq)
{
    struct ibv_wc           wc;
    struct ibv_cq           *evt_cq;
    void                    *cq_context;
	
    if (ibv_get_cq_event(comp_chan,&evt_cq,&cq_context))
    {
        printf("failed to get cq event\n");
		return 1;
    }
	if (ibv_req_notify_cq(cq,0))
    {
        printf("failed to get notification\n");
		return 1;
    }
	if (ibv_poll_cq(cq,1,&wc) != 1)
    {
        printf("failed to pool completion request\n");
		return 1;
    }
	if (wc.status != IBV_WC_SUCCESS)
    {
        printf("wc is not success\n");
		return 1;
    }

    return 0;
}

int main(int argc, char *argv[]) 
{ 
    struct pdata                rep_pdata;

    struct rdma_event_channel   *cm_channel;
    struct rdma_cm_id           *listen_id; 
    struct rdma_cm_id           *cm_id; 
    struct rdma_cm_event        *event; 
    struct rdma_conn_param      conn_param = { };

    struct ibv_pd               *pd; 
    struct ibv_comp_channel     *comp_chan; 
    struct ibv_cq               *cq;
    struct ibv_cq               *evt_cq;
    struct ibv_mr               *mr; 
    struct ibv_qp_init_attr     qp_attr = { };
    struct ibv_sge              sge; 
    struct ibv_send_wr          send_wr = { };
    struct ibv_send_wr          *bad_send_wr; 
    struct ibv_wc               wc;
    void                        *cq_context;
    struct sockaddr_in          sin;
    uint32_t                    *buf;
    int                         err;

    /* We use rdmacm lib to establish rdma connection and ibv lib to write, read, send, receive data here. */

    cm_channel = rdma_create_event_channel();
    if (!cm_channel) 
    {
        puts("Could not create event channel. You may not have the necessary RDMA set.");
        return 1;
    }

    err = rdma_create_id(cm_channel,&listen_id,NULL,RDMA_PS_TCP); 
    if (err) 
    {
        puts("error while acquiring rdmacm id.");
        return err;
    }
    sin.sin_family = AF_INET; 
    sin.sin_port = htons(9191);
    sin.sin_addr.s_addr = INADDR_ANY;

    printf("waiting for connection.\n");
    err = rdma_bind_addr(listen_id,(struct sockaddr *)&sin);
    if (err) 
    {
        return 1;
    } 
    err = rdma_listen(listen_id,1);
    if (err)
        return 1;

    err = rdma_get_cm_event(cm_channel,&event);
    if (err)
    {
        printf("error while getting rdma_get_cm_event: %d", err);
        return err;
    }
    if (event->event != RDMA_CM_EVENT_CONNECT_REQUEST)
    {
        printf("not an connection request.\n");
        return 1;
    }

    cm_id = event->id;
    rdma_ack_cm_event(event);

    pd = ibv_alloc_pd(cm_id->verbs);
    if (!pd) 
    {
        puts("error when allocating protection domain. quitting");
        return 1;
    }

    comp_chan = ibv_create_comp_channel(cm_id->verbs);
    if (!comp_chan)
    {
        puts("Error while creating completion channel.");
        return 1;
    }

    cq = ibv_create_cq(cm_id->verbs,1,NULL,comp_chan,0); 
    if (!cq)
    {
        puts("Erro while creating completion queue");
        return 1;
    }
    if (ibv_req_notify_cq(cq,0))
    {
        puts("could not fetch notifications on the completion queue, quitting.");
        return 1;
    }

    buf = calloc(BUFSIZE, sizeof(uint32_t)); // Receive BUFSIZE elements
    if (!buf) 
        return 1;

    mr = ibv_reg_mr(pd,buf,BUFSIZE*sizeof(uint32_t), 
        IBV_ACCESS_LOCAL_WRITE | 
        IBV_ACCESS_REMOTE_READ | 
        IBV_ACCESS_REMOTE_WRITE); 
    if (!mr) 
    {
        puts("memory region could not be registered. quitting");
        return 1;
    } 

    memset(&qp_attr,0,sizeof(qp_attr));
    qp_attr.cap.max_send_wr = 1;
    qp_attr.cap.max_send_sge = 1;
    qp_attr.cap.max_recv_wr = 1;
    qp_attr.cap.max_recv_sge = 1;
    qp_attr.send_cq = cq;
    qp_attr.recv_cq = cq;
    qp_attr.qp_type = IBV_QPT_RC;

    err = rdma_create_qp(cm_id,pd,&qp_attr); 
    if (err) {
	    perror("rdma cm create qp error");
        return err;
	}

    rep_pdata.buf_va = bswap_64((uintptr_t)buf); 
    rep_pdata.buf_rkey = htonl(mr->rkey); 
    conn_param.responder_resources = 1;  
    conn_param.private_data = &rep_pdata; 
    conn_param.private_data_len = sizeof(rep_pdata);

    err = rdma_accept(cm_id,&conn_param); 
    if (err) 
        return 1;

    err = rdma_get_cm_event(cm_channel,&event);
    if (err) 
        return err;
    if (event->event != RDMA_CM_EVENT_ESTABLISHED)
    {
        printf("Expected event: %s, got: %s\n",
        get_rdma_event(RDMA_CM_EVENT_ESTABLISHED),
        get_rdma_event(event->event));

        return 1;
    }
    rdma_ack_cm_event(event);


    if (prepare_recv_notify_before_using_rdma_write(cm_id, pd))
    {
        printf("Crashed\n");
        return 1;
    }
    if (check_notify_before_using_rdma_write(comp_chan, cq))
    {
        printf("Crashed 2\n");
        return 1;
    }

    // Now receive all BUFSIZE elements
    printf("Received the file with %d bytes!\n", ntohl(buf[0]));
    for (int i = 0; i < 10; i++)
    {
        printf("%d -> %d\n", i+1, htonl(buf[i]));
    }
    /*
    FILE* write_file = fopen("send_file.bin", "wb");
    if (!write_file)
    {
        printf("Could not write file, aborting.\n");
        exit(0);
    }
    */
    // Post send operation
    sge.addr = (uintptr_t)buf; 
    sge.length = sizeof(uint32_t); 
    sge.lkey = mr->lkey;
    
    send_wr.opcode = IBV_WR_SEND;
    send_wr.send_flags = IBV_SEND_SIGNALED;
    send_wr.sg_list = &sge;
    send_wr.num_sge = 1;

    if (ibv_post_send(cm_id->qp, &send_wr, &bad_send_wr)) 
        return 1;

    // Check for completion event
	if (ibv_get_cq_event(comp_chan, &evt_cq, &cq_context))
		return 1;
	ibv_ack_cq_events(cq, 1);
	if (ibv_req_notify_cq(cq, 0))
		return 1;
	if (ibv_poll_cq(cq, 1, &wc) != 1)
		return 1;
	if (wc.status != IBV_WC_SUCCESS)
    {
        puts("Work completion was not successful.");
		return 1;
    }
    printf("Status of event: %d\n", wc.status);
    
    // Clean up on disconnection
    err = rdma_get_cm_event(cm_channel,&event);
    if (err)
        return err;

    rdma_ack_cm_event(event);

    if (event->event == RDMA_CM_EVENT_DISCONNECTED) 
    {
        printf("End communication!\n");
        rdma_destroy_qp(cm_id);
        ibv_dereg_mr(mr);
        free(buf);
        err = rdma_destroy_id(cm_id);
        if (err != 0)
            perror("destroy cm id fail.");
    }

    rdma_destroy_event_channel(cm_channel);
    return 0;
}
