#ifndef SMSG_H_SEEN
#define SMSG_H_SEEN

#include <stdio.h>		/* for FILE */
#include <time.h>
#include <utmp.h>		/* to define struct utmp on netbsd */
#include <utmpx.h>		/* for __UT_NAMESIZE and __UT_LINESIZE */
#include <netinet/in.h>		/* for struct in_addr in the islocal() prototype :( */

#if 0
#ifndef PACKAGE
# define PACKAGE "ezmezg"
#endif
#ifndef VERSION
# define VERSION "undefined.-1"
#endif
#ifndef OS
# define OS "void OS"
#endif
#endif /* 0 */

#define SET_REUSEADDR 1	/* undef this to let the server not set this option */

#define PORT_DEFAULT PORT
#define LOCAL_ADDRESS "127.0.0.1" /* don't change unless your box is real strange */
#define SERVER_BACKLOG 5	/* number of clients wanting a connection that can be queued */
#define MSG_MAX_LEN 16768	/* maximum number of bytes read from stdin */
//#define BUF_SIZE 1024		/* size of the buffer given to recv */
#define MAX_CLIENTS 111		/* maximum number of clients that can be connected to a server */
//TTYSET_UNSET must be equal to UDATA_TTYDEF
#define TTYSET_UNSET "none"	/* ttyname which removes tty preference */
#define TTYSET_THISTTY "this"	/* ttyname which sets preference to current tty (whole string or first char) */
#define MAGIC_WHO_ARG "&!KRySTaLBaLL!&"

//used by input/output procedures to choose between gui and console
extern short int usegui;


/*
 * prototypes
 */
//from smsg.c
void say(char *format, ...);
void error(char *format, ...);


//from server.c
int runserver(short int port, short int autofork);


//from client.c
struct chatopts_s {
	short int forcecolor;
	short int barspec;
	short int erase_key;
	short int mailchecktime;
	char *room;
};
int sendmessage(char *msgdata, char *targethost, char *targetuser, short int port);
int getmessage(short int port);
int getlist(char *targethost, short int port);
int sendprefs(char *tty, char beep, char mode, short int port);
int chatter(char *nick, char *targethost, short int port, const struct chatopts_s *chatopts);

//from servmisc.c
#ifndef UTX_USERSIZE
# ifndef __UT_NAMESIZE
#  define UTX_USERSIZE 32 /* as in solaris utmpx.h */
# else
#  define UTX_USERSIZE __UT_NAMESIZE
# endif
#endif
#ifndef UTX_LINESIZE
# ifndef __UT_LINESIZE
#  define UTX_LINESIZE 32 /* as in solaris utmpx.h */
# else
#  define UTX_LINESIZE __UT_LINESIZE
# endif
#endif

#define NMAX UTX_USERSIZE+1	/* +1 to store NUL byte also */
#define NMAX_NUL UTX_USERSIZE	/* 'offset' for maximum NUL byte location */
#define NMAX_NAME UTX_USERSIZE	/* length of field itself */
#define LMAX UTX_LINESIZE+6	/* to store '/dev/' + NUL byte also */
#define LMAX_NUL UTX_LINESIZE+5	/* 'offset' for maximum NUL byte location */
#define LMAX_LINE UTX_LINESIZE	/* length of field itself */
#define LMAX_DEVLEN 5		/* length of '/dev/' part */

struct userlist_ent {
	char name[NMAX];
	char tty[LMAX];		//tty found in first entry for user
	unsigned int count;	//number of times user is logged in
	time_t login;		//time at which user logged in
};

extern short int loglevel;
extern char *logfilename;
extern FILE *logfile;

void slog(short int level, char *format, ...);
size_t xstrlen(const char *s);
int islocal(struct in_addr in);
int iswritabletty(const char *ttypath);
int isontty(const char *user, const char *tty);
int isloggedin(const char *user);
struct userlist_ent *get_userlist(const char *user);

int mq_add(char *user, char *msg, char *sendtime, char *sender, char *source_ip);
int mq_get(char *user, char **msg, char **sendtime, char **sender, char **source_ip, int *numleft);
int mq_getnum(char *user);


//from udata.c
struct udata_ent {
	char *user;	//name of user to which this entry belongs
	char *tty;	//first tty belonging to user
	char beep;	//0=beep off, 1=beep on
	char mode;	//0=writemode, 1=queue (, 2=queue with notification on all of user's tty's)
	char *queue;	//pointer to message queue (NULL if no queue present)

	int fd;		//to find users from clients in server.c (reset on leave chat/disconnect!!!)
	char *nick;	//chat nickname (if NULL use user?)
	unsigned short int flags; //chat flags: away,banned,operator(,superuser),present('logged into chatroom'),...
	//...

	struct udata_ent *prev;
	struct udata_ent *next;
};
typedef enum UDATA_CFLAGS {
	UDC_FLAG_OPERATOR = 1,
	UDC_FLAG_AWAY = 2,
	UDC_FLAG_PRESENT = 4,
	UDC_FLAG_BANNED = 8,
	UDC_FLAG_SUPERUSER = 16
} UDATA_CFLAGS;
#define UDATA_LEAVE -2
#define UDATA_DEF -1
//UDATA_TTYDEF must be equal to TTYSET_UNSET
#define UDATA_TTYDEF "none"
#define UDATA_FLAGSDEF 0
#define UDATA_BEEPDEF 1
#define UDATA_MODEDEF 0
//fill in name yourself and be careful not to overwrite existing entries with this one (prev and next pointers are lost)
//#define UDATA_DEF_ENTRY (struct udata_ent){NULL, NULL, -2, -2, NULL, -1, NULL, 0, NULL, NULL}
#define UDATA_DEF_ENTRY (struct udata_ent){NULL, NULL, UDATA_LEAVE, UDATA_LEAVE, NULL, -1, NULL, UDATA_FLAGSDEF, NULL, NULL}

struct udata_ent *udata_get(char *user);
struct udata_ent *udata_get_by_nick(char *nick);
struct udata_ent *udata_get_by_fd(int fd);
int udata_add(struct udata_ent *ne);
int udata_remove(char *user);
//int udata_remove_by_fd(int fd);
int udata_update(struct udata_ent *e);
int udata_set_qp(char *user, char *queue_p);
char *udata_get_qp(char *user);
int udata_flagset(char *user, unsigned short int f);
int udata_flagunset(char *user, unsigned short int f);

#endif /* !SMSG_H_SEEN */
