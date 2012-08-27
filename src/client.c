/*
 * this file contains functions related to the smsg client
 *
 * TODO: all functions should return -1 (and close socket) when an unexpected netcommand is received
 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>	/* for close() and getuid() */
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "smsg.h"
#include "ncmd.h"


//connects to port on *host (tcp/ip), fills in *peeraddr and returns socket fd or
//returns -1 on error
int connect_to_server(char *host, short int port, struct sockaddr_in *peeraddr)
{
	int fd;
//	struct protoent *prot_entry;
	struct hostent *host_entry;

	if((host_entry = gethostbyname(host)) == NULL) {
		error("unable to resolve host `%s' (%s)\n", host, hstrerror(h_errno));
		return -1;
	}
//	if((prot_entry = getprotobyname("tcp")) == NULL) {
//		error("error getting tcp protocol number\n");
//		return -1;
//	}
//	if((fd = socket(PF_INET, SOCK_DGRAM, prot_entry->p_proto)) == -1) {
	if((fd = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
		error("error opening client socket (%s)\n", strerror(errno));
		return -1;
	}
	peeraddr->sin_family = AF_INET;
	peeraddr->sin_port = htons(port);
	peeraddr->sin_addr = *((struct in_addr *)host_entry->h_addr);
	memset(peeraddr->sin_zero, '\0', 8);
	if(connect(fd, (struct sockaddr *)peeraddr, sizeof(struct sockaddr_in)) == -1) {
		error("error connecting to %s:%i (%s)\n", inet_ntoa(peeraddr->sin_addr), port, strerror(errno));
		close(fd);
		return -1;
	}

	return fd;
}


int sendmessage(char *msgdata, char *targethost, char *targetuser, short int port)
{
	int sendfd = -1;	//init to please gcc
	struct sockaddr_in peeraddr;
	struct passwd *user_pwd;
	char *rbuf = NULL, *sbuf = NULL, *targ = NULL;
	long int rbuf_len = 0, sbuf_len = 0, targ_len = 0;

	if((user_pwd = getpwuid(getuid())) == NULL) {
		error("error getting user information (%s)\n", strerror(errno));
		close(sendfd);
		return -1;
	}

	if((sendfd = connect_to_server(targethost, port, &peeraddr)) == -1) return -1;


	ncmd_set(&sbuf, &sbuf_len, ncmds[MSG].opcode);
	ncmd_add(&sbuf, &sbuf_len, user_pwd->pw_name, strlen(user_pwd->pw_name));
	if(targetuser != NULL)
		ncmd_add(&sbuf, &sbuf_len, targetuser, strlen(targetuser));
	else
		ncmd_add(&sbuf, &sbuf_len, NULL, 0);
	ncmd_add(&sbuf, &sbuf_len, msgdata, strlen(msgdata));
	if(sendbuf(sendfd, sbuf, sbuf_len, 0) == -1) {
		error("error sending message (%s)\n", strerror(errno));
	}

	switch(recvbuf(sendfd, &rbuf, &rbuf_len, 0)) {
	case -1: error("error receiving data (%s)\n", strerror(errno)); break;
	case 0: error("peer closed connection while data was expected\n"); break;
	default:
		if(ncmd_iscomplete(rbuf, rbuf_len)) {
			switch(ncmd_get(rbuf, rbuf_len)) {
			case ACK:
				say("succesfully sent message of %i bytes to", sbuf_len);
				if(targetuser != NULL) say(" %s@%s:%i\n", targetuser, inet_ntoa(peeraddr.sin_addr), port);
				else say(" %s:%i\n", inet_ntoa(peeraddr.sin_addr), port);
				break;
			case NAK:
				say("error delivering message of %i bytes to", sbuf_len);
				if(targetuser != NULL) say(" %s@%s:%i", targetuser, inet_ntoa(peeraddr.sin_addr), port);
				else say(" %s:%i\n", inet_ntoa(peeraddr.sin_addr), port);
				ncmd_getarg(rbuf, rbuf_len, &targ, &targ_len, 0, 1);
				say(" (%s)\n", targ);
				break;
			default:
				error("received unexpected command %s (expected ACK or NAK)\n", ncmd_getname(ncmd_get(rbuf, rbuf_len)));
			}
		}
	}

	close(sendfd);
	return 0;
}

int getmessage(short int port)
{
	int sendfd = -1;	//init to please gcc
	struct sockaddr_in peeraddr;
	struct passwd *user_pwd;
	char *rbuf = NULL, *sbuf = NULL, *targ = NULL;
	long int rbuf_len = 0, sbuf_len = 0, targ_len = 0;

	if((user_pwd = getpwuid(getuid())) == NULL) {
		error("error getting user information (%s)\n", strerror(errno));
		close(sendfd);
		return -1;
	}

	if((sendfd = connect_to_server(LOCAL_ADDRESS, port, &peeraddr)) == -1) return -1;


	ncmd_set(&sbuf, &sbuf_len, ncmds[GETQMSG].opcode);
	ncmd_add(&sbuf, &sbuf_len, user_pwd->pw_name, strlen(user_pwd->pw_name));
	if(sendbuf(sendfd, sbuf, sbuf_len, 0) == -1) {
		error("error sending message (%s)\n", strerror(errno));
	}

	switch(recvbuf(sendfd, &rbuf, &rbuf_len, 0)) {
	case -1: error("error receiving data (%s)\n", strerror(errno)); break;
	case 0: error("peer closed connection while data was expected\n"); break;
	default:
		if(ncmd_iscomplete(rbuf, rbuf_len)) {
			switch(ncmd_get(rbuf, rbuf_len)) {
			case QMSG:
				ncmd_getarg(rbuf, rbuf_len, &targ, &targ_len, 0, 1);
				if(atoi(targ) == 0) {
					say("There are no messages left on the queue.\n");
				} else {
					say("There are %i messages left on the queue.\n", atoi(targ));
				}
				if(ncmd_getnumargs(rbuf, rbuf_len) > 1) {
					ncmd_getarg(rbuf, rbuf_len, &targ, &targ_len, 2, 1);
					say("Queued message from %s", targ);
					ncmd_getarg(rbuf, rbuf_len, &targ, &targ_len, 3, 1);
					say("@%s sent on", targ);
					ncmd_getarg(rbuf, rbuf_len, &targ, &targ_len, 1, 1);
					say(" %s:\n", targ);
					ncmd_getarg(rbuf, rbuf_len, &targ, &targ_len, 4, 1);
					say("%s\n", targ);
				} else {
					say("There are no queued messages.\n");
				}
				break;
			case NAK:
				ncmd_getarg(rbuf, rbuf_len, &targ, &targ_len, 0, 1);
				say("error getting queued message (which does -not- mean there are none) (%s)\n", targ);
				break;
			default:
				error("received unexpected command %s (expected QMSG or NAK)\n", ncmd_getname(ncmd_get(rbuf, rbuf_len)));
			}
		}
	}

	close(sendfd);
	return 0;
}

int getlist(char *targethost, short int port)
{
	int sendfd = -1;	//init to please gcc
	struct sockaddr_in peeraddr;
	char *rbuf = NULL, *sbuf = NULL, *targ0 = NULL, *targ1 = NULL;
	long int rbuf_len = 0, sbuf_len = 0, targ0_len = 0, targ1_len = 0;
	int t, i;

	if((sendfd = connect_to_server(targethost, port, &peeraddr)) == -1) return -1;


	ncmd_set(&sbuf, &sbuf_len, ncmds[GETLIST].opcode);
	if(sendbuf(sendfd, sbuf, sbuf_len, 0) == -1) {
		error("error sending list request (%s)\n", strerror(errno));
	}

	switch(recvbuf(sendfd, &rbuf, &rbuf_len, 0)) {
	case -1: error("error receiving data (%s)\n", strerror(errno)); break;
	case 0: error("peer closed connection while data was expected\n"); break;
	default:
		if(ncmd_iscomplete(rbuf, rbuf_len)) {
			switch(ncmd_get(rbuf, rbuf_len)) {
			case LIST:
				ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 1);
				ncmd_getarg(rbuf, rbuf_len, &targ1, &targ1_len, 1, 1);
				say("server v%s running on %s at %s\n", targ0, targ1, inet_ntoa(peeraddr.sin_addr));
				say("mesg user\n");
				t = ncmd_getnumargs(rbuf, rbuf_len);
				if(t == 2) {
					say("no users logged in\n");
				} else {
					for(i=2; i<t; i+=2) {
						ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, i, 0);
						ncmd_getarg(rbuf, rbuf_len, &targ1, &targ1_len, i+1, 0);
						switch(targ1[0]) {
						case -1: say("  ?  %s\n", targ0); break;
						case 0: say("  n  %s\n", targ0); break;
						case 1: say("  y  %s\n", targ0); break;
						default: say(" huh %s\n", targ0); break;
						}
					}
				}
				break;
			case NAK:
				ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 1);
				say("error getting user listing (%s)\n", targ0);
				break;
			default:
				error("received unexpected command %s (expected LIST or NAK)\n", ncmd_getname(ncmd_get(rbuf, rbuf_len)));
			}
		} else say("got %i bytes\n", rbuf_len);
	}

	close(sendfd);
	return 0;
}

