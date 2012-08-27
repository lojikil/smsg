/*
 * This is a little program used to retrieve a list of users present in smsg chat
 * It is used by rchat to simulate a multi-channel chat
 *
 * Usage: smsgwho host:port [options]
 * Available options:
 *  -f<char>	use <char> instead of ' '(space) to separate fields
 *  -r<char>	use <char> instead of '\n'(newline) to separate records
 *  -c		do not output memberlist but only the number of members
 *
 * returns 0 on success and something else on failure
 * The output consists of records containing these fields:
 * chat_nick which is prepended by @ and/or & just like inside chat
 * username@host
 *
 * This output can be used with sed/awk, for example to count the number of
 * chatting users in the server on port 16798 do this:
 * smsgwho localhost:16798|wc -l
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "smsg.h"	/* for PORT_DEFAULT AND MAGIC_WHO_ARG */
#include "ncmd.h"

#define DFL_HOST "localhost"
#define DFL_OFS ' '
#define DFL_ORS '\n'


//connects to port on *host (tcp/ip), fills in *peeraddr and returns socket fd or
//returns -1 on error
int connect_to_server(char *host, short int port, struct sockaddr_in *peeraddr)
{
	int fd;
//	struct protoent *prot_entry;
	struct hostent *host_entry;

	if((host_entry = gethostbyname(host)) == NULL) {
		fprintf(stderr, "unable to resolve host `%s' (%s)\n", host, hstrerror(h_errno));
		return -1;
	}
//	if((prot_entry = getprotobyname("tcp")) == NULL) {
//		fprintf(stderr, "error getting tcp protocol number\n");
//		return -1;
//	}
//	if((fd = socket(PF_INET, SOCK_DGRAM, prot_entry->p_proto)) == -1) {
	if((fd = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
		fprintf(stderr, "error opening client socket (%s)\n", strerror(errno));
		return -1;
	}
	peeraddr->sin_family = AF_INET;
	peeraddr->sin_port = htons(port);
	peeraddr->sin_addr = *((struct in_addr *)host_entry->h_addr);
	memset(peeraddr->sin_zero, '\0', 8);
	if(connect(fd, (struct sockaddr *)peeraddr, sizeof(struct sockaddr_in)) == -1) {
		fprintf(stderr, "error connecting to %s:%i (%s)\n", inet_ntoa(peeraddr->sin_addr), port, strerror(errno));
		close(fd);
		return -1;
	}

	return fd;
}


int main(int argc, char *argv[])
{
	char *rbuf = NULL, *sbuf = NULL, *tnick = NULL, *tinfo = NULL;
	long int rbuf_len = 0, sbuf_len = 0, tnick_len = 0, tinfo_len = 0;
	struct sockaddr_in peeraddr;
	int peerfd = -1, ti;
	char *tp;

	int port = PORT_DEFAULT, count_only = 0;
	char *host = NULL;
	char ofs = DFL_OFS, ors = DFL_ORS;

/* PART ONE: PARSE AND CONFIGURE */
	host = strdup(DFL_HOST);

	for(ti=1; ti<argc; ti++) {
		if(strstr(argv[ti], "-f") == argv[ti]) {
			if(strlen(argv[ti]) >= 3) ofs = argv[ti][2];
		} else if(strstr(argv[ti], "-r") == argv[ti]) {
			if(strlen(argv[ti]) >= 3) ors = argv[ti][2];
		} else if(strstr(argv[ti], "-c") == argv[ti]) {
			count_only = 1;
		} else {
			if((tp = rindex(argv[ti], ':')) != NULL) {
				if(atoi(tp+1) > 0) port = atoi(tp+1);
				*tp = '\0';
			}
			if(strlen(argv[ti]) > 0) {
				free(host);
				host = strdup(argv[ti]);
			}
		}
	}

/* PART TWO: CONNECT AND COMMUNICATE */
	if((peerfd = connect_to_server(host, port, &peeraddr)) == -1) exit(1);

	ncmd_set(&sbuf, &sbuf_len, C_WHO);
	ncmd_add(&sbuf, &sbuf_len, MAGIC_WHO_ARG, strlen(MAGIC_WHO_ARG));
	if((ti = sendbuf(peerfd, sbuf, sbuf_len, 0)) == -1) {
		fprintf(stderr, "error sending message (%s)\n", strerror(errno));
		close(peerfd);
		exit(2);
	}

	while(!ncmd_iscomplete(rbuf, rbuf_len)) {
		switch(recvbuf(peerfd, &rbuf, &rbuf_len, 0)) {
		case -1:
			fprintf(stderr, "error receiving data\n");
			exit(3);
			break;
		case 0:
			fprintf(stderr, "connection closed by peer\n");
			exit(3);
			break;
		}
	}

/* PART THREE: DISSECT AND OUTPUT */
	if(count_only) {
		printf("%i\n", ncmd_getnumargs(rbuf, rbuf_len) / 2);
	} else {
		//start at one, the first argument is not relevant for this program
		for(ti=1; ti<ncmd_getnumargs(rbuf, rbuf_len); ti+=2) {
			ncmd_getarg(rbuf, rbuf_len, &tnick, &tnick_len, ti, 1);
			ncmd_getarg(rbuf, rbuf_len, &tinfo, &tinfo_len, ti+1, 1);
			if(ti > 0) printf("%c", ors);
			printf("%s%c%s", tnick, ofs, tinfo);
		}
	}

	free(host);
	close(peerfd);
	return 0;
}
