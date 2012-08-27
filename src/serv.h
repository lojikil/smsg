//to be included by server.c and servchat.h and nothing else
#include <sys/socket.h>
#include "smsg.h"

struct sclient {
	int fd;
//	unsigned int errors;	//to count how many consecutive errors occurred
	struct sockaddr_in address;
	unsigned long int buflen;
	char *buf;
} clients[MAX_CLIENTS];


int handle_chatcmd(int cl_idx);
