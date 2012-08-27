/*
 * this file contains functions related to the smsg server
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "smsg.h"
#include "ncmd.h"
#include "serv.h"

#define error_weird_settty "unable to set tty preference...this shouldn't happen"
#define error_not_local "command only for local usage (make sure to use localhost)"
#define error_list_get "unable to get userlist (probably no utmp access)"
#define error_string_dup "error duplicating string (probably a lack of available memory)"
#define error_internal "internal error"
#define error_no_user_given "explicit username required"
#define error_no_such_user "user does not exist"
#define error_user_not_logged_in "user seems not to be logged in"
#define error_tty_open "unable to open user's terminal"
#define error_mesg_no "user is denying messages...sorry"
#define error_queue_get "error getting message from queue"
#define error_chat_banned "you're banned, get lost"
#define error_chat_notpresent "you're not present in chat so you can't leave either...(now that's logic :)"
#define error_chat_ispresent "you're already present in chat"
#define error_chat_nickinuse "nickname is already in use"
#define error_chat_peerclose "connection closed by peer"

static int ls_exitloop = 0;

//signal handler for: SIGPIPE, SIGTERM, SIGINT
static void ls_handle_signals(int sig)
{
	//set signal handler again?
	switch(sig) {
	case SIGPIPE:
		//maybe dump error?
		break;
	case SIGTERM: case SIGINT:
		ls_exitloop = 1;
		break;
	default:
		slog(0, "hey! why signal %i?", sig);
//		error("hey! why signal %i?\n", sig);
		//huh?
	}
}


static inline int ls_send_buffer(int s, const void *msg, size_t len, int flags)
{
	if(sendbuf(s, msg, len, flags) == -1) {
		slog(0, "error sending command (%s)", strerror(errno));
		return -1;
	}
	return 0;
}


/*
 * WARNING: non-default return values!!!!! -5 is normal, -1 is error and 0 or greater is a clients[] index to kick out
 */
