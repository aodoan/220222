/*
    General functions
*/
#ifndef __RDMA_LIB__
#define __RDMA_LIB__
#include <stdio.h>
#include <string.h>

#define BUFSIZE 512

/**
 * @brief get the name of the event based on the enum number
 * @param event_num the int
 * @return string with the name of the event
 */
const char* get_rdma_event(int event_num);

#endif //__RDMA_LIB__