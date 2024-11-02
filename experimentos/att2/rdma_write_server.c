/*
 * modified from https://github.com/w180112/RDMA-example/tree/master
 * that modified from http://www.digitalvampire.org/rdma-tutorial-2007/notes.pdf
 * 
 * build:
 * gcc -o server rdma_write_server.c -lrdmacm -libverbs
 * 
 * usage: 
 * ./server 
 * 
 * waits for client to connect, receives two integers, and sends their sum back to the client.  
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <byteswap.h>
#include <rdma/rdma_cma.h> 

enum { 
    RESOLVE_TIMEOUT_MS = 5000,
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
		return 1;
	if (ibv_req_notify_cq(cq,0))
		return 1;
	if (ibv_poll_cq(cq,1,&wc) != 1)
		return 1;
	if (wc.status != IBV_WC_SUCCESS)
		return 1;

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

    /* In RDMA programming, transmission is an "asychronize" procedure, all the "events" were generated on NIC. 
     * Programmer should "get" those event and than ack and process them. 
     */

    /* In rdmacm lib, each event will generated by NIC and we should "get" these events from event channel, 
     * so we should create an event channel first. 
     */
    cm_channel = rdma_create_event_channel();
    if (!cm_channel) 
        return 1;

    /* Like socket fd in socket porgramming, we need to acquire a rdmacm id.
     */
    err = rdma_create_id(cm_channel,&listen_id,NULL,RDMA_PS_TCP); 
    if (err) 
        return err;

    /* Note: port 20000 doesn't equal to the socket port in TCP/IP, 
     * in RoCEv2, all of the packets use port 4791,
     * port 20000 here indicates a higher level abstraction port
     */
    sin.sin_family = AF_INET; 
    sin.sin_port = htons(20000);
    sin.sin_addr.s_addr = INADDR_ANY;

    
    /* Bind to local port and listen for connection request */
    /* Binding an indicated addr means rdmacm id will be bound to that RDMA device,
     * we bind wildcard addr here to make rdma_listen listen to all RDMA device
     */
    err = rdma_bind_addr(listen_id,(struct sockaddr *)&sin);
    if (err) 
        return 1;
    err = rdma_listen(listen_id,1);
    if (err)
        return 1;

    while(1) {
        /* We need to "get" rdmacm event to acquire event occured on NIC. */
        err = rdma_get_cm_event(cm_channel,&event);
        if (err)
            return err;

        if (event->event != RDMA_CM_EVENT_CONNECT_REQUEST)
        {
            printf("First message of the client was not an connection request! Aborting conneciton.\n");
            return 1;
        }
        cm_id = event->id;
        /* Each rdmacm event should be acked. */
        rdma_ack_cm_event(event);

        /* Create verbs objects now that we know which device to use */
        
        /* Allocate protection domain, each pd can be used to create queue pair, 
         * register memory regien, etc.
         * Each pd is a protection of a group of objects, 
         * it means you can't use these objects between different pd.
         */
        pd = ibv_alloc_pd(cm_id->verbs);
        if (!pd) 
            return 1;

        /* A completion event channel like rdma_create_event_channel in libibverbs */
        comp_chan = ibv_create_comp_channel(cm_id->verbs);
        if (!comp_chan)
            return 1;

        /* create a completion queue, a cq contains a completion work request. 
         * All the events about NIC, transmission will be in the cq 
         * Since libibverbs is thread-safe, use multiple cqs to 1 or many completion channels is avaliable.
         */
        cq = ibv_create_cq(cm_id->verbs,1,NULL,comp_chan,0); 
        if (!cq)
            return 1;

        /* Requests create compiletion notification when any work completion is add to the cq,
         * therefore work completion can be "get" by using ibv_get_cq_event() 
         */
        if (ibv_req_notify_cq(cq,0))
            return 1;

        buf = calloc(2,sizeof(uint32_t));
        if (!buf) 
            return 1;

        /* register a memory region with a specific pd */
        mr = ibv_reg_mr(pd,buf,2*sizeof(uint32_t), 
            IBV_ACCESS_LOCAL_WRITE | 
            IBV_ACCESS_REMOTE_READ | 
            IBV_ACCESS_REMOTE_WRITE); 
        if (!mr) 
            return 1;
        
        memset(&qp_attr,0,sizeof(qp_attr));
        qp_attr.cap.max_send_wr = 1;
        qp_attr.cap.max_send_sge = 1;
        qp_attr.cap.max_recv_wr = 1;
        qp_attr.cap.max_recv_sge = 1;

        qp_attr.send_cq = cq;
        qp_attr.recv_cq = cq;

        qp_attr.qp_type = IBV_QPT_RC;
        /* create a queue pair, a qp is for post send/receive.
         * If pd is NULL, rdma_create_qp will use default pd on RDMA device
         */
        err = rdma_create_qp(cm_id,pd,&qp_attr); 
        if (err) {
	        perror("rdma cm create qp error");
            return err;
	    }
        rep_pdata.buf_va = bswap_64((uintptr_t)buf); 
        /* we need to prepare remote key to give to client */
        rep_pdata.buf_rkey = htonl(mr->rkey); 
	    conn_param.responder_resources = 1;  
        conn_param.private_data = &rep_pdata; 
        conn_param.private_data_len = sizeof(rep_pdata);

        /* Accept connection, at this point, server will send the mr info and buffer addr to client to let client directly write data to it,
         * our example here is rep_pdata and conn_param
         */
        err = rdma_accept(cm_id,&conn_param); 
        if (err) 
            return 1;
        err = rdma_get_cm_event(cm_channel,&event);
        if (err) 
            return err;
        if (event->event != RDMA_CM_EVENT_ESTABLISHED)
            return 1;
        rdma_ack_cm_event(event);

        /* we need to check IBV_WR_RDMA_WRITE is done, so we post_recv at first */
        if (prepare_recv_notify_before_using_rdma_write(cm_id, pd))
            return 1;
        /* we need to check we already receive RDMA_WRITE done notification */
        if (check_notify_before_using_rdma_write(comp_chan, cq))
            return 1;

        /* Add two integers and send reply back */
	    printf("client write %d and %d ", ntohl(buf[0]), ntohl(buf[1]));
        buf[0] = htonl(ntohl(buf[0]) + ntohl(buf[1]));
        printf("sum of both numbers is: %d\n", ntohl(buf[0]));
        /* register post send, here we use IBV_WR_SEND */
        sge.addr = (uintptr_t)buf; 
        sge.length = sizeof(uint32_t); 
        sge.lkey = mr->lkey;
    
        send_wr.opcode = IBV_WR_SEND;
        send_wr.send_flags = IBV_SEND_SIGNALED;
        send_wr.sg_list = &sge;
        send_wr.num_sge = 1;

        if (ibv_post_send(cm_id->qp,&send_wr,&bad_send_wr)) 
            return 1;

        /* Like rdmacm, ibv events should be "get" than "ack".
         * Note that the "ack" procedure use mutex to make sure "ack" successfully,
         * so "ack" many events simultaneously can enhance performence
         */
		if (ibv_get_cq_event(comp_chan,&evt_cq,&cq_context))
			return 1;
		ibv_ack_cq_events(cq,1);
		if (ibv_req_notify_cq(cq,0))
			return 1;
		if (ibv_poll_cq(cq,1,&wc) != 1)
			return 1;
		if (wc.status != IBV_WC_SUCCESS)
			return 1;

        printf("Status of event: %d\n", wc.status);

	    err = rdma_get_cm_event(cm_channel,&event);
    	if (err)
        	return err;
	    rdma_ack_cm_event(event);

	    if (event->event == RDMA_CM_EVENT_DISCONNECTED) {
		    rdma_destroy_qp(cm_id);
		    ibv_dereg_mr(mr);
		    free(buf);
  		    err = rdma_destroy_id(cm_id);
		    if (err != 0)
			    perror("destroy cm id fail.");
	    }
    }
	rdma_destroy_event_channel(cm_channel);
    return 0;
}