int ls_handle_netcmd(unsigned int cl)
{
	const char yes = 1, no = 0;
	int retval = -5;
	char *sbuf = NULL, *targ0 = NULL, *targ1 = NULL;
	long int sbuf_len = 0, targ0_len = 0, targ1_len = 0;
	struct udata_ent *temp_udata;
	struct udata_ent tue; //make sure to re-init this to UDATA_DEF_ENTRY every time
	struct userlist_ent *userlist;
	unsigned char tmesg;
	char tbeep, tmode;
	char *tptr0, *tptr1, *tptr2, *tptr3;
	char tword[2];
	int tint;

//could save some idents by using break; after errors
	switch(ncmd_get(clients[cl].buf, clients[cl].buflen)) {
	case MSG:
	{
/*
<p> means proceed, <e> means exit
gotuser?<p>:<e>
existuser?<p>:<e>
beep||imm?
	gettty/acceptmsg?<p>:<e>/opentty/istty?<p>:<e>/fdtofile
imm/queue==imm?
	loggedinuser?
		printmsg
imm/queue?queue
	queuemsg
beep?beep
	beep
fileopened?
	close
sendback
*/
		int ttyfd;
		FILE *ttyfile;

		ncmd_getarg(clients[cl].buf, clients[cl].buflen, &targ0, &targ0_len, 0, 1);
		ncmd_getarg(clients[cl].buf, clients[cl].buflen, &targ1, &targ1_len, 1, 1);
		slog(1, "received message from %s@%s for %s", targ0, inet_ntoa(clients[cl].address.sin_addr), targ1);
		//if everything is okay, this string gets send back because it will not be overwritten
		ncmd_set(&sbuf, &sbuf_len, ACK);
		ncmd_getarg(clients[cl].buf, clients[cl].buflen, &targ0, &targ0_len, 1, 1);
		if(xstrlen(targ0) == 0) {
			ncmd_set(&sbuf, &sbuf_len, NAK);
			ncmd_add(&sbuf, &sbuf_len, error_no_user_given, xstrlen(error_no_user_given));
		} else if(getpwnam(targ0) == NULL) {	//actually this can indicate a memory lack too...but whatever
			ncmd_set(&sbuf, &sbuf_len, NAK);
			ncmd_add(&sbuf, &sbuf_len, error_no_such_user, xstrlen(error_no_such_user));
		} else if(!isloggedin(targ0)) {
			ncmd_set(&sbuf, &sbuf_len, NAK);
			ncmd_add(&sbuf, &sbuf_len, error_user_not_logged_in, xstrlen(error_user_not_logged_in));
		} else {
			//check for ttypref !=UDATA_TTYDEF (and validity), otherwise get first tty from userlist
			temp_udata = udata_get(targ0);
			tbeep = (temp_udata == NULL ? -1 : temp_udata->beep);
			if(temp_udata != NULL && !isontty(targ0, temp_udata->tty)) {
				temp_udata = NULL;
				tue = UDATA_DEF_ENTRY;
				tue.user = targ0;
				tue.tty = UDATA_TTYDEF;
				udata_update(&tue);
			}
			if(temp_udata == NULL || strcmp(temp_udata->tty, TTYSET_UNSET) == 0) {
				if((userlist = get_userlist(targ0)) == NULL) {
					ncmd_set(&sbuf, &sbuf_len, NAK);
					ncmd_add(&sbuf, &sbuf_len, error_list_get, xstrlen(error_list_get));
					ls_send_buffer(clients[cl].fd, sbuf, sbuf_len, 0);
					break;
				}
				if((tptr0 = strdup(userlist[0].tty)) == NULL) {
					free(userlist);
					ncmd_set(&sbuf, &sbuf_len, NAK);
					ncmd_add(&sbuf, &sbuf_len, error_string_dup, xstrlen(error_string_dup));
					ls_send_buffer(clients[cl].fd, sbuf, sbuf_len, 0);
					break;
				}
				free(userlist);
			} else {
				if((tptr0 = strdup(temp_udata->tty)) == NULL) {
					ncmd_set(&sbuf, &sbuf_len, NAK);
					ncmd_add(&sbuf, &sbuf_len, error_string_dup, xstrlen(error_string_dup));
					ls_send_buffer(clients[cl].fd, sbuf, sbuf_len, 0);
					break;
				}
			}
			//check if user is accepting messages
			if(!iswritabletty(tptr0)) {
				free(tptr0);
				ncmd_set(&sbuf, &sbuf_len, NAK);
				ncmd_add(&sbuf, &sbuf_len, error_mesg_no, xstrlen(error_mesg_no));
				ls_send_buffer(clients[cl].fd, sbuf, sbuf_len, 0);
				break;
			}
			//open tty
			if((ttyfd = open(tptr0, O_WRONLY|O_NOCTTY)) == -1) {
				free(tptr0);
				ncmd_set(&sbuf, &sbuf_len, NAK);
				ncmd_add(&sbuf, &sbuf_len, error_tty_open, xstrlen(error_tty_open));
				ls_send_buffer(clients[cl].fd, sbuf, sbuf_len, 0);
				break;
			}
			free(tptr0);
			//check if it's really a tty
			if(!isatty(ttyfd)) {
				close(ttyfd);
				ncmd_set(&sbuf, &sbuf_len, NAK);
				ncmd_add(&sbuf, &sbuf_len, error_tty_open, xstrlen(error_tty_open));
				ls_send_buffer(clients[cl].fd, sbuf, sbuf_len, 0);
				break;
			}
			//a stream is needed for fprintf
			if((ttyfile = fdopen(ttyfd, "w")) == NULL) {
				close(ttyfd);
				ncmd_set(&sbuf, &sbuf_len, NAK);
				ncmd_add(&sbuf, &sbuf_len, error_tty_open, xstrlen(error_tty_open));
				ls_send_buffer(clients[cl].fd, sbuf, sbuf_len, 0);
				break;
			}
			//write message (maybe filter trough nonprintable-char filter)
			ncmd_getarg(clients[cl].buf, clients[cl].buflen, &targ0, &targ0_len, 0, 1);
			ncmd_getarg(clients[cl].buf, clients[cl].buflen, &targ1, &targ1_len, 2, 1);
			fprintf(ttyfile, "\nmessage from %s@%s:\n%s\n", targ0, inet_ntoa(clients[cl].address.sin_addr), targ1);
			//beep if desired
			if((tbeep == -1 ? UDATA_BEEPDEF : tbeep) == 1) fprintf(ttyfile, "\007");
			//close tty
			fclose(ttyfile);	//is the fd (ttyfd) closed too?
		}
		ls_send_buffer(clients[cl].fd, sbuf, sbuf_len, 0);
		break;
	}
	case GETQMSG:
		ncmd_getarg(clients[cl].buf, clients[cl].buflen, &targ0, &targ0_len, 0, 1);
		if(!islocal(clients[cl].address.sin_addr)) {
			slog(1, "received queued message request from %s for %s (not local, denied)", inet_ntoa(clients[cl].address.sin_addr), targ0);
			ncmd_set(&sbuf, &sbuf_len, NAK);
			ncmd_add(&sbuf, &sbuf_len, error_not_local, xstrlen(error_not_local));
		} else if(getpwnam(targ0) == NULL) {	//actually this can indicate a memory lack too...but whatever
			slog(1, "received queued message request from %s for %s (no such user, denied)", inet_ntoa(clients[cl].address.sin_addr), targ0);
			ncmd_set(&sbuf, &sbuf_len, NAK);
			ncmd_add(&sbuf, &sbuf_len, error_no_such_user, xstrlen(error_no_such_user));
		} else {
			slog(1, "received queued message request from %s for %s", inet_ntoa(clients[cl].address.sin_addr), targ0);
			ncmd_getarg(clients[cl].buf, clients[cl].buflen, &targ0, &targ0_len, 0, 1);
			if(mq_getnum(targ0) == 0) {
				ncmd_set(&sbuf, &sbuf_len, QMSG);
				ncmd_add(&sbuf, &sbuf_len, "\0\0", 2);
			} else {
				if(mq_get(targ0, &tptr0, &tptr1, &tptr2, &tptr3, &tint) == -1) {
					ncmd_set(&sbuf, &sbuf_len, NAK);
					ncmd_add(&sbuf, &sbuf_len, error_no_such_user, xstrlen(error_queue_get));
				} else {
					ncmd_set(&sbuf, &sbuf_len, QMSG);
					store_ns(tword, tint);
					ncmd_add(&sbuf, &sbuf_len, tword, 2);
					ncmd_add(&sbuf, &sbuf_len, tptr0, xstrlen(tptr0));
					ncmd_add(&sbuf, &sbuf_len, tptr1, xstrlen(tptr1));
					ncmd_add(&sbuf, &sbuf_len, tptr2, xstrlen(tptr2));
					ncmd_add(&sbuf, &sbuf_len, tptr3, xstrlen(tptr3));
				}
			}
		}
		ls_send_buffer(clients[cl].fd, sbuf, sbuf_len, 0);
		break;
	case GETLIST:
		slog(1, "received list request from %s", inet_ntoa(clients[cl].address.sin_addr));
		if((userlist = get_userlist(NULL)) == NULL) {
			ncmd_set(&sbuf, &sbuf_len, NAK);
			ncmd_add(&sbuf, &sbuf_len, error_list_get, xstrlen(error_list_get));
		} else {
			ncmd_set(&sbuf, &sbuf_len, LIST);
			ncmd_add(&sbuf, &sbuf_len, VERSION, xstrlen(VERSION));
			ncmd_add(&sbuf, &sbuf_len, OS, xstrlen(OS));
			tint = 0;
			while(userlist[tint].count != 0) {
				ncmd_add(&sbuf, &sbuf_len, userlist[tint].name, xstrlen(userlist[tint].name));
				//-1 (unknown state) is sent too (on purpose)
				tmesg = iswritabletty(userlist[tint].tty);
				ncmd_add(&sbuf, &sbuf_len, &tmesg, 1);
				tint++;
			}
			free(userlist);
		}
		ls_send_buffer(clients[cl].fd, sbuf, sbuf_len, 0);
		break;
	case SETPREF:
		ncmd_getarg(clients[cl].buf, clients[cl].buflen, &targ0, &targ0_len, 0, 1);
		if(!islocal(clients[cl].address.sin_addr)) {
			slog(1, "received preferences from %s@%s (not local, denied)", targ0, inet_ntoa(clients[cl].address.sin_addr));
			ncmd_set(&sbuf, &sbuf_len, NAK);
			ncmd_add(&sbuf, &sbuf_len, error_not_local, xstrlen(error_not_local));
		} else if(getpwnam(targ0) == NULL) {	//actually this can indicate a memory lack too...but whatever
			slog(1, "received preferences from %s@%s (no such user, denied)", targ0, inet_ntoa(clients[cl].address.sin_addr));
			ncmd_set(&sbuf, &sbuf_len, NAK);
			ncmd_add(&sbuf, &sbuf_len, error_no_such_user, xstrlen(error_no_such_user));
		} else if(!isloggedin(targ0)) {
			slog(1, "received preferences from %s@%s (user not logged in, denied)", targ0, inet_ntoa(clients[cl].address.sin_addr));
			ncmd_set(&sbuf, &sbuf_len, NAK);
			ncmd_add(&sbuf, &sbuf_len, error_user_not_logged_in, xstrlen(error_user_not_logged_in));
		} else {
			slog(1, "received preferences from %s@%s", targ0, inet_ntoa(clients[cl].address.sin_addr));
			ncmd_getarg(clients[cl].buf, clients[cl].buflen, &targ0, &targ0_len, 2, 0);
			ncmd_getarg(clients[cl].buf, clients[cl].buflen, &targ1, &targ1_len, 3, 1);
			tbeep = targ0[0]; tmode = targ1[0];
			ncmd_getarg(clients[cl].buf, clients[cl].buflen, &targ0, &targ0_len, 0, 1);
			ncmd_getarg(clients[cl].buf, clients[cl].buflen, &targ1, &targ1_len, 1, 1);
			if(xstrlen(targ1) == 0) {
				free(targ1);		//ugly hack
				targ1_len = 0;		//  "   "
				targ1 = NULL;		//  "   "
			}
			errno = 0;
			tue = UDATA_DEF_ENTRY;
			tue.user = targ0;
			tue.tty = targ1;
			tue.beep = tbeep;
			tue.mode = tmode;
			if(udata_update(&tue) == -1) {
				ncmd_set(&sbuf, &sbuf_len, NAK);
				if(errno != 0) {
					tptr0 = strerror(errno);
					ncmd_add(&sbuf, &sbuf_len, tptr0, xstrlen(tptr0));
				} else {
					ncmd_add(&sbuf, &sbuf_len, error_weird_settty, xstrlen(error_weird_settty));
				}
			} else {
				ncmd_set(&sbuf, &sbuf_len, ACK);
			}
		}
		ls_send_buffer(clients[cl].fd, sbuf, sbuf_len, 0);
		break;
	case GETPREF:
		ncmd_getarg(clients[cl].buf, clients[cl].buflen, &targ0, &targ0_len, 0, 1);
		if(!islocal(clients[cl].address.sin_addr)) {
			slog(1, "received preferences request from %s for %s (not local, denied)", inet_ntoa(clients[cl].address.sin_addr), targ0);
			ncmd_set(&sbuf, &sbuf_len, NAK);
			ncmd_add(&sbuf, &sbuf_len, error_not_local, xstrlen(error_not_local));
		} else if(getpwnam(targ0) == NULL) {	//actually this can indicate a memory lack too...but whatever
			slog(1, "received preferences request from %s for %s (no such user, denied)", inet_ntoa(clients[cl].address.sin_addr), targ0);
			ncmd_set(&sbuf, &sbuf_len, NAK);
			ncmd_add(&sbuf, &sbuf_len, error_no_such_user, xstrlen(error_no_such_user));
		} else {
			slog(1, "received preferences request from %s for %s", inet_ntoa(clients[cl].address.sin_addr), targ0);
			ncmd_getarg(clients[cl].buf, clients[cl].buflen, &targ0, &targ0_len, 0, 1);
			temp_udata = udata_get(targ0);
			ncmd_set(&sbuf, &sbuf_len, PREF);
			if(temp_udata != NULL) {
				ncmd_add(&sbuf, &sbuf_len, temp_udata->tty, xstrlen(temp_udata->tty));
				ncmd_add(&sbuf, &sbuf_len, &(temp_udata->beep), 1);
				ncmd_add(&sbuf, &sbuf_len, &(temp_udata->mode), 1);
			} else {
				ncmd_add(&sbuf, &sbuf_len, TTYSET_UNSET, xstrlen(TTYSET_UNSET));
				tbeep = -1; tmode = -1;
				ncmd_add(&sbuf, &sbuf_len, &tbeep, 1);
				ncmd_add(&sbuf, &sbuf_len, &tmode, 1);
			}
		}
		ls_send_buffer(clients[cl].fd, sbuf, sbuf_len, 0);
		break;
	case CHATDOOR:
//fixme: the the code of lcs_dist_ncmd() is used twice here...replace it?
	{
		struct udata_ent *tuent = NULL;
		struct udata_ent nue = UDATA_DEF_ENTRY;
		ncmd_getarg(clients[cl].buf, clients[cl].buflen, &targ0, &targ0_len, 0, 1);
		nue.user = targ0;	//this is also used on nick setting etc...don't change targ0!!
		if((tuent = udata_get(targ0)) == NULL) {
			udata_add(&nue);
			tuent = udata_get(targ0);
		}
		//fixme: badly structured if's reside here...
		if(!tuent) {	//creation of entry went wrong...bad luck
				//slog...
				ncmd_set(&sbuf, &sbuf_len, NAK);
				ncmd_add(&sbuf, &sbuf_len, error_internal, xstrlen(error_internal));
//fixme: This is really ugly!
		} else if(ncmd_getnumargs(clients[cl].buf, clients[cl].buflen) == 1 ||
		          (ncmd_getnumargs(clients[cl].buf, clients[cl].buflen) > 1 &&
			   ncmd_getarg(clients[cl].buf, clients[cl].buflen, &targ1, &targ1_len, 1, 1) != -1 && targ1_len == 1)) {
			if(!(tuent->flags & UDC_FLAG_PRESENT)) {
				slog(1, "%s wanted to leave chat but wasn't present", inet_ntoa(clients[cl].address.sin_addr));
				ncmd_set(&sbuf, &sbuf_len, NAK);
				ncmd_add(&sbuf, &sbuf_len, error_chat_notpresent, xstrlen(error_chat_notpresent));
			} else {
				int i;
				struct udata_ent *tud;
				if(ncmd_getnumargs(clients[cl].buf, clients[cl].buflen) == 3) {
					ncmd_getarg(clients[cl].buf, clients[cl].buflen, &targ1, &targ1_len, 2, 1);
					slog(1, "%s(%s) with nick %s left chat (%s)", tuent->user, inet_ntoa(clients[cl].address.sin_addr), tuent->nick, targ1);
				} else {
					slog(1, "%s(%s) with nick %s left chat", tuent->user, inet_ntoa(clients[cl].address.sin_addr), tuent->nick);
				}

				//start of separate sbuf and sbuf_len usage
				//this can safely use sbuf and sbuf_len because they are sent before overwritten
				ncmd_set(&sbuf, &sbuf_len, C_DOOR);
				ncmd_add(&sbuf, &sbuf_len, &no, 1);	//no is: 'const char no = 0;'
				ncmd_add(&sbuf, &sbuf_len, tuent->nick, xstrlen(tuent->nick));
				if(targ1) ncmd_add(&sbuf, &sbuf_len, targ1, targ1_len);

				for(i=0; i<MAX_CLIENTS; i++) {
					if(clients[i].fd > 0 && (tud = udata_get_by_fd(clients[i].fd)) &&
					   (tud->flags&UDC_FLAG_PRESENT) && i != cl) {
						if(sendbuf(clients[i].fd, sbuf, sbuf_len, 0) == -1) {
							slog(0, "error sending message to %s (%s)", inet_ntoa(clients[i].address.sin_addr), strerror(errno));
						}
					}
				}
				//end of separate sbuf and sbuf_len usage

				//fixme? set nick to NULL?
				nue.fd = -1;
				//fixme: errorcheck for udata_update()
				udata_update(&nue);
				udata_flagunset(targ0, UDC_FLAG_PRESENT | UDC_FLAG_AWAY | UDC_FLAG_OPERATOR | UDC_FLAG_SUPERUSER);
				ncmd_set(&sbuf, &sbuf_len, ACK);
			}
		} else {	//ignore the possibility that there are more than two arguments
			//this getarg must be here because it's used in one of the if's
			ncmd_getarg(clients[cl].buf, clients[cl].buflen, &targ1, &targ1_len, 1, 1);
			if(tuent->flags & UDC_FLAG_BANNED) {
				slog(1, "%s(%s) wanted to join chat with nick %s but was banned", tuent->user, inet_ntoa(clients[cl].address.sin_addr), tuent->nick);
				ncmd_set(&sbuf, &sbuf_len, NAK);
				ncmd_add(&sbuf, &sbuf_len, error_chat_banned, xstrlen(error_chat_banned));
			} else if(tuent->flags & UDC_FLAG_PRESENT) {
				slog(1, "%s(%s) wanted to join chat with nick %s but was already present", tuent->user, inet_ntoa(clients[cl].address.sin_addr), tuent->nick);
				ncmd_set(&sbuf, &sbuf_len, NAK);
				ncmd_add(&sbuf, &sbuf_len, error_chat_ispresent, xstrlen(error_chat_ispresent));
			} else if(udata_get_by_nick(targ1)) {
				slog(1, "%s(%s) wanted to join chat with nick %s which is already in use", tuent->user, inet_ntoa(clients[cl].address.sin_addr), targ1);
				ncmd_set(&sbuf, &sbuf_len, NAK);
				ncmd_add(&sbuf, &sbuf_len, error_chat_nickinuse, xstrlen(error_chat_nickinuse));
			} else {
				int i;
				struct udata_ent *tud;

				nue.nick = targ1;
				nue.fd = clients[cl].fd;
				//fixme: errorcheck for udata_update()
				udata_update(&nue);
				udata_flagunset(targ0, ~0);	//unset all bits
				udata_flagset(targ0, UDC_FLAG_PRESENT);

				//start of separate sbuf and sbuf_len usage
				//this can safely use sbuf and sbuf_len because they are sent before overwritten
				ncmd_set(&sbuf, &sbuf_len, C_DOOR);
				ncmd_add(&sbuf, &sbuf_len, &yes, 1);	//yes is: 'const char yes = 1;'
				ncmd_add(&sbuf, &sbuf_len, tuent->nick, xstrlen(tuent->nick));

				for(i=0; i<MAX_CLIENTS; i++) {
					if(clients[i].fd > 0 && (tud = udata_get_by_fd(clients[i].fd)) &&
					   (tud->flags&UDC_FLAG_PRESENT) && i != cl) {
						if(sendbuf(clients[i].fd, sbuf, sbuf_len, 0) == -1) {
							slog(0, "error sending message to %s (%s)", inet_ntoa(clients[i].address.sin_addr), strerror(errno));
						}
					}
				}
				//end of separate sbuf and sbuf_len usage

				ncmd_set(&sbuf, &sbuf_len, ACK);
				slog(1, "%s(%s) joined chat with nick %s", tuent->user, inet_ntoa(clients[cl].address.sin_addr), tuent->nick);
			}
		}
		ls_send_buffer(clients[cl].fd, sbuf, sbuf_len, 0);
		break;
	}
	case C_WHO:
	{
		char *tb = NULL; long int tbl = 0;
		//test if this is a magic who command (for rchat use), if not no break is executed and
		//normal presence testing is performed
		if(ncmd_getnumargs(clients[cl].buf, clients[cl].buflen) == 1) {
			ncmd_getarg(clients[cl].buf, clients[cl].buflen, &tb, &tbl, 0, 1);
			if(strcmp(tb, MAGIC_WHO_ARG) == 0) {
				slog(1, "received magic who chatcommand from %s", inet_ntoa(clients[cl].address.sin_addr));
				retval = handle_chatcmd(cl);
				break;
			}
		}
	}
	//this gets easily forgotten when adding chatcommands :)
	//I left out the local commands
	case C_DOOR: case C_NICK: case C_AWAY: case C_BAN: case C_BEEP:
	case C_DEOP: case C_KICK: case C_ME: case C_OP: case C_OPER:
	case C_PSAY: case C_SAY: case C_UNBAN:
		if((temp_udata = udata_get_by_fd(clients[cl].fd)) && (temp_udata->flags&UDC_FLAG_PRESENT)) {
			retval = handle_chatcmd(cl);
		} else {
			slog(0, "received unexpected chatcommand %s of %lu bytes from %s (user is not in chat)", ncmd_getname(ncmd_get(clients[cl].buf, clients[cl].buflen)), clients[cl].buflen, inet_ntoa(clients[cl].address.sin_addr));
		}
		break;
	default:
		//fixme: also dump arguments as string and in case of 1 to 4 bytes hex number?
		slog(0, "received unexpected command %s of %lu bytes from %s", ncmd_getname(ncmd_get(clients[cl].buf, clients[cl].buflen)), clients[cl].buflen, inet_ntoa(clients[cl].address.sin_addr));
		retval = -1;
	}

	return retval;
}


