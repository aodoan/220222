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
    /*
    enum rdma_cm_event_type {
   53     RDMA_CM_EVENT_CONNECT_ERROR,
   54     RDMA_CM_EVENT_UNREACHABLE,
   55     RDMA_CM_EVENT_REJECTED,
   56     RDMA_CM_EVENT_ESTABLISHED,
   57     RDMA_CM_EVENT_DISCONNECTED,
   58     RDMA_CM_EVENT_DEVICE_REMOVAL,
   59     RDMA_CM_EVENT_MULTICAST_JOIN,
   60     RDMA_CM_EVENT_MULTICAST_ERROR,
   61     RDMA_CM_EVENT_ADDR_CHANGE,
   62     RDMA_CM_EVENT_TIMEWAIT_EXIT
    */

}