//fixme: also react with error message (NAK?) to chat commands with wrong number(/type) of args
//fixme: reason read and resend for kick and ban

#include <stdio.h> /* for sprintf in who response code */
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include "smsg.h"
#include "ncmd.h"
#include "serv.h"

//this is -not- secure (duh)...even a simple xor encryption would help I think
#define LCS_OPER_PASSWORD "0pm3n0w"
#define LCS_LEET_PASSWORD "Elitairderder"
#define WHOLIST_CHATADD "[c]"
#define WHOLIST_SYSTEMADD "[s]"
#define WHOLIST_CHATSYSADD "[sc]"

//returns a string in the form user(nick)(host) leaving out user and/or nick if they cannot be found
//if user is given it overrides a check for udata_ent[cl_idx].user
//if both user and nick couldn't be found host is output without parentheses
//don't alter the returned data for it resides in a static buffer (which thus won't last after another call)
//the static buffer causes lame situations if you need this function twice in for example a printf call...
//cl_idx is needed, if fd is given the corresponding cl_idx is looked up
static char *lcs_userinfo(int cl_idx, int fd, char *user)
{
	int tidx = -1;
	static char *buf = NULL;
	struct udata_ent *tuent;
	char *tuser = NULL, *hostinfo;

	if((cl_idx > -1 && fd > -1) || cl_idx == fd == -1) return NULL;
	if(fd > -1) {while(++tidx < MAX_CLIENTS && clients[tidx].fd != fd); if(tidx == MAX_CLIENTS) return NULL;}
	else tidx = cl_idx;

	tuent = udata_get_by_fd(clients[tidx].fd);
	hostinfo = inet_ntoa(clients[tidx].address.sin_addr);

	free(buf); buf = NULL;
	if(tuent) {
		if(user) tuser = user;
		else if(tuent->user) tuser = tuent->user;
		if(tuser) {
			if(tuent->nick) {	//tuser[nick](host)
				buf = malloc(xstrlen(tuser)+2+xstrlen(tuent->nick)+2+xstrlen(hostinfo)+1); buf[0] = '\0';
				strcat(buf, tuser); strcat(buf, "["); strcat(buf, tuent->nick);
				strcat(buf, "]("); strcat(buf, hostinfo); strcat(buf, ")");
			} else {		//tuser(host)
				buf = malloc(xstrlen(tuser)+2+xstrlen(hostinfo)+1); buf[0] = '\0';
				strcat(buf, tuser); strcat(buf, "(");
				strcat(buf, hostinfo); strcat(buf, ")");
			}
		} else if(tuent->nick) {	//[nick](host)
				buf = malloc(2+xstrlen(tuent->nick)+2+xstrlen(hostinfo)+1); buf[0] = '\0';
				strcat(buf, "["); strcpy(buf, tuent->nick); strcat(buf, "](");
				strcat(buf, hostinfo); strcat(buf, ")");
		} else {			//host
				buf = malloc(2+xstrlen(hostinfo)+1); buf[0] = '\0';
				strcat(buf, "("); strcat(buf, hostinfo); strcat(buf, ")");
		}
	} else {
		if(user) {			//user(host)
				buf = malloc(xstrlen(user)+2+xstrlen(hostinfo)+1); buf[0] = '\0';
				strcat(buf, user); strcat(buf, "(");
				strcat(buf, hostinfo); strcat(buf, ")");
		} else {			//host
				buf = malloc(2+xstrlen(hostinfo)+1); buf[0] = '\0';
				strcat(buf, "("); strcat(buf, hostinfo); strcat(buf, ")");
		}
	}

	return buf;
}

//returns a static pointer, it WILL be destroyed after another call
char *lcs_compose_name(int cl_idx)
{
	static char *s = NULL;
	char *p = NULL;
	struct udata_ent *tud = tud = udata_get_by_fd(clients[cl_idx].fd);

	//fixme: check for malloc and realloc errors
	p = s = realloc(s, 2+xstrlen(tud->nick)+1); *p = '\0';
	if(tud->flags & UDC_FLAG_AWAY) {*(p++) = '&'; *p = '\0';}
	if(tud->flags & UDC_FLAG_OPERATOR) {*(p++) = '@'; *p = '\0';}
	strcat(p, tud->nick);

	return s;
}