int runserver(short int port, short int autofork)
{
	int i, num_bytes;
	unsigned int num_clients = 0;
	const int yes = 1;	//needed for setsockopt() reuseaddress setting
	const char no = 0;
	int child_pid;
	time_t starttime;
	long int uptime;
	int servfd, newfd, maxfd;
	fd_set master, read_fds;
//	struct protoent *prot_entry;
	struct sockaddr_in servaddr, peeraddr;
	int peeraddr_len = sizeof(struct sockaddr_in);	//important to be inited or accept will fuck up the first time!!!!!
	int tfd;

	FD_ZERO(&master);
	FD_ZERO(&read_fds);

	if(loglevel >= 0) {
		if(logfilename == NULL || xstrlen(logfilename) == 0) {
			if((tfd = dup(2)) == -1) {	//try to duplicate stderr
				error("error duplicating stderr for logging (%s)\n", strerror(errno));
				return -1;
			}
			if((logfile = fdopen(tfd, "a")) == NULL) {	//and open it as a stream
				error("error opening a dupped stderr for logging (%s)\n", strerror(errno));
				return -1;
			}
		} else {
			if((logfile = fopen(logfilename, "a")) == NULL) {	//open logfilename as a stream
				error("error opening log file (%s)\n", strerror(errno));
				return -1;
			}
		}
	}

//	if((prot_entry = getprotobyname("tcp")) == NULL) {
//		error("error getting tcp protocol number\n");
//		return -1;
//	}
//	if((servfd = socket(PF_INET, SOCK_DGRAM, prot_entry->p_proto)) == -1) {
	if((servfd = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
		error("error opening server socket (%s)\n", strerror(errno));
		return -1;
	}
	maxfd = servfd;
	FD_SET(servfd, &master);

	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	servaddr.sin_addr.s_addr = INADDR_ANY;	//should actually be converted by htonl()
	memset(&(servaddr.sin_zero), '\0', 8);
	if(bind(servfd, (struct sockaddr *)&servaddr, sizeof(struct sockaddr)) == -1) {
		error("error binding server socket to port %i (%s)\n", port, strerror(errno));
		return -1;
	}
#ifdef SET_REUSEADDR
	if(setsockopt(servfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
		error("error making server socket address reusable (nonfatal, can just be annoying) (%s)\n", strerror(errno));
	}
#endif
	if(listen(servfd, SERVER_BACKLOG) == -1) {
		error("error making the server socket listen (%s)\n", strerror(errno));
		return -1;
	}

	signal(SIGTERM, &ls_handle_signals);
	signal(SIGINT, &ls_handle_signals);
	signal(SIGPIPE, &ls_handle_signals);

	if(autofork) {
		child_pid = fork();
		switch(child_pid) {
		case -1:	//this is an error (no child created at all)
			error("error forking server (%s)\n", strerror(errno));
			return -1;
			break;
		case 0:		//this is the child
			if(setsid() == -1) {	//try to create a new session for the child
						//so it's not a child anymore
				error("error creating a new session for forked process (%s)\n", strerror(errno));
				return -1;
			}
			break;
		default:	//this is the parent with child_pid being the pid of the new child
			say("Server forked to background. pid=%i\n", child_pid);
			return 0;
		}
	}
/*
 * from here everything is child code (if fork was executed and did succeed of course)
 */

	starttime = time(NULL);
	slog(0, "-- %s server v%s (%s) started -----", PACKAGE, VERSION, OS);

	while(!ls_exitloop) {
		read_fds = master;
		if(select(maxfd+1, &read_fds, NULL, NULL, NULL) == -1) {
//experimental code here...seems to work just fine
			if(errno == EINTR && ls_exitloop == 1) break;
			slog(0, "select error (%s)", strerror(errno));
//			error("select error (%s)\n", strerror(errno));
			return -1;
		}

		if(FD_ISSET(servfd, &read_fds)) {
			if(num_clients >= MAX_CLIENTS) {
				slog(1, "connection refused (max. number of clients reached (max. is %i))", MAX_CLIENTS);
/*
 * WARNING!!! this is a really bad solution to this (yet unsolved) problem, but at least a little
 * better than doing nothing at all (so select() keeps returning and client keeps waiting)
 */
				close(accept(servfd, NULL, NULL));
			} else {
//fixme: as the accept(2) manual says, some errors must be handled specially
				if((newfd = accept(servfd, (struct sockaddr *)&peeraddr, &peeraddr_len)) == -1) {
					//this is nonfatal I guess?? (just bad luck for the other side)
					slog(1, "error accepting connection (%s)", strerror(errno));
				} else {
					maxfd = (newfd>maxfd ? newfd : maxfd);
					FD_SET(newfd, &master);
					for(i=0; i<num_clients; i++) {
						if(clients[i].fd == -1) break;
					}
					if(i == num_clients) num_clients++;
					clients[i].fd = newfd;
					clients[i].address = peeraddr;
					clients[i].buf = NULL;
					clients[i].buflen = 0;
					slog(1, "connection from %s", inet_ntoa(clients[i].address.sin_addr));
				}
			}
		}
		for(i=0; i<num_clients; i++) {
			if(clients[i].fd != -1 && FD_ISSET(clients[i].fd, &read_fds)) {
				//fixme: what is this peeraddr_len assignment doing here exactly???
				peeraddr_len = sizeof(struct sockaddr);
				num_bytes = recvbuf(clients[i].fd, &clients[i].buf, &clients[i].buflen, 0);
				switch(num_bytes) {
				case -1:	//error
				{
					struct udata_ent *tuent = udata_get_by_fd(clients[i].fd);
					if(tuent) slog(0, "error receiving data from %s(%s) (%s)", tuent->user, inet_ntoa(clients[i].address.sin_addr), strerror(errno));
					else slog(0, "error receiving data from %s (%s)", inet_ntoa(clients[i].address.sin_addr), strerror(errno));
					break;
				}
				case 0:		//connection closed
				{
					struct udata_ent *tuent = udata_get_by_fd(clients[i].fd);
					if(tuent && (tuent->flags & UDC_FLAG_PRESENT)) {
						int j;
						struct udata_ent nue = UDATA_DEF_ENTRY;
						struct udata_ent *tud;
						char *sbuf = NULL;
						long int sbuf_len = 0;

						slog(1, "connection from %s(%s) closed by peer (%s removed from chat)", tuent->user, inet_ntoa(clients[i].address.sin_addr), tuent->nick);

						ncmd_set(&sbuf, &sbuf_len, C_DOOR);
						ncmd_add(&sbuf, &sbuf_len, &no, 1);	//no is: 'const char no = 0;'
						ncmd_add(&sbuf, &sbuf_len, tuent->nick, xstrlen(tuent->nick));
						ncmd_add(&sbuf, &sbuf_len, error_chat_peerclose, xstrlen(error_chat_peerclose));

						for(j=0; j<MAX_CLIENTS; j++) {
							if(clients[j].fd > 0 && (tud = udata_get_by_fd(clients[j].fd)) &&
							   (tud->flags&UDC_FLAG_PRESENT) && j != i) {
								if(sendbuf(clients[j].fd, sbuf, sbuf_len, 0) == -1) {
									slog(0, "error sending message to %s (%s)", inet_ntoa(clients[j].address.sin_addr), strerror(errno));
								}
							}
						}

						nue.user = tuent->user;
						//fixme? set nick to NULL?
						nue.fd = -1;
						//fixme: errorcheck for udata_update()
						udata_update(&nue);
						udata_flagunset(nue.user, UDC_FLAG_PRESENT | UDC_FLAG_AWAY | UDC_FLAG_OPERATOR | UDC_FLAG_SUPERUSER);
					} else {
						if(tuent) slog(1, "connection from %s(%s) closed by peer", tuent->user, inet_ntoa(clients[i].address.sin_addr));
						else slog(1, "connection from %s closed by peer", inet_ntoa(clients[i].address.sin_addr));
					}
					FD_CLR(clients[i].fd, &master);
					close(clients[i].fd);
					free(clients[i].buf);
					clients[i].buf = NULL;
					clients[i].buflen = 0;
					clients[i].fd = -1;
					break;
				}
				default:
					if(ncmd_iscomplete(clients[i].buf, clients[i].buflen)) {
						int r = ls_handle_netcmd(i);
						//fixme? everything below zero is now unhandled
						if(r >= 0) {
							struct udata_ent *tuent = udata_get_by_fd(clients[r].fd);
							if(tuent) slog(1, "killed connection from %s(%s)", tuent->user, inet_ntoa(clients[i].address.sin_addr));
							else slog(1, "killed connection from %s", inet_ntoa(clients[i].address.sin_addr));
							FD_CLR(clients[r].fd, &master);
							close(clients[r].fd);
							free(clients[r].buf);
							clients[r].buf = NULL;
							clients[r].buflen = 0;
							clients[r].fd = -1;
						}
						ncmd_remove(&(clients[i].buf), &(clients[i].buflen));
					}
				}
			}
		}
	}

	uptime = difftime(time(NULL), starttime);
	close(servfd);
	slog(0, "-- server shut down (uptime: %lid %i:%.2i:%.2i) -----", uptime/86400, (uptime%86400)/3600, (uptime%3600)/60, uptime%60);
	if(logfile != NULL) fclose(logfile);
	return 0;
}
