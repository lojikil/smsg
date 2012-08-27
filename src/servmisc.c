/*
 * this file contains functions the server functions need to operate, I put them
 * here because server.c is growing quite big and the functions here are sort of
 * generally used...
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <utmp.h>		/* to define struct utmp on netbsd */
#include <utmpx.h>
#include "smsg.h"
#include "ncmd.h"	/* for read_ns(), read_nl(), etc. which should be somewhere else */


short int loglevel = 0;		//loglevel 0 by default...good choice?
char *logfilename = NULL;	//name of logfile
FILE *logfile = NULL;		//uninitialized stream


void slog(short int level, char *format, ...)
{
	time_t nowtoo = time(NULL);
	struct tm *now = localtime(&nowtoo);
	va_list ap;

	if(logfile != NULL && level <= loglevel) {
		fprintf(logfile, "%02i/%02i %i:%02i:%02i smsg[server]: ", now->tm_mday, now->tm_mon+1, now->tm_hour, now->tm_min, now->tm_sec);
		va_start(ap, format);
		vfprintf(logfile, format, ap);
		va_end(ap);
		fprintf(logfile, "\n");
		fflush(logfile);
	}
}


//quick hack to avoid segfaults after calling strlen with a NULL pointer
size_t xstrlen(const char *s)
{
	return (s ? strlen(s) : 0);
}


//make sure in is in network byte order!
//currently only accepts localhost(127.0.0.1)
int islocal(struct in_addr in)
{
	struct in_addr local_in;
	if(inet_aton(LOCAL_ADDRESS, &local_in) == 0)
		return 0;
	else if(local_in.s_addr == in.s_addr)
		return 1;
	else
		return 0;
}

//no check is made if ttypath really is a tty
int iswritabletty(const char *ttypath)
{
	struct stat statbuf;
	if(stat(ttypath, &statbuf) == -1) return 0;	//returns 'not writable' on error...
	if((statbuf.st_mode & 020) != 0)
		return 1;
	else
		return 0;
}

//checks if user is logged in on tty using utmp
int isontty(const char *user, const char *tty)
{
	struct utmpx line_ut, *uptr;
	setutxent();
	strncpy(line_ut.ut_line, tty+5, UTX_LINESIZE);	// +5 -> skip '/dev/'
	if((uptr = getutxline(&line_ut)) == (struct utmpx *)0) {
		endutxent();
		return 0;
	}
	if(strncmp(uptr->ut_user, user, UTX_LINESIZE)) {
		endutxent();
		return 0;
	}
	endutxent();
	return 1;
}

//just does get_userlist(user) and checks if it gets back an empty list or not
int isloggedin(const char *user)
{
	struct userlist_ent *l = get_userlist(user);
	if(l == NULL || l->count == 0) {	//error returns 0 too...
		free(l);
		return 0;
	} else {
		free(l);
		return 1;
	}
}

//if user is not NULL only user is counted (thus zero or one entries as result), otherwise it
//finds all different users in utmp and creates an entry for them in the returned userlist
//tty is the tty found in the first entry and count is how many times the user is logged in
//don't forget freeing the returned array when you're done with it!
//to reliably detect the array-end, test for a count of 0
struct userlist_ent *get_userlist(const char *user)
{
	struct userlist_ent *list = NULL, *t;
	struct utmpx *uptr;
	char namebuf[NMAX];
	unsigned int n = 0, error = 0, i, j;

	namebuf[NMAX_NUL] = '\0';	//if care is taken this will never be overwritten I hope??

	setutxent();
	while((uptr = getutxent()) != (struct utmpx *)0) {
		if(uptr->ut_type != USER_PROCESS) continue;
		if(uptr->ut_user[0] == '\0') continue;
		memcpy(&namebuf, uptr->ut_user, NMAX_NAME);
		if(user != NULL && strcmp(namebuf, user) != 0) continue;
		j = -1;
		for(i=0; i<n; i++) {
			if(strcmp(list[i].name, namebuf) == 0) {
				j = i;
				break;
			}
		}
		if(j != -1) {			//user already present
			list[j].count++;	//so just increment count
		} else {
			t = realloc(list, sizeof(struct userlist_ent)*(n+1));
			if(t == NULL) {
				free(list);
				list = NULL;
				error = 1;
				break;
			} else {
				list = t;
				strcpy(list[n].name, namebuf);
				list[n].tty[LMAX_NUL] = '\0';
				strcpy(list[n].tty, "/dev/");
				memcpy(list[n].tty+LMAX_DEVLEN, uptr->ut_line, LMAX_LINE);
				list[n].login = uptr->ut_tv.tv_sec;
				list[n].count = 1;
				n++;
			}
		}
	}
	endutxent();

