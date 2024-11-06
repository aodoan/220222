#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <byteswap.h>
#include <rdma/rdma_cma.h>
#include "utils.h"

enum { 
    RESOLVE_TIMEOUT_MS = 500, 
    BUFSIZE = DEFAULT_BUF_SIZE,
}; 

struct pdata { 
    uint64_t	buf_va; 
    uint32_t	buf_rkey;
};

int prepare_send_notify_after_rdma_write(struct rdma_cm_id *cm_id, struct ibv_pd *pd)
{
    struct ibv_sge sge; 
    struct ibv_send_wr send_wr = { }; 
    struct ibv_send_wr *bad_send_wr; 

    uint8_t *buf = calloc(1, sizeof(uint8_t));
    struct ibv_mr *mr = ibv_reg_mr(pd, buf, sizeof(uint8_t), IBV_ACCESS_LOCAL_WRITE); 
    if (!mr) 
        return 1;
    
    sge.addr = (uintptr_t)buf; 
    sge.length = sizeof(uint8_t); 
    sge.lkey = mr->lkey;
    
    memset(&send_wr, 0, sizeof(send_wr));
    send_wr.wr_id = 2;
    send_wr.opcode = IBV_WR_SEND;
    send_wr.sg_list = &sge;
    send_wr.num_sge = 1;

    if (ibv_post_send(cm_id->qp, &send_wr, &bad_send_wr)) 
        return 1;

    return 0;
}

