#include "utils.h"

const char* get_rdma_event(int event_num)
{
    static const char* rdma_event_strings[] = {
        "RDMA_CM_EVENT_ADDR_RESOLVED",
        "RDMA_CM_EVENT_ADDR_ERROR",
        "RDMA_CM_EVENT_ROUTE_RESOLVED",
        "RDMA_CM_EVENT_ROUTE_ERROR",
        "RDMA_CM_EVENT_CONNECT_REQUEST",
        "RDMA_CM_EVENT_CONNECT_RESPONSE",
        "RDMA_CM_EVENT_CONNECT_ERROR",
        "RDMA_CM_EVENT_UNREACHABLE",
        "RDMA_CM_EVENT_REJECTED",
        "RDMA_CM_EVENT_ESTABLISHED",
        "RDMA_CM_EVENT_DISCONNECTED",
        "RDMA_CM_EVENT_DEVICE_REMOVAL",
        "RDMA_CM_EVENT_MULTICAST_JOIN",
        "RDMA_CM_EVENT_MULTICAST_ERROR",
        "RDMA_CM_EVENT_ADDR_CHANGE",
        "RDMA_CM_EVENT_TIMEWAIT_EXIT"
    };
    if (event_num < 0 || event_num > 15)
        return "Not an valid RDMA event identifier!";

    return rdma_event_strings[event_num];
}