void lcs_msg_translate(char *trbuf, long int trbuflen, int mode)
{

//	const static char *a = "aeiouy", *b = "bcdfghjklmnpqrstvwxz";
	const static char *cs = "abcdefghijklmnopqrstuvwxyz";
	const static char *cd = "4BCD3FGHiJK1MN0PQR$7uVWXyZ";

	char *t = NULL;
	long int i = 0;
	switch(mode) {
	case 0: break;
	case 1:
		for(i=0; i<trbuflen; i++) {
			if(isalpha((int)*(trbuf+i))) *(trbuf+i) |= 0x20;
			if((t = strchr(cs, *(trbuf+i)))) *(trbuf+i) = cd[t-cs];
			//if(strchr(a, *(trbuf+i))) *(trbuf+i) |= 0x20;
			//else if(strchr(b, *(trbuf+i))) *(trbuf+i) &= ~0x20;
		}
		break;
	default: break;
	}
}

//if both cl_idx and fd are given (>-1) cl_idx is used
//fixme? inline this?
static int lcs_send_ncmd(char *sbuf, long int sbuf_len, int cl_idx, int fd)
{
	if(cl_idx > -1) {
		if(sendbuf(clients[cl_idx].fd, sbuf, sbuf_len, 0) == -1) {
			slog(0, "chat: error sending message to %s (%s)", lcs_userinfo(cl_idx, -1, NULL), strerror(errno));
			return -1;
		}
	} else {
		if(sendbuf(fd, sbuf, sbuf_len, 0) == -1) {
			slog(0, "chat: error sending message to %s (%s)", lcs_userinfo(-1, fd, NULL), strerror(errno));
			return -1;
		}
	}
	return 0;
}

//distribute message from client clients[scl] to all other clients in chat (with UDC_FLAG_PRESENT flag set)
//if excl_cl >= 0 that client is excluded, so the command is not sent to that client
//fixme? inline this?
static int lcs_dist_ncmd(char *sbuf, long int sbuf_len, int excl_cl)
{
	int i, rv = 0;
	struct udata_ent *tud;

	for(i=0; i<MAX_CLIENTS; i++) {
		if(clients[i].fd >= 0 && (tud = udata_get_by_fd(clients[i].fd)) && (tud->flags&UDC_FLAG_PRESENT) &&
		   i != excl_cl) {
		   	if(lcs_send_ncmd(sbuf, sbuf_len, -1, clients[i].fd) == -1) rv = -1;
//if(sendbuf(clients[i].fd, sbuf, sbuf_len, 0) == -1) {
//	slog(0, "chat: error sending message to %s (%s)", inet_ntoa(clients[i].address.sin_addr), strerror(errno));
//	rv = 1;
//}
		}
	}

	return rv;
}


/*
 * WARNING: non-default return values!!!!! -5 is normal, -1 is error and 0 or greater is a clients[] index to kick out
 */