int main(int argc, char *argv[]) 
{
    struct pdata server_pdata;
    struct rdma_event_channel *cm_channel; 
    struct rdma_cm_id *cm_id; 
    struct rdma_cm_event *event;  
    struct rdma_conn_param conn_param = { };
    struct ibv_pd *pd; 
    struct ibv_comp_channel *comp_chan; 
    struct ibv_cq *cq; 
    struct ibv_cq *evt_cq; 
    struct ibv_mr *mr; 
    struct ibv_qp_init_attr qp_attr = { }; 
    struct ibv_sge sge; 
    struct ibv_send_wr send_wr = { }; 
    struct ibv_send_wr *bad_send_wr; 
    struct ibv_recv_wr recv_wr = { }; 
    struct ibv_recv_wr *bad_recv_wr; 
    struct ibv_wc wc; 
    void *cq_context; 
    struct addrinfo *res, *t; 
    struct addrinfo hints = { 
        .ai_family    = AF_INET,
        .ai_socktype  = SOCK_STREAM
    };
    int n; 
    uint32_t *buf; 
    int err;

    if (argc != 3)
    {
        printf("Usage: %s [server_address] [file]\n", argv[0]);
        exit(1);
    } 

    // Create event channel
    cm_channel = rdma_create_event_channel(); 
    if (!cm_channel)
    {  
        puts("Failed to create completion channel.");
        return 1; 
    }

    // Create rdma_cm_id
    err = rdma_create_id(cm_channel, &cm_id, NULL, RDMA_PS_TCP);
    if (err)
    {
        puts("Failed to acquire rdmacm id. Quitting.");
        return err;
    }

    // Resolve address
    n = getaddrinfo(argv[1], "9191", &hints, &res);
    if (n < 0)
    {
        printf("TCP port specified is being used. quitting.\n");
        return 1;
    }


    err = rdma_resolve_addr(cm_id, NULL, res->ai_addr, RESOLVE_TIMEOUT_MS);
    if (err)
    {
        puts("Could not resolve address.");
        return err;
    }

    err = rdma_get_cm_event(cm_channel, &event);
    if (err)
    {
        puts("could not get cm event");
        return err;
    }
    if (event->event != RDMA_CM_EVENT_ADDR_RESOLVED)
    {
        printf("Expected event: %s, got: %s",
            get_rdma_event(RDMA_CM_EVENT_ADDR_RESOLVED),
            get_rdma_event(event->event));
        return 1;
    }
    rdma_ack_cm_event(event);

    err = rdma_resolve_route(cm_id, RESOLVE_TIMEOUT_MS);
    if (err)
        return err;
    
    err = rdma_get_cm_event(cm_channel, &event);
    if (err)
        return err;
    if (event->event != RDMA_CM_EVENT_ROUTE_RESOLVED)
        return 1; 
    rdma_ack_cm_event(event);

    // Allocate protection domain and create completion queue
    pd = ibv_alloc_pd(cm_id->verbs); 
    if (!pd) 
        return 1;

    comp_chan = ibv_create_comp_channel(cm_id->verbs);
    if (!comp_chan) 
        return 1;

    cq = ibv_create_cq(cm_id->verbs, 2, NULL, comp_chan, 0); 
    if (!cq) 
        return 1;

    if (ibv_req_notify_cq(cq, 0))
        return 1;

    // Allocate memory for BUFSIZE elements
    buf = calloc(BUFSIZE, sizeof(uint32_t)); 
    
    if (!buf) 
        return 1;

    // Register memory region for BUFSIZE elements
    mr = ibv_reg_mr(pd, buf, BUFSIZE * sizeof(uint32_t), IBV_ACCESS_LOCAL_WRITE); 
    if (!mr) 
        return 1;

    // Initialize Queue Pair attributes
    qp_attr.cap.max_send_wr = 4; 
    qp_attr.cap.max_send_sge = 1;
    qp_attr.cap.max_recv_wr = 1; 
    qp_attr.cap.max_recv_sge = 1; 
    qp_attr.send_cq = cq;
    qp_attr.recv_cq = cq;
    qp_attr.qp_type = IBV_QPT_RC;

    // Create Queue Pair
    err = rdma_create_qp(cm_id, pd, &qp_attr);
    if (err)
        return err;

    // Set connection parameters and establish the connection
    conn_param.initiator_depth = 1;
    conn_param.retry_count = 7;
    err = rdma_connect(cm_id, &conn_param);
    if (err)
        return err;

    // Wait for connection to be established
    err = rdma_get_cm_event(cm_channel, &event);
    if (err)
    {
        printf("Error occurred while connecting\n");
        return err;
    }
    if (event->event != RDMA_CM_EVENT_ESTABLISHED)
    {
        printf("Expected event: %s, got: %s\n", 
            get_rdma_event(RDMA_CM_EVENT_ESTABLISHED),
            get_rdma_event(event->event));
        return 1;
    }

    // Receive memory information from the server
    memcpy(&server_pdata, event->param.conn.private_data, sizeof(server_pdata));
    rdma_ack_cm_event(event);

    int sentinel = 1;
    long int a;
    printf("0 0 to quit!\n");

    printf("Enter the first number: ");
    scanf("%ld", &a);
    if (a == -1)
    {
        sentinel = 0;
    }
    else
    {
        // Open the file in binary mode
        FILE *file = fopen(argv[2], "rb");
        if (!file) 
        {
            perror("Error opening file");
            return 1;
        }

        // Seek to the end to get the size of the file
        fseek(file, 0, SEEK_END);
        long int file_size = ftell(file);
        fseek(file, 0, SEEK_SET);  // Reset the file pointer to the start

        buf[0] = htonl(file_size);
        size_t bytes_read = fread(buf + 1, 1, file_size, file);
        for (int i = 0; i < 10; i++)
        {
            printf("%d -> %d\n", i+1, htonl(buf[i]));
        }

        // Prepare the RDMA write operation
        sge.addr = (uintptr_t)buf; 
        sge.length = BUFSIZE * sizeof(uint32_t);
        sge.lkey = mr->lkey;

        send_wr.wr_id = 1;
        send_wr.opcode = IBV_WR_RDMA_WRITE;
        send_wr.send_flags = IBV_SEND_SIGNALED;
        send_wr.sg_list = &sge;
        send_wr.num_sge = 1;
        send_wr.wr.rdma.rkey = ntohl(server_pdata.buf_rkey);
        send_wr.wr.rdma.remote_addr = bswap_64(server_pdata.buf_va);

        if (ibv_post_send(cm_id->qp, &send_wr, &bad_send_wr))
            return 1;

        // Wait for work completion
        int end_loop = 0;
        while (!end_loop)
        {
            if (ibv_get_cq_event(comp_chan, &evt_cq, &cq_context))
            {
                puts("Failed to get cq event.");
                return 1;
            }

            if (ibv_req_notify_cq(cq, 0))
            {
                puts("Failed to get the notification.");
                return 1;
            }

            if (ibv_poll_cq(cq, 1, &wc) != 1)
            {
                puts("failed to pull the wc");
                return 1;
            }

            if (wc.status != IBV_WC_SUCCESS)
            {
                puts("wc received is not success.");
                return 1;
            }

            switch (wc.wr_id)
            {
                case 0:
                    printf("All good!\n");
                    end_loop = 1;
                    break;

                case 1:
                    // Send notification after RDMA write is done
                    if (prepare_send_notify_after_rdma_write(cm_id, pd))
                    {
                        printf("Sending notification failed\n");
                        return 1;
                    }
                    break;

                default:
                    printf("Ending loop\n");
                    end_loop = 1;
                    break;
            }
        }
        ibv_ack_cq_events(cq, 2);
    }

    // Clean up and disconnect
    rdma_disconnect(cm_id);
    err = rdma_get_cm_event(cm_channel, &event);
    if (err)
        return err;

    rdma_ack_cm_event(event);
    rdma_destroy_qp(cm_id);
    ibv_dereg_mr(mr);
    free(buf);
    err = rdma_destroy_id(cm_id);
    if (err)  
    {
        perror("rdma_destroy_id");
        return err;
    }
    rdma_destroy_event_channel(cm_channel);
    return 0;
}
