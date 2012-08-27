/*
 * The list of commands must be placed in the 'typedef enum { ... } NCMDOp;' and in the
 * ncmds[] array. In the array names must be specified as well.
 * Remember to set ncmd_max_code to the last command defined in the enum.
 *
 * It is advised to set the first command to 'ERROR', 'UNKNOWN' or anything alike so command
 * number 0 can be used to denote an error.
 *
 * All parameter stuff and when to use which command is completely your own bussiness.
 * This layer only composes and decomposes commands (and provides a wrapper to write() and read()
 * to make sure your buffer is not partly sent because the MTU of the network interface is too small.
 */

#ifndef NCMD_H
#define NCMD_H 1

#define NCMD_VERSION "0.80"

typedef enum {
	UNKNOWN,
	MSG,
	GETQMSG,
	QMSG,
	GETLIST,
	LIST,
	SETPREF,
	GETPREF,
	PREF,
	CHATDOOR,
	ACK,
	NAK,

	C_DOOR,
	C_AWAY,
	C_BAN,
	C_BEEP,
	C_BS,
	C_CLEAR,
	C_DEOP,
	C_HELP,
	C_KICK,
	C_LOG,
	C_ME,
	C_NICK,
	C_OP,
	C_OPER,
	C_PRIV,
	C_PSAY,
	C_QUIT,
	C_SAY,
	C_UNBAN,
	C_WHO		//last command!! don't forget updating ncmd_max_code if you change this
} NCMDOp;
const static unsigned short int ncmd_max_code = C_WHO;

//fixme? If the N_ prefix on normal commands doesn't cause problems, I could try to add the prefix to
//       the command names also...but that would require many changes in the sourcecode
const static struct {
	const char *name;
	const NCMDOp opcode;
} ncmds[] = {
	{"!!UNKNOWN!!", UNKNOWN},
	{"N_MSG", MSG},
	{"N_GETQMSG", GETQMSG},
	{"N_QMSG", QMSG},
	{"N_GETLIST", GETLIST},
	{"N_LIST", LIST},
	{"N_SETPREF", SETPREF},
	{"N_GETPREF", GETPREF},
	{"N_PREF", PREF},
	{"N_CHATDOOR", CHATDOOR},
	{"N_ACK", ACK},
	{"N_NAK", NAK},

	{"door", C_DOOR},
	{"away", C_AWAY},
	{"ban", C_BAN},
	{"beep", C_BEEP},
	{"bs", C_BS},
	{"clear", C_CLEAR},
	{"deop", C_DEOP},
	{"help", C_HELP},
	{"kick", C_KICK},
	{"log", C_LOG},
	{"me", C_ME},
	{"nick", C_NICK},
	{"op", C_OP},
	{"oper", C_OPER},
	{"priv", C_PRIV},
	{"psay", C_PSAY},
	{"quit", C_QUIT},
	{"say", C_SAY},
	{"unban", C_UNBAN},
	{"who", C_WHO},
	{NULL, 0}
};


/*
 * ncmd.c prototypes
 */

//these should be moved to a better place (functions themselves also...) like misc.c
void store_nl(char *p, unsigned long int val);
unsigned long int read_nl(const char *p);
void store_ns(char *p, unsigned short int val);
unsigned short int read_ns(const char *p);

char *ncmd_getname(NCMDOp ncmd_code);
NCMDOp ncmd_getnum(const char *s, long int sl);
int ncmd_iscomplete(const char *buf, unsigned long int len);
NCMDOp ncmd_get(const char *buf, unsigned long int len);
unsigned short int ncmd_getnumargs(const char *buf, unsigned long int len);
int ncmd_getarg(char *buf, unsigned long int len, char **ap, unsigned long int *aplen, unsigned int argnum, int addzero);
int ncmd_remove(char **buf, unsigned long int *len);
int ncmd_set(char **buf, unsigned long int *len, NCMDOp ncmd);
int ncmd_add(char **buf, unsigned long int *buflen, const char *s, unsigned long int slen);

int sendbuf(int s, const void *msg, size_t len, int flags);
int recvbuf(int s, char **buf, unsigned long int *len, int flags);

#endif