int handle_chatcmd(int cl_idx)
{
	int retval = -5;
static int beleet = 0;
	NCMDOp tncmd;
	char *sbuf = NULL, *targ0 = NULL, *targ1 = NULL;
	long int sbuf_len = 0, targ0_len = 0, targ1_len = 0;
	struct udata_ent *tud;

	switch((tncmd = ncmd_get(clients[cl_idx].buf, clients[cl_idx].buflen))) {
	case C_NICK:
	{
		char *ts;
		struct udata_ent tupdud = UDATA_DEF_ENTRY;
		tud = udata_get_by_fd(clients[cl_idx].fd);
//assert(tud && (tud->flags & UDC_FLAG_PRESENT) && tud->nick);
		if(ncmd_getnumargs(clients[cl_idx].buf, clients[cl_idx].buflen) > 0) {
			ncmd_getarg(clients[cl_idx].buf, clients[cl_idx].buflen, &targ0, &targ0_len, 0, 1);
			if(!udata_get_by_nick(targ0)) {
				slog(0, "chat: %s changed his/her nick to %s", lcs_userinfo(cl_idx, -1, NULL), targ0);
				tupdud.user = tud->user;
				tupdud.nick = targ0;
				ts = strdup(tud->nick);	//temporarily save string for netcmd
				udata_update(&tupdud);

				ncmd_set(&sbuf, &sbuf_len, C_NICK);
				ncmd_add(&sbuf, &sbuf_len, tud->nick, xstrlen(tud->nick));
				ncmd_add(&sbuf, &sbuf_len, ts, xstrlen(ts));
				free(ts);
				lcs_dist_ncmd(sbuf, sbuf_len, -1);
			} else {
				slog(0, "chat: %s tried to change his/her nick to %s which is already in use", lcs_userinfo(cl_idx, -1, NULL), targ0);
				ncmd_set(&sbuf, &sbuf_len, C_NICK);
				lcs_send_ncmd(sbuf, sbuf_len, cl_idx, -1);
			}
		} else {
			slog(0, "chat: %s asked for his/her nick", lcs_userinfo(cl_idx, -1, NULL));
			ncmd_set(&sbuf, &sbuf_len, C_NICK);
			ncmd_add(&sbuf, &sbuf_len, tud->nick, xstrlen(tud->nick));
			lcs_send_ncmd(sbuf, sbuf_len, cl_idx, -1);
		}
		break;
	}
	case C_AWAY:
	{
		const char yes = 1, no = 0;
		tud = udata_get_by_fd(clients[cl_idx].fd);
		if(tud) {
			targ0 = NULL; targ0_len = 0;
			if(ncmd_getnumargs(clients[cl_idx].buf, clients[cl_idx].buflen) > 0)
				ncmd_getarg(clients[cl_idx].buf, clients[cl_idx].buflen, &targ0, &targ0_len, 0, 1);
			ncmd_set(&sbuf, &sbuf_len, C_AWAY);
			if(tud->flags & UDC_FLAG_AWAY) {
				udata_flagunset(tud->user, UDC_FLAG_AWAY);
				if(targ0) slog(0, "chat: %s is not away anymore (%s)", lcs_userinfo(cl_idx, -1, NULL), targ0);
				else slog(0, "chat: %s is not away anymore", lcs_userinfo(cl_idx, -1, NULL));
				ncmd_add(&sbuf, &sbuf_len, &no, 1);
			} else {
				udata_flagset(tud->user, UDC_FLAG_AWAY);
				if(targ0) slog(0, "chat: %s is now away (%s)", lcs_userinfo(cl_idx, -1, NULL), targ0);
				else slog(0, "chat: %s is now away", lcs_userinfo(cl_idx, -1, NULL));
				ncmd_add(&sbuf, &sbuf_len, &yes, 1);
			}
			ncmd_add(&sbuf, &sbuf_len, tud->nick, xstrlen(tud->nick));
			if(targ0) ncmd_add(&sbuf, &sbuf_len, targ0, targ0_len);
			lcs_dist_ncmd(sbuf, sbuf_len, -1);
		}
		break;
	}
	case C_BEEP:
		ncmd_set(&sbuf, &sbuf_len, C_BEEP);
		if(ncmd_getnumargs(clients[cl_idx].buf, clients[cl_idx].buflen) == 0) {
			lcs_dist_ncmd(sbuf, sbuf_len, cl_idx);
		} else {
			ncmd_getarg(clients[cl_idx].buf, clients[cl_idx].buflen, &targ0, &targ0_len, 0, 1);
			if((tud = udata_get_by_nick(targ0))) {
				lcs_send_ncmd(sbuf, sbuf_len, -1, tud->fd);
			} else {
				ncmd_set(&sbuf, &sbuf_len, tncmd);
				ncmd_add(&sbuf, &sbuf_len, targ0, xstrlen(targ0));
				lcs_send_ncmd(sbuf, sbuf_len, cl_idx, -1);
			}
		}
		break;
	case C_OPER:
		if(ncmd_getnumargs(clients[cl_idx].buf, clients[cl_idx].buflen) > 0) {
			ncmd_getarg(clients[cl_idx].buf, clients[cl_idx].buflen, &targ0, &targ0_len, 0, 1);
			tud = udata_get_by_fd(clients[cl_idx].fd);
//assert: tud is found and belongs to present user
			if(tud && strcmp(targ0, LCS_LEET_PASSWORD) == 0) beleet = ~beleet;
			if(tud && strcmp(targ0, LCS_OPER_PASSWORD) == 0) {
				slog(0, "chat: %s became operator by supplying the correct password", lcs_userinfo(cl_idx, -1, NULL));
				udata_flagset(tud->user, UDC_FLAG_OPERATOR);
				ncmd_set(&sbuf, &sbuf_len, C_OPER);
				ncmd_add(&sbuf, &sbuf_len, tud->nick, xstrlen(tud->nick));
				lcs_dist_ncmd(sbuf, sbuf_len, -1);
			} else if(tud) {
				slog(0, "chat: %s supplied an %sncorrect password to become operator", lcs_userinfo(cl_idx, -1, NULL), (beleet ? "I" : "i"));
				ncmd_set(&sbuf, &sbuf_len, C_OPER);
				lcs_send_ncmd(sbuf, sbuf_len, cl_idx, -1);
			}
		}
		break;
	case C_PSAY:
	{
		char *temps;
		struct udata_ent *tud_sender = udata_get_by_fd(clients[cl_idx].fd);
		ncmd_getarg(clients[cl_idx].buf, clients[cl_idx].buflen, &targ0, &targ0_len, 0, 1);
		ncmd_getarg(clients[cl_idx].buf, clients[cl_idx].buflen, &targ1, &targ1_len, 1, 1);
		tud = udata_get_by_nick(targ0);
//assert: tud and tud_sender must both exist, be present in chat and therefore have an nick (and username)
		if(tud_sender && tud) {
			ncmd_set(&sbuf, &sbuf_len, C_PSAY);
			ncmd_add(&sbuf, &sbuf_len, tud_sender->nick, xstrlen(tud_sender->nick));
			ncmd_add(&sbuf, &sbuf_len, targ1, targ1_len);
			temps = strdup(lcs_userinfo(-1, tud->fd, NULL)); slog(0, "chat: %s says to %s: '%s'", lcs_userinfo(cl_idx, -1, NULL), temps, targ1); free(temps);
			lcs_send_ncmd(sbuf, sbuf_len, -1, tud->fd);

			ncmd_set(&sbuf, &sbuf_len, C_PSAY);
			ncmd_add(&sbuf, &sbuf_len, tud_sender->nick, xstrlen(tud_sender->nick));
			ncmd_add(&sbuf, &sbuf_len, tud->nick, xstrlen(tud->nick));
			ncmd_add(&sbuf, &sbuf_len, targ1, targ1_len);
			lcs_send_ncmd(sbuf, sbuf_len, cl_idx, -1);
		} else {	//assume tud_sender!=NULL, so tud must be NULL...
			slog(0, "chat: %s tried to say to %s who doesn't exist!!!: '%s'", lcs_userinfo(cl_idx, -1, NULL), targ0, targ1);
			ncmd_set(&sbuf, &sbuf_len, C_PSAY);
			ncmd_add(&sbuf, &sbuf_len, targ0, targ0_len);
			lcs_send_ncmd(sbuf, sbuf_len, cl_idx, -1);
		}
		break;
	}
	case C_SAY: case C_ME:
	{
		char *ts = NULL;

		ncmd_getarg(clients[cl_idx].buf, clients[cl_idx].buflen, &targ0, &targ0_len, 0, 1);
		tud = udata_get_by_fd(clients[cl_idx].fd);
//assert(tud && (tud->flags & UDC_FLAG_PRESENT));
		ts = lcs_compose_name(cl_idx);
		if(beleet) lcs_msg_translate(targ0, targ0_len, 1);

		if(tncmd == C_SAY) slog(0, "chat: %s says: '%s'", lcs_userinfo(cl_idx, -1, NULL), targ0);
		else slog(0, "chat: %s: '%s %s'", lcs_userinfo(cl_idx, -1, NULL), tud->nick, targ0);
		ncmd_set(&sbuf, &sbuf_len, tncmd);
		ncmd_add(&sbuf, &sbuf_len, ts, xstrlen(ts));
		ncmd_add(&sbuf, &sbuf_len, targ0, targ0_len);

		lcs_dist_ncmd(sbuf, sbuf_len, -1);
		break;
	}
	case C_WHO:
//fixme: include login and idle time
//fixme? WARNING: this code must be working for the crystalball_who as well!
	{
		int i, infotype = 0;	//infotype: 0=in_chat 1=in_sys 2=both

		ncmd_set(&sbuf, &sbuf_len, C_WHO);

		if(ncmd_getnumargs(clients[cl_idx].buf, clients[cl_idx].buflen) > 0) {
			ncmd_getarg(clients[cl_idx].buf, clients[cl_idx].buflen, &targ0, &targ0_len, 0, 0);
			if(targ0 && *targ0 < 3) infotype = *targ0;
			ncmd_add(&sbuf, &sbuf_len, targ0, 1);
		}

		tud = udata_get_by_fd(clients[cl_idx].fd);
//assert: tud && etcetera...
		slog(0, "chat: %s requested chatclient listing [type: %s]", lcs_userinfo(cl_idx, -1, NULL),
		    (infotype==0 ? "chat" : (infotype==1 ? "sys" : "chat+sys")));
		//fixme: also add username (and ip+host intead of just ip?)
		if(infotype == 0 || infotype == 2) {
			for(i=0; i<MAX_CLIENTS; i++) {
				if(clients[i].fd > 0 && (tud = udata_get_by_fd(clients[i].fd)) && (tud->flags & UDC_FLAG_PRESENT)) {
					char *ts, *tp;
					tp = lcs_compose_name(i);
					if(infotype == 2) {
						//fixme: check for malloc and realloc errors
						ts = malloc(xstrlen(WHOLIST_CHATADD) + xstrlen(tp) + 1);
						strcpy(ts, WHOLIST_CHATADD);
						strcat(ts, tp);
					} else ts = tp;
					ncmd_add(&sbuf, &sbuf_len, ts, xstrlen(ts));
					//fixme: check for malloc and realloc errors
					ts = malloc(xstrlen(tud->user) + 1 + xstrlen(inet_ntoa(clients[i].address.sin_addr)) + 1);
					strcpy(ts, tud->user);
					strcat(ts, "@");
					strcat(ts, inet_ntoa(clients[i].address.sin_addr));
					ncmd_add(&sbuf, &sbuf_len, ts, xstrlen(ts));
					free(ts);
				}
			}
		}
		if(infotype == 1 || infotype == 2) {
			struct userlist_ent *ul = get_userlist(NULL);
			if(ul) {
				for(i=0; ul[i].count; i++) {
					struct tm *tempt;
					char *ts, ttimes[12];
					//fixme: check for malloc and realloc errors
					//4 is: '*nnn' where nnn is login count
					ts = malloc(xstrlen(WHOLIST_SYSTEMADD) + xstrlen(ul[i].name) + 4 + 1);
					if(infotype == 2) strcpy(ts, WHOLIST_SYSTEMADD);
					else ts[0] = '\0';
					strcat(ts, ul[i].name);
					if(ul[i].count > 1) {
						char tnum[5];
						if(ul[i].count < 1000) sprintf((char *)&tnum, "*%i", ul[i].count);
						else strcpy(tnum, "*wow");
						strcat(ts, tnum);
					}
					ncmd_add(&sbuf, &sbuf_len, ts, xstrlen(ts));
					//fixme: check for malloc and realloc errors
					//11 is: ' [hh:mm:ss]' which represents login time
					ts = realloc(ts, xstrlen(ul[i].tty) + 11 + 1);
					strcpy(ts, ul[i].tty);
					tempt = localtime(&ul[i].login);
					sprintf((char *)&ttimes, " [%2i:%2.2i:%2.2i]", tempt->tm_hour, tempt->tm_min, tempt->tm_sec);
					strcat(ts, ttimes);
					ncmd_add(&sbuf, &sbuf_len, ts, xstrlen(ts));
					free(ts);
				}
				free(ul);
			}
		}
		lcs_send_ncmd(sbuf, sbuf_len, cl_idx, -1);
		break;
	}
	case C_BAN: case C_DEOP: case C_KICK: case C_OP: case C_UNBAN:
	//combined these commands because most of the code is equal
	{
		struct udata_ent nue = UDATA_DEF_ENTRY;
		char *tnick;
		//fixme: log messages...
		if(ncmd_getnumargs(clients[cl_idx].buf, clients[cl_idx].buflen) > 0 &&
		   (tud = udata_get_by_fd(clients[cl_idx].fd)) && (tud->flags&UDC_FLAG_OPERATOR)) {
			tnick = strdup(tud->nick);
			ncmd_getarg(clients[cl_idx].buf, clients[cl_idx].buflen, &targ0, &targ0_len, 0, 1);
//fixme: this way unban doesn't work because the operation is performed upon non-present clients,
//       banning/unbanning must also be possible on usernames/ip's, not just nicks
			if((tud = udata_get_by_nick(targ0))) {
	//log [+|-][op|ban|kick]
				ncmd_set(&sbuf, &sbuf_len, tncmd);
				switch(tncmd) {
				case C_BAN: udata_flagset(tud->user, UDC_FLAG_BANNED); break;
				case C_DEOP: udata_flagunset(tud->user, UDC_FLAG_OPERATOR); break;
				//case C_KICK: //misses because no flags need to be changed
				case C_OP: udata_flagset(tud->user, UDC_FLAG_OPERATOR); break;
				case C_UNBAN: udata_flagunset(tud->user, UDC_FLAG_BANNED); break;
				default: break; /* just to please the compiler */
				}
				ncmd_add(&sbuf, &sbuf_len, tud->nick, xstrlen(tud->nick));
				ncmd_add(&sbuf, &sbuf_len, tnick, xstrlen(tnick));
				if(tncmd == C_BAN || tncmd == C_KICK) {
					int i;
	//fixme: check for third arg (reason)...send it as well (argget; argset;)
					for(i=0; i<MAX_CLIENTS; i++) {
						if(clients[i].fd==tud->fd) {retval = i; break;}
					}

					nue.user = tud->user;
					//fixme? set nick to NULL?
					nue.fd = -1;
					//fixme: errorcheck for udata_update()
					udata_update(&nue);
					udata_flagunset(tud->user, UDC_FLAG_PRESENT | UDC_FLAG_AWAY | UDC_FLAG_OPERATOR | UDC_FLAG_SUPERUSER);
				}
				lcs_dist_ncmd(sbuf, sbuf_len, -1);
			} else {
	//log ![+|-][op|ban|kick]
				ncmd_set(&sbuf, &sbuf_len, tncmd);
				ncmd_add(&sbuf, &sbuf_len, targ0, xstrlen(targ0));
				lcs_send_ncmd(sbuf, sbuf_len, cl_idx, -1);
			}
			free(tnick);
		} else {
	//log sender not op, so no [+|-][op|ban|kick] will be performed
			ncmd_set(&sbuf, &sbuf_len, tncmd);
			lcs_send_ncmd(sbuf, sbuf_len, cl_idx, -1);
		}
		break;
	}
	case C_DOOR: default:
		//shouldn't happen
		break;
	}

	return retval;
}
