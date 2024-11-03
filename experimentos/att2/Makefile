all:
	gcc -o client rdma_write_client.c utils.c -lrdmacm -libverbs
	gcc -o server rdma_write_server.c utils.c -lrdmacm -libverbs
	