int sendprefs(char *tty, char beep, char mode, short int port)
{
	int sendfd = -1;	//init to please gcc
	struct sockaddr_in peeraddr;
	struct passwd *user_pwd;
	char *rbuf = NULL, *sbuf = NULL, *targ = NULL;
	long int rbuf_len = 0, sbuf_len = 0, targ_len = 0;

	if((user_pwd = getpwuid(getuid())) == NULL) {
		error("error getting user information (%s)\n", strerror(errno));
		close(sendfd);
		return -1;
	}

	if((sendfd = connect_to_server(LOCAL_ADDRESS, port, &peeraddr)) == -1) return -1;


	ncmd_set(&sbuf, &sbuf_len, ncmds[SETPREF].opcode);
	ncmd_add(&sbuf, &sbuf_len, user_pwd->pw_name, strlen(user_pwd->pw_name));
	if(tty == NULL)
		ncmd_add(&sbuf, &sbuf_len, "", 0);
	else
		ncmd_add(&sbuf, &sbuf_len, tty, strlen(tty));
	ncmd_add(&sbuf, &sbuf_len, &beep, 1);
	ncmd_add(&sbuf, &sbuf_len, &mode, 1);
	if(sendbuf(sendfd, sbuf, sbuf_len, 0) == -1) {
		error("error sending message (%s)\n", strerror(errno));
	}

	switch(recvbuf(sendfd, &rbuf, &rbuf_len, 0)) {
	case -1: error("error receiving data (%s)\n", strerror(errno)); break;
	case 0: error("peer closed connection while data was expected\n"); break;
	default:
		if(ncmd_iscomplete(rbuf, rbuf_len)) {
			switch(ncmd_get(rbuf, rbuf_len)) {
			case ACK:
				say("preferences set\n");
				break;
			case NAK:
				ncmd_getarg(rbuf, rbuf_len, &targ, &targ_len, 0, 1);
				say("error setting preferences (%s)\n", targ);
				break;
			default:
				error("received unexpected command %s (expected ACK or NAK)\n", ncmd_getname(ncmd_get(rbuf, rbuf_len)));
			}
		}
	}

/*


	ncmd_remove(&rbuf, &rbuf_len);

	ncmd_set(&sbuf, &sbuf_len, ncmds[GETPREF].opcode);
	ncmd_add(&sbuf, &sbuf_len, user_pwd->pw_name, strlen(user_pwd->pw_name));
	if(sendbuf(sendfd, sbuf, sbuf_len, 0) == -1) {
		error("error sending message (%s)\n", strerror(errno));
	}

	switch(recvbuf(sendfd, &rbuf, &rbuf_len, 0)) {
	case -1: error("error receiving data (%s)\n", strerror(errno)); break;
	case 0: error("peer closed connection while data was expected\n"); break;
	default:
		if(ncmd_iscomplete(rbuf, rbuf_len)) {
			switch(ncmd_get(rbuf, rbuf_len)) {
			case PREF:
				ncmd_getarg(rbuf, rbuf_len, &targ, &targ_len, 0, 1);
say("tty: `%s'", targ);
				ncmd_getarg(rbuf, rbuf_len, &targ, &targ_len, 1, 1);
say(" beep: %i", targ[0]);
				ncmd_getarg(rbuf, rbuf_len, &targ, &targ_len, 2, 1);
say(" mode: %i\n", targ[0]);
				break;
			case NAK:
				ncmd_getarg(rbuf, rbuf_len, &targ, &targ_len, 0, 1);
				say("error getting preferences (%s)\n", targ);
				break;
			default:
				error("received unexpected command %s (expected PREF or NAK)\n", ncmd_getname(ncmd_get(rbuf, rbuf_len)));
			}
		}
	}


*/

	close(sendfd);
	return 0;
}


//fixme: I think this prototype doesn't belong here but I don't know a better place at the moment
int chatter_func(int connfd, const char *initial_nick, const char *server_string, const struct chatopts_s *chatopts, char **reason);

int chatter(char *nick, char *targethost, short int port, const struct chatopts_s *chatopts)
{
	int sendfd = -1;	//init to please gcc
	struct sockaddr_in peeraddr;
	char *shost = (targethost ? targethost : "localhost");
	struct passwd *user_pwd;
	char *rbuf = NULL, *sbuf = NULL, *targ0 = NULL;
	long int rbuf_len = 0, sbuf_len = 0, targ0_len = 0;
	char *reasonptr = NULL;

	//fixme? should this use euid or uid?
	if((user_pwd = getpwuid(getuid())) == NULL) {
		error("error getting user information (%s)\n", strerror(errno));
		close(sendfd);
		return -1;
	}

	if((sendfd = connect_to_server(shost, port, &peeraddr)) == -1) return -1;

	ncmd_set(&sbuf, &sbuf_len, ncmds[CHATDOOR].opcode);
	ncmd_add(&sbuf, &sbuf_len, user_pwd->pw_name, strlen(user_pwd->pw_name));
	ncmd_add(&sbuf, &sbuf_len, (nick ? nick : user_pwd->pw_name), (nick ? strlen(nick) : strlen(user_pwd->pw_name)));
	if(sendbuf(sendfd, sbuf, sbuf_len, 0) == -1) {
		error("error sending chat join request (%s)\n", strerror(errno));
	}

	switch(recvbuf(sendfd, &rbuf, &rbuf_len, 0)) {
	case -1: error("error receiving data (%s)\n", strerror(errno)); break;
	case 0: error("peer closed connection while data was expected\n"); break;
	default:
		if(ncmd_iscomplete(rbuf, rbuf_len)) {
			switch(ncmd_get(rbuf, rbuf_len)) {
			case ACK:
				break;
			case NAK:
				ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 1);
				say("error joining chat (%s)\n", targ0);
				close(sendfd);
				return -1;
				break;
			default:
				error("received unexpected command %s (expected ACK or NAK)\n", ncmd_getname(ncmd_get(rbuf, rbuf_len)));
			}
		} else say("got %i bytes\n", rbuf_len);
	}
	ncmd_remove(&rbuf, &rbuf_len);

	if(chatter_func(sendfd, (nick ? nick : user_pwd->pw_name), shost, chatopts, &reasonptr) > -2) {
		ncmd_set(&sbuf, &sbuf_len, ncmds[CHATDOOR].opcode);
		ncmd_add(&sbuf, &sbuf_len, user_pwd->pw_name, strlen(user_pwd->pw_name));
		if(reasonptr) {
			ncmd_add(&sbuf, &sbuf_len, "", 0);
			ncmd_add(&sbuf, &sbuf_len, reasonptr, strlen(reasonptr));
			free(reasonptr); reasonptr = NULL;
		}
		if(sendbuf(sendfd, sbuf, sbuf_len, 0) == -1) {
			error("error sending chat leave request (%s)\n", strerror(errno));
		}

		switch(recvbuf(sendfd, &rbuf, &rbuf_len, 0)) {
		case -1: error("error receiving data (%s)\n", strerror(errno)); break;
		case 0: error("peer closed connection while data was expected\n"); break;
		default:
			if(ncmd_iscomplete(rbuf, rbuf_len)) {
				switch(ncmd_get(rbuf, rbuf_len)) {
				case ACK:
					break;
				case NAK:
					ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 1);
					say("error leaving chat...this should NOT happen (%s)\n", targ0);
					close(sendfd);
					return -1;
					break;
				default:
					error("received unexpected command %s (expected ACK or NAK)\n", ncmd_getname(ncmd_get(rbuf, rbuf_len)));
				}
			} else say("got %i bytes\n", rbuf_len);
		}
	}

	close(sendfd);
	return 0;
}