	if(!error) {	//everything went well...add a termination entry
		t = realloc(list, sizeof(struct userlist_ent)*(n+1));
		if(t == NULL) {	//damn! error...the only solution is to delete the whole list...
			free(list);
			list = NULL;
		} else {
			list = t;
			list[n].name[0] = '\0';
			list[n].tty[0] = '\0';
			list[n].count = 0;
			n++;
		}
	}

	return list;
}


//queue starts with a doubleword containing the number of allocated bytes (easier maintenance),
//then a word containing number of queued messages
//each message is a doubleword containing the total length and then 4 ASCIIZ strings
//containing: time, sender, ip, message

//queue given message with given data
int mq_add(char *user, char *msg, char *sendtime, char *sender, char *source_ip)
{
	char *t, *qp;
	struct udata_ent tue = UDATA_DEF_ENTRY;
	int l = xstrlen(msg) + xstrlen(sendtime) + xstrlen(sender) + xstrlen(source_ip) + 4;

	if(msg != NULL && sendtime != NULL && sender != NULL && source_ip != NULL) {
		if((qp = udata_get_qp(user)) == NULL) {
			tue.user = user;
			if(udata_add(&tue)) return -1;
			qp = udata_get_qp(user);
		}
		if(qp == NULL) {
			if((t = malloc(l+10)) == NULL) return -1;
			udata_set_qp(user, t);
			qp = t;	//maybe qp = udata_get_qp(user); would be cleaner...but whatever
			store_nl(qp, l+10);	//set length of allocated space
			store_ns(qp+4, 1);	//set number of messages
			t = qp + 6;
		} else {
			if((t = realloc(qp, read_nl(qp)+l)) == NULL) return -1;
			udata_set_qp(user, qp);
			qp = t;
			t = qp + read_nl(qp);	//end of old queue is start of new msg
			store_nl(qp, read_nl(qp)+l);
			store_ns(qp+4, read_ns(qp+4)+1);
		}
		store_nl(t, l+4); t += 4;
		strcpy(t, sendtime); t += xstrlen(sendtime) + 1;
		strcpy(t, sender); t += xstrlen(sender) + 1;
		strcpy(t, source_ip); t += xstrlen(source_ip) + 1;
		strcpy(t, msg);
	} else return -1;

	return 0;
}

//set parms to allocated mem with oldest queued message and remove it
//everything is NULL if no queued message is present
int mq_get(char *user, char **msg, char **sendtime, char **sender, char **source_ip, int *numleft)
{
	char *t;
	char *qp = udata_get_qp(user);

	if(qp != NULL) {
		*numleft = read_ns(qp);
		t = qp + 10;
		if((*sendtime = strdup(t)) == NULL) return -1;
		t += xstrlen(t) + 1;
		if((*sender = strdup(t)) == NULL) {free(*sendtime); return -1;}
		t += xstrlen(t) + 1;
		if((*source_ip = strdup(t)) == NULL) {free(*sendtime); free(*sender); return -1;}
		t += xstrlen(t) + 1;
		if((*msg = strdup(t)) == NULL) {free(*sendtime); free(*sender); free(*source_ip); return -1;}
		store_nl(qp, read_nl(qp)-read_nl(qp+6));
		store_ns(qp+4, read_ns(qp+4)-1);
		if(read_ns(qp+4) == 0) {
			free(qp);
			udata_set_qp(user, NULL);
		} else {
			memmove(qp+6, qp+6+read_ns(qp+6), read_nl(qp)-6);
			if((t = realloc(qp, read_nl(qp))) == NULL) return -1;
			udata_set_qp(user, t);
		}
	} else {
		*msg = NULL; *sendtime = NULL; *sender = NULL; *source_ip = NULL; *numleft = 0;
	}

	return 0;
}

//return number of queued messages for user
int mq_getnum(char *user)
{
	struct udata_ent *tent = udata_get(user);
	if(tent != NULL) return read_ns(tent->queue+4);
	return 0;
}
