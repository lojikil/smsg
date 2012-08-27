/*
 * TODO:
 *  - sidebar config inside chat (/conf command used for other purposes too)
 *  - add clientlist to logopen and date+time to log open and close
 *  - macro support (Fn to recall, S-Fn or /macro to set, perhaps allow %n vars)
 * ?-?scrollback (sigh...would be handy to have 'virtual' curses windows)
 *  - maybe three values for C_DOOR? 2=join 1=leave by connection close 0=normal leave
 *    so C_DOOR with arg doesn't have to be used for connection_closed_by_peer anymore
 *  - implement reason giving in send code for ban and kick (arg grouping like in say and psay)
 *    and reason displaying for ban and kick in receive code
 *  - received commands ban,kick and unban could be grouped as well as op and deop
 *
 * '[From ][envsender][ ][day<dayname> mon dd<space-right-padded daymonth> hh:mm:ss year]<24chars>([ ][moreinfo])'
 * from-quoted message ('From ' with any number of '>'s in front gets a(nother) '>')
 * blank line or EOF
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/ioctl.h>	/* for resize signal trapping */
#include <signal.h>	/* for resize signal trapping */
#include <pwd.h>
#ifdef HAVE_NCURSES
# include <ncurses.h>
#else
# include <curses.h>
#endif
#include <errno.h>
#include "smsg.h"
#include "ncmd.h"


#define LCC_ODEFAULT_C	7
#define LCC_OSE_C	15
#define LCC_OSAY_S	"["
#define LCC_OSAY_E	"]"
#define LCC_OSAY_C	9
#define LCC_OSAYSELF_C	6
#define LCC_OSAYPERS_C	4
#define LCC_OMSG_S	"<"
#define LCC_OMSG_E	">"
#define LCC_OMSG_C	2
#define LCC_OEMOTE_S	" --"
#define LCC_OEMOTE_E	""
#define LCC_OEMOTE_C	3

#ifndef CMDLINE_HISTLEN
# define CMDLINE_HISTLEN 1
#endif

/* Define this if you don't want your mbox to be opened/closed when its size changes.
   Obviously mail can't be counted if you define this. */
//#define NO_MBOX_OPEN

/* Define to get debug info... */
#define LCC_DEBUG

/*
When buffer pointer is initialized, LCC_BUF_ADD_LEN bytes are allocated.
When the buffer length minus LCC_BUF_ENLARGE_ZONE or more is used, the buffer is
enlarged by LCC_BUF_ADD_LEN.
*/
#define LCC_BUF_ADD_LEN 100
#define LCC_BUF_ENLARGE_ZONE 10
#define LCC_DFL_LOGFILE "smsg.chatlog"
#define LCC_DFL_BARSIZE 15
#define LCC_TOKEN_DELIM " \f\n\r\t\v" /* delimiter string is same as isspace() uses */
#define lcc_skip_to_token_start(p, delim) \
	{ \
		if(*p) \
		  while(*(*p) != '\0' && strchr(delim, *(*p))) \
		    (*p)++; \
	}

#define LCC_ASAY_NUM_PREFIXES 5
const static struct lcc_asay_prefix_s {
	int attrib;
	char *prefix;
} lcc_asay_prefixes[LCC_ASAY_NUM_PREFIXES] = {
	{-10, "no prefix"},	//no prefix (used for line continuation and debugging mainly)
	{2, "@@@ "},		//local events
	{5, ">>> "},		//remote events
	{4, "--- "},		//errors
	{14, "??? "},		//help
};

struct ibuf_s {
	char *ibuf;
	int ttlen;
	int ulen;
};

static WINDOW *cwin_out, *cwin_stat, *cwin_in, *cwin_chaty;
static int update_outwin = 0, update_statwin = 0, update_inwin = 0, update_chatywin = 0;
static int use_color = 0;
static int backspace_char = KEY_BACKSPACE;
static char **culist = NULL;	//pointer array for keeping usernames
static FILE *logfile = NULL;
static int mailcount = -1;	//-1 is unknown number of mails
static struct timeval mailival, maillastcheck, temptv;

static const char *lcc_help_general = "***** smsg v"VERSION" chatter help *****\n"
"  TOPIC: general help\n"
"\n"
"...some general info about the chatter and stuff comes here...\n"
"\n"
"To get help on a specific topic type '/help <topic>' where\n"
"<topic> is the topic of your choice (duh).\n"
"Available topics (abbreviations separated by comma's):\n"
" -- i,iface,interface     help about the interface keys and meanings\n"
" -- c,cmd,cmds,commands   help about available commands\n";

static const char *lcc_help_interface = "??? ***** smsg v"VERSION" chatter help *****\n"
"  TOPIC: interface help\n"
"\n"
"This help is just temporary until at least layout is improved...\n"
"\n"
"- The upper part of the screen shows what happens in the chatroom.\n"
"- The middle part (blue background if you have colors) contains some information:\n"
"  On the left is your nickname and right of that your flags, 'A' means away\n"
"  and 'O' means operator. On the right the character insert/overwrite\n"
"  state and left of that is the serverhost you're connected to.\n"
"  Between insert mode and serverhost, there will be an 'L' if logging is on.\n"
"  Any garbage in the middle probably is debugging info...\n"
"- The lower part is where your fabulous typing work appears.\n"
"- The left (or...uhh) part contains the nicknames of everyone currently in chat.\n"
"\n"
"Use the enter key to send your command or message.\n"
"To edit your typing you can use home, end, cursor-left and cursor-right to move\n"
"through it and backspace and delete to remove characters.\n"
"You can use the tab key to autocomplete nicknames (and commands in the future).\n"
"The insert key works as well by the way.\n"
"Cursor-up and cursor-down can be used to scroll through a list of previously typed commands.\n"
"Page-up and page-down will be used for the scrollback buffer.\n"
"\n"
"Well I think it's all quite obvious...and this help won't be of much help if it's not :)\n";

static const char *lcc_help_commands = "??? ***** smsg v"VERSION" chatter help *****\n"
"  TOPIC: command help\n"
"\n"
"Issueing a command is done by preceding the command of your choice by\n"
"a '/', everything which doesn't start with a slash is considered a message\n"
"and, depending on the PRIV state, sent as a SAY or as a PSAY command.\n"
"Available commands are ('*' means not implemented yet):\n"
"\n"
"AWAY [reason]	switch your away status on or off, if you like\n"
"			you can give a reason\n"
"*BAN <nick>		ban <nick> so he/she is kicked and can't get\n"
"			in again\n"
"BEEP [nick]		beep terminal of nick or everyone if no nick given\n"
"			of course this doesn't guarantee anything\n"
"BS <key>		change backspace key to a custom character\n"
"CLEAR		clear the chatscreen (if possible)\n"
"DEOP <nick>		same as /op but takes away operator right instead\n"
"HELP		dump this help\n"
"KICK <nick>		kick <nick> out of chat\n"
"LOG			switch session log on or off\n"
"ME <text>		dumps a remark of yours in a fashion like:\n"
"			<nick text> ...give it a try to see\n"
"NICK [nick]		change your nick to <nick> if specified,\n"
"			otherwise get your current nick\n"
"*PRIV [nick]	engage a private conversation with <nick> or\n"
"OP <nick>		give <nick> operator rights (you need to be\n"
"			operator yourself!)\n"
"OPER <password>	give yourself operator rights by supplying the\n"
"			correct <password>\n"
"PSAY <nick> <text>	send the message <text> to <nick> only\n"
"QUIT [reason]	quit chat, give a reason if you like\n"
"SAY <text>		send the message <text> to everyone in chat\n"
"			end one (in which case <nick> is not needed)\n"
"*UNBAN <nick>	unban <nick> so he/she can enter again\n"
"WHO			get a list of clients currently in chat and/or\n"
"			logged on (use arg: a/all, s/sys or c/chat)\n"
"			note: a and s give a list, c ot nothing updates the sidebar\n"
"\n"
"(more commands to come: ping, finger/whois, etc...)\n";

#ifdef HAVE_NCURSES
//this code is stolen from lmme
static void lcc_resize_sig_handler(int signum)
{
	// the resizeterm() seems to be necessary to generate the KEY_RESIZE
	struct winsize win;
	ioctl(STDIN_FILENO, TIOCGWINSZ, &win);
	resizeterm(win.ws_row, win.ws_col);
	/* SA_ONESHOT or SA_RESETHAND not set so signal hook shouldn't need to be set again here */
	//signal(SIGWINCH, lcc_resize_sig_handler);
}
#endif

//use 0-7 for normal colors, 8-15 for bold colors (bright colors) and 16 for white on blue
static void lcc_attrset(WINDOW *w, int color)
{
	if(!use_color) return;
	switch(color) {
	case 0 ... 7: wattrset(w, COLOR_PAIR(color) | A_NORMAL); break;
	case 8 ... 15: wattrset(w, COLOR_PAIR(color-8) | A_BOLD); break;
	case 16: wattrset(w, COLOR_PAIR(8) | A_BOLD); break;
	}
}

static void lcc_asay(int prefix, char *format, ...)
{
#ifdef HAVE_NCURSES
	attr_t tattr;
	short tpair;
#endif
	va_list ap;
	int spr, sp_size = 100;
	char *sp;
	int tl, tp = 0;

#ifdef HAVE_NCURSES
	wattr_get(cwin_out, &tattr, &tpair, NULL);
#endif

	sp = malloc(sp_size);
	while(1) {
		va_start(ap, format);
		spr = vsnprintf(sp, sp_size, format, ap);
		va_end(ap);
		if(spr > -1 && spr < sp_size) break;
		if(spr > -1) sp_size = spr + 1;	//glibc 2.1, allocate precisely what is needed
		else sp_size *= 2;		//glibc 2.0, twice the old size
		sp = realloc(sp, sp_size);
	}

	while(1) {
		if(prefix < LCC_ASAY_NUM_PREFIXES && prefix > 0) {
			lcc_attrset(cwin_out, lcc_asay_prefixes[prefix].attrib);
			waddstr(cwin_out, lcc_asay_prefixes[prefix].prefix);
			if(logfile) fputs(lcc_asay_prefixes[prefix].prefix, logfile);
#ifdef HAVE_NCURSES
			wattr_set(cwin_out, tattr, tpair, NULL);
#endif
		}

		tl = strcspn(sp + tp, "\n\r\0");
		wprintw(cwin_out, "%.*s", (*(sp + tp + tl) == '\0' ? tl : tl + 1), sp + tp);
		if(logfile) fprintf(logfile, "%.*s", (*(sp + tp + tl) == '\0' ? tl : tl + 1), sp + tp);

		if(*(sp + tp + tl) == '\0' || *(sp + tp + tl + 1) == '\0') break;
		else tp += tl + 1;
	}

	free(sp);
	update_outwin = 1;
}


static void lcc_sidebar_repaint(void)
{
	werase(cwin_chaty);
	if(culist) {
		int i = 0;
		wmove(cwin_chaty, 1, 0);
		while(culist[i]) {
			wprintw(cwin_chaty, " %s\n", culist[i]);
			i++;
		}
	}
	box(cwin_chaty, 0, 0);
	update_chatywin = 1;
}

static void lcc_status_update(const char *server, const char *nick, const char *room, const char *priv, int flags, int insertmode)
{
	//fixme? also show your host and perhaps time?
	//[nick] [(O)(A)] ([talking to: priv])        ...            [server] mail:nnn (L)  [ins/ovr]
	if(use_color) wbkgdset(cwin_stat, COLOR_PAIR(8) | A_BOLD);
	werase(cwin_stat);
	if(room) mvwprintw(cwin_stat, 0, 0, "[%s in #%s]:[%s%s]",
	                   nick, room, (flags&UDC_FLAG_OPERATOR ? "O" : ""), (flags&UDC_FLAG_AWAY ? "A" : ""));
	else mvwprintw(cwin_stat, 0, 0, "[%s]:[%s%s]",
	               nick, (flags&UDC_FLAG_OPERATOR ? "O" : ""), (flags&UDC_FLAG_AWAY ? "A" : ""));
	if(priv) wprintw(cwin_stat, " [talking to: %s]", priv);
	//-17 for ' mail:nnn (L) [ins]' or ' (L) [ovr]'
	if(server) mvwprintw(cwin_stat, 0, COLS-2-strlen(server)-17, "[%s]", server);
	if(mailcount == -1) wprintw(cwin_stat, " mail:???");
	else wprintw(cwin_stat, " mail:%3.i", mailcount);
	wprintw(cwin_stat, " %s [%s]", (logfile ? "L" : " "), (insertmode ? "ins" : "ovr"));

	update_statwin = 1;
}

//this function inserts data into buffer bs and updates cwin_in, assuming
//it up to date
void lcc_input_edit_wizard(struct ibuf_s *bs, char *data, int datalen, int *ipos, int insertmode)
{
	int orglen = bs->ulen;
	int newlen = (insertmode		//calc new length of buffer
		      ? bs->ulen + datalen
		      : bs->ulen + datalen - (bs->ulen - *ipos));
	if(newlen < bs->ulen) newlen = bs->ulen;	//buffer can never become shorter

	if(newlen > bs->ttlen) {	//new length doesn't fit, enlarge buffer
		bs->ibuf = realloc(bs->ibuf, newlen);
		bs->ttlen = newlen;
	}

	//update buffer contents
	if(insertmode && *ipos < bs->ulen) memmove(bs->ibuf + *ipos + datalen, bs->ibuf + *ipos, bs->ulen - *ipos);
	memcpy(bs->ibuf + *ipos, data, datalen);
	bs->ulen = newlen;

	//update cwin_in contents
	waddnstr(cwin_in, bs->ibuf + *ipos, (insertmode ? orglen - *ipos + datalen : datalen));
	update_inwin = 1;

	*ipos += datalen;
}

//this is the strsep from glibc (sysdeps/generic/strsep.c)
//I put it in here because sunos doesn't have this function
char *lcc_my_strsep(char **p, const char *delim)
{
	char *begin, *end;

	if((begin = *p) == NULL) return NULL;

	//a frequent case is when the delimiter string contains only one character
	//here we don't need to call the expensive strpbrk() function and instead work using strchr()
	if(delim[0] == '\0' || delim[1] == '\0') {
		char ch = delim[0];
		if(ch == '\0') {
			end = NULL;
		} else {
			if(*begin == ch) end = begin;
			else if(*begin == '\0') end = NULL;
			else end = strchr(begin+1, ch);
		}
	} else {
		end = strpbrk(begin, delim);	//find the end of the token
	}

	if(end) {
		//terminate the token and set *p past NUL character
		*end++ = '\0';
		*p = end;
	} else {
		*p = NULL;	//no more delimiters; this is the last token
	}

	return begin;
}

//the same as strsep, except it first skips to the start of the next token
static char *lcc_strsepx(char **p, char *delim)
{
	//fixme: I -think- it would be better to copy the whole strsep here and 'integrate' both things
	lcc_skip_to_token_start(p, delim);
	//if(*p[0] == '\0') return NULL;
	return lcc_my_strsep(p, delim);
}

static int openlog(const char *filename)
{
	if(logfile) return 1;
	if((logfile = fopen(filename, "a")) == NULL) return -1;
	fputs("_\\|. chat session log opened .|/_\n", logfile);
	return 0;
}
static int closelog(void)
{
	if(!logfile) return 1;
	fputs("_\\|. chat session log closed .|/_\n", logfile);
	fclose(logfile);
	logfile = NULL;
	return 0;
}

//returns -1 on error, 0 on no change or 1 on change
static int lcc_mboxchange(const char *path)
{
	static int firstrun = 1;
	static struct stat last_st;
	struct stat temp_st;
	int changed = 0;

	if(stat(path, &temp_st) == -1) {
		if(mailcount > -1) lcc_asay(1, "Ow. Less mail! (file is removed..)");
		mailcount = -1;
		return -1;
	}

	if(firstrun) {
		firstrun = 0;
		changed = 2;
	} else {
		//could also compare temp_st.st_atime to temp_st.st_mtime
		//but other programs can change the atime so it's not reliable
		if(temp_st.st_size != last_st.st_size)
			changed = (temp_st.st_size > last_st.st_size ? 1 : -1);
	}

	if(changed) {
#ifndef NO_MBOX_OPEN
		int n = 0;
		FILE *tf;
		char buf[255];
		if(changed != 2) lcc_asay(1, "%s mail!", (changed > 0 ? "Yay! More" : "Hmm. Less"));
		if((tf = fopen(path, "r")) == NULL) {lcc_asay(0, "\n"); mailcount = -1; return -1;}
		while(fgets((char *)&buf, 255, tf)) {
			if(strncmp("From ", (char *)&buf, 5) == 0) {
				n++;
				if(changed == 1 && mailcount != -1 && n > mailcount) {
					int sl = strcspn(buf + 5, " \t\n\r\0");
					//*(buf + 5 + sl) = '\0';
					//lcc_asay(0, " [%s]", buf + 5);
					lcc_asay(0, " [%.*s]", sl, buf + 5);
				}
			}
			if(strncmp("Subject: ", (char *)&buf, 9) == 0
			   && changed == 1 && mailcount != -1 && n > mailcount) {
				int sl = strcspn(buf + 9, "\n\r\0");
				lcc_asay(0, "<%.*s>", sl, buf + 9);
			}
		}
		fclose(tf);
		if(changed != 2) lcc_asay(0, "\n");
		mailcount = n;
#else
		if(changed != 2) lcc_asay(1, "%s mail!\n", (changed > 0 ? "Yay! More" : "Hmm. Less"));
#endif
	}

	last_st = temp_st;
	return (changed ? 1 : 0);
}

static void lcc_culist_clear(void)
{
	if(culist) {
		char **p = culist;
		while(*p) {
			free(*p);
			p++;
		}
		free(culist);
		culist = NULL;
	}
}
static int lcc_culist_add(const char *name)
{
	if(culist == NULL) {
		if((culist = malloc(sizeof(char *) * 2)) == NULL) return -1;
		if((culist[0] = strdup(name)) == NULL) {free(culist); culist = NULL; return -1;}
		culist[1] = NULL;
	} else {
		int i = 0;
		char **p;
		while(culist[i++]);
		if((p = realloc(culist, sizeof(char *) * (i + 1))) == NULL) return -1;
		culist = p;
		if((culist[i-1] = strdup(name)) == NULL) return -1;
		culist[i] = NULL;
	}
	return 0;
}


//server_string is just a string describing the server ('hostname(ip)') to show on the statusbar
//returns 0 on success, -2 on server quit (so there's no point in sending a leave command anymore), -1
//on other error
//reason should be a nullpointer which can be used to return a reason for quitting
int chatter_func(int connfd, const char *initial_nick, const char *server_string, const struct chatopts_s *chatopts, char **reason)
{
//this var gets set in when a change in the present users is about to happen,
//at the end of the loop a /who request will be sent out
int req_who = 0;
	int stupd = 1, kicked = 0, insertmode = 1;
	int rv = 0, exitloop = 0, sendit;
	char *bufcopy = NULL, *rtokenp, *rbuf = NULL, *sbuf = NULL;
	struct ibuf_s iqueue[CMDLINE_HISTLEN][2];
	int ibuf_pos = 0, ibqi = 0;
	long int rbuf_len = 0, sbuf_len = 0;
	int maxfd;
#ifdef HAVE_NCURSES	/* since it's only use for SIGWINCH */
	struct sigaction sastruct;
#endif
	fd_set master, read_fds;
	NCMDOp tncmd;
	int flags = 0;
	struct passwd *pwent = NULL;
	char *mboxpath = NULL, *priv = NULL, *nick = (initial_nick ? strdup(initial_nick) : NULL);

//******* init
#ifdef HAVE_NCURSES
	//resize signal trapping code from lmme
	sastruct.sa_handler = lcc_resize_sig_handler;
	sigemptyset(&sastruct.sa_mask);
	sastruct.sa_flags = SA_RESTART;
	sigaction(SIGWINCH, &sastruct, NULL);
#endif
//todo: sigterm and sigint handlers

	FD_ZERO(&master);
	FD_ZERO(&read_fds);
	FD_SET(STDIN_FILENO, &master);
	FD_SET(connfd, &master);
	maxfd = connfd + 1;

	initscr();
	if(chatopts->barspec >= 0 && ((chatopts->barspec&0x1FF) <= COLS
	                              || ((chatopts->barspec&0x1FF) == 0x1FF))) {	//legal barspec supplied?
		int barsiz = chatopts->barspec & 0x1FF; if(barsiz == 0x1FF) barsiz = LCC_DFL_BARSIZE;
		if(chatopts->barspec&0x200) {	//bar on right right side
			cwin_out = newwin(LINES-3, COLS-barsiz, 0, 0);
			cwin_chaty = newwin(LINES-3, barsiz, 0, COLS-barsiz);
		} else {		//bar on right left side
			cwin_out = newwin(LINES-3, COLS-barsiz, 0, barsiz);
			cwin_chaty = newwin(LINES-3, barsiz, 0, 0);
		}
	} else {
		cwin_out = newwin(LINES-3, COLS-LCC_DFL_BARSIZE, 0, LCC_DFL_BARSIZE);
		cwin_chaty = newwin(LINES-3, LCC_DFL_BARSIZE, 0, 0);
	}
	cwin_stat = newwin(1, COLS, LINES-3, 0);
	cwin_in = newwin(2, COLS, LINES-2, 0);
	cbreak();
	noecho();
	//enable scrolling for output window
	//idlok(cwin_out, TRUE);
	scrollok(cwin_out, TRUE);
	//and also for the input window
	//idlok(cwin_in, TRUE);
	scrollok(cwin_in, TRUE);
	keypad(cwin_in, TRUE);

	if(has_colors() || chatopts->forcecolor) {
		use_color = 1;
		start_color();
#ifdef HAVE_NCURSES /* fixme: is this right? */
		use_default_colors();
#endif
		init_pair(0, -1, -1);
		init_pair(1, COLOR_BLUE,    -1);
		init_pair(2, COLOR_GREEN,   -1);
		init_pair(3, COLOR_CYAN,    -1);
		init_pair(4, COLOR_RED,     -1);
		init_pair(5, COLOR_MAGENTA, -1);
		init_pair(6, COLOR_YELLOW,  -1);
		init_pair(7, COLOR_WHITE,   -1);
		init_pair(8, COLOR_WHITE,   COLOR_BLUE);
	}

{
int i;
		for(i=0; i<CMDLINE_HISTLEN; i++) {
			iqueue[i][0].ibuf = iqueue[i][1].ibuf = NULL;
			iqueue[i][0].ttlen = iqueue[i][1].ttlen = 0;
			iqueue[i][0].ulen = iqueue[i][1].ulen = 0;
		}
}

	//set both to zero so mail is immediately checked on program start
	mailival.tv_sec = 0; mailival.tv_usec = 0;
	maillastcheck.tv_sec = 0; maillastcheck.tv_usec = 0;

	if((mboxpath = getenv("MAIL")) != NULL) {
		char *t = mboxpath;
		mboxpath = malloc(strlen(t) + 1);
		strcpy(mboxpath, t);
	} else {
		pwent = getpwuid(getuid());
		mboxpath = malloc(strlen(MBOXDIR) + 1 + strlen(pwent->pw_name) + 1);
		strcpy(mboxpath, MBOXDIR);
		strcat(mboxpath, "/");
		strcat(mboxpath, pwent->pw_name);
	}

//******* main code
	lcc_attrset(cwin_out, 10);
	lcc_asay(1, " ,s$SSSSSSS$s.  .sS. sSSs.`SSSSS&s, .sSSSSSS'\n");
	lcc_asay(1, " 7SS'     _oSP .SS SYY `SS `SS.     SS'   \"'");
	lcc_attrset(cwin_out, 2); lcc_asay(0, "   C H A T T E R\n"); lcc_attrset(cwin_out, 10);
	lcc_asay(1, "  `%%S$s. SS^\" .S'  `Y' dS'. ^7SSs. dS .oSs.");
	lcc_attrset(cwin_out, 4); lcc_asay(0, "      v"VERSION"\n"); lcc_attrset(cwin_out, 10);
	lcc_asay(1, " So  `3S    .sY` .   .dS'     `%%S' SS.  `SS.\n");
	lcc_asay(1, " `SSSS&' .,$P+'   .s&SP'.sSSSSS$'  `sSSSSSSP");
	lcc_attrset(cwin_out, 9); lcc_asay(0, "  by Ed de Wonderkat and Matt Logsdon\n");
	lcc_asay(0, "\n");

lcc_attrset(cwin_out, 12);
lcc_asay(3, "***** Please send any complaints, bug reports or comments to:\n");
lcc_asay(3, "***** elanor@cypher-sec.org\n");
lcc_asay(3, "\n");
lcc_asay(3, "Notes:\n");
lcc_asay(3, "If your backspace key does not work correctly use the /bs command to set a\n");
lcc_asay(3, "custom backspace key. A commandline argument for that is being worked on, be\n");
lcc_asay(3, "patient.\n");
lcc_asay(3, "This remedy (read: ugly hack) will only work in some particular cases; it\n");
lcc_asay(3, "appears some terminals use a two-char sequence which ncurses can't handle afaik.\n");
lcc_asay(3, "If you know how to solve this please tell me!\n");
lcc_asay(3, "\n");
lcc_asay(3, "Yay! Smsg now has tab completion!\n");
lcc_asay(3, "And there is now command history, use cursor up and down.\n");
lcc_asay(0, "\n");
	lcc_attrset(cwin_out, LCC_ODEFAULT_C);
	update_outwin = 1;

	backspace_char = chatopts->erase_key;

	//request /who list right after joining to fill sidebar
	ncmd_set(&sbuf, &sbuf_len, C_WHO);
	if(sendbuf(connfd, sbuf, sbuf_len, 0) == -1) {
		lcc_asay(3, "error sending command (%s)\n", strerror(errno));
	}
	update_chatywin = 1;

	while(!exitloop) {
		//the stupd check must be before the update_*win checks because it sets update_statwin
		if(stupd) {lcc_status_update(server_string, nick, chatopts->room, priv, flags, insertmode); stupd = 0;}
#ifdef LCC_DEBUG
mvwprintw(cwin_stat, 0, 30, "ttl=%i ul=%i p=%i ibqi=%i       ",
	  iqueue[ibqi][0].ttlen, iqueue[ibqi][0].ulen, ibuf_pos, ibqi); update_statwin = 1;
#endif
		//this was the most efficient method I could think of for refreshment
		//(apart maybe from single line refreshment if that's possible, but scrolling makes that difficult)
		//this way the screen is never updated more than once a loop
if(update_inwin) wmove(cwin_in, ibuf_pos/COLS, ibuf_pos%COLS);
		if(update_outwin || update_statwin || update_inwin || update_chatywin) {
			if(update_outwin) wnoutrefresh(cwin_out);
			if(update_statwin) wnoutrefresh(cwin_stat);
			if(update_inwin) wnoutrefresh(cwin_in);
			if(update_chatywin) wnoutrefresh(cwin_chaty);

//necessary only for sidebar and hackprints in statusbar??
setsyx(LINES-2+(ibuf_pos/COLS), ibuf_pos%COLS);
			doupdate();
			update_outwin = update_statwin = update_inwin = update_chatywin = 0;
		}

		read_fds = master;
		if(select(maxfd, &read_fds, NULL, NULL,
			  (chatopts->mailchecktime > 0 ? &mailival : NULL)) == -1) {
			if(errno == EINTR) continue;
			lcc_asay(3, "select error (%s)\n", strerror(errno));
		}

		if(chatopts->mailchecktime > 0) {
			/* the intervals can be 0.9sec too early or too late but that's not a problem for mailchecking */
			gettimeofday(&temptv, NULL);
			if(maillastcheck.tv_sec + chatopts->mailchecktime <= temptv.tv_sec) {
				if(lcc_mboxchange(mboxpath) != 0) stupd = 1;
				maillastcheck.tv_sec = temptv.tv_sec;
				mailival.tv_sec = chatopts->mailchecktime; mailival.tv_usec = 0;
			} else {
				mailival.tv_sec = chatopts->mailchecktime - (temptv.tv_sec - maillastcheck.tv_sec);
				mailival.tv_usec = 0;
			}
		}

		if(FD_ISSET(STDIN_FILENO, &read_fds)) {
			int cont_loop = 0;
			int ch;	//ch as int screws up on sparc (big endian i suppose)

			//make sure some room is allocated
			if(!iqueue[ibqi][0].ibuf) {
				iqueue[ibqi][0].ibuf = malloc(LCC_BUF_ADD_LEN);
				iqueue[ibqi][0].ttlen = LCC_BUF_ADD_LEN;
				iqueue[ibqi][0].ulen = 0;
			} else if(iqueue[ibqi][0].ulen >= iqueue[ibqi][0].ttlen - LCC_BUF_ENLARGE_ZONE) {
				iqueue[ibqi][0].ibuf = realloc(iqueue[ibqi][0].ibuf, iqueue[ibqi][0].ttlen + LCC_BUF_ADD_LEN);
				iqueue[ibqi][0].ttlen += LCC_BUF_ADD_LEN;
			}

			/* get character */
			ch = wgetch(cwin_in);
#ifdef LCC_DEBUG
mvwprintw(cwin_stat, 0, 20, "c=<%#o>   ", ch); update_statwin = 1;
#endif

			/* character read, act properly */
/* fixme: WARNING: custom_backspace_char_hack*/
if(ch == backspace_char) ch = 8;
			switch(ch) {
#ifdef HAVE_NCURSES
			case KEY_RESIZE:	/* we get this if the terminal is resized */
			{
//fixme: scrolling doesn't work (and can't work I think cause the window has already been altered when this code is run)
//fixme: chaty win doesn't repaint
//fixme: add support for custom sidebar location/size
				int tx, ty;
				getyx(cwin_out, ty, tx);
				//scroll up window if necessary, so that not the upper but the lower part remains visible
				if(ty-(LINES-3) > 0) wscrl(cwin_out, ty-(LINES-3));
				wresize(cwin_out, LINES-3, COLS-LCC_DFL_BARSIZE);
				wresize(cwin_chaty, LINES-3, LCC_DFL_BARSIZE);
				mvwin(cwin_stat, LINES-3, 0);
				mvwin(cwin_in, LINES-2, 0);

#ifdef LCC_DEBUG
lcc_asay(0, "new size: %ix%i [scrolling up %i lines]\n", COLS, LINES, ty-(LINES-3));
#endif
				stupd = update_outwin = update_statwin = update_inwin = update_chatywin = 1;
				cont_loop = 1;


#if 0
	if(chatopts->barspec >= 0 && ((chatopts->barspec&0x1FF) <= COLS
	                              || ((chatopts->barspec&0x1FF) == 0x1FF))) {	//legal barspec supplied?
		int barsiz = chatopts->barspec & 0x1FF; if(barsiz == 0x1FF) barsiz = LCC_DFL_BARSIZE;
		if(chatopts->barspec&0x200) {	//bar on right right side
			cwin_out = newwin(LINES-3, COLS-barsiz, 0, 0);
			cwin_chaty = newwin(LINES-3, barsiz, 0, COLS-barsiz);
		} else {		//bar on right left side
			cwin_out = newwin(LINES-3, COLS-barsiz, 0, barsiz);
			cwin_chaty = newwin(LINES-3, barsiz, 0, 0);
		}
	} else {
		cwin_out = newwin(LINES-3, COLS-LCC_DFL_BARSIZE, 0, LCC_DFL_BARSIZE);
		cwin_chaty = newwin(LINES-3, LCC_DFL_BARSIZE, 0, 0);
	}
	cwin_stat = newwin(1, COLS, LINES-3, 0);
	cwin_in = newwin(2, COLS, LINES-2, 0);
#endif


				break;
			}
#endif /* HAVE_NCURSES */

			case 9:	//tab key
			{
				int cui = 0, cusi = -1, ctslen;
				char *cw = NULL, *cts = iqueue[ibqi][0].ibuf + ibuf_pos;

				cont_loop = 1;	//has to be done here since there are multiple break;points

				while(--cts >= iqueue[ibqi][0].ibuf && strchr(LCC_TOKEN_DELIM, *cts) == NULL);
				cts++;
				ctslen = ibuf_pos - (cts - iqueue[ibqi][0].ibuf);
				//now cts points to token start, token length is ibuf_pos - (cts - iqueue[ibqi][0].ibuf)

				if(ctslen > 0 && cts[0] == '/'
				   && cts == iqueue[ibqi][0].ibuf) {	//look for matching command
					char *t;
					if(ctslen > 0) {cts++; ctslen--;}	//skip the '/'
					for(cui=0; cui<=ncmd_max_code; cui++) {
						t = ncmd_getname(cui);
						if(strlen(t) >= 2	//don't consider things starting with '!!' or 'N_'
						   && ((t[0] == '!' && t[1] == '!')
						       || (t[0] == 'N' && t[1] == '_'))) continue;
						if(strncasecmp(cts, t, ctslen) == 0) {
							if(cusi == -1) cusi = cui;	//first match
							else {cui = -1; break;}		//second match
						}
					}
					if(cusi == -1) break;	//break if nothing matched
					if(cui == -1) {
						lcc_asay(1, "multiple command matches:");
						for(cui=0; cui<=ncmd_max_code; cui++) {
							t = ncmd_getname(cui);
							if(strlen(t) >= 2	//don't print things starting with '!!' or 'N_'
							   && ((t[0] == '!' && t[1] == '!')
							       || (t[0] == 'N' && t[1] == '_'))) continue;
							if(strncasecmp(cts, t, ctslen) == 0)
								lcc_asay(0, " <%s>", t);
						}
						lcc_asay(0, "\n");
						break;
					} else {
						cw = ncmd_getname(cusi);	//must reget string because it is statically allocated!
					}
				} else {				//look for matching nickname
					if(!culist) break;
					while(culist[cui]) {
						if(strncasecmp(cts, culist[cui], ctslen) == 0) {
							if(cusi == -1) cusi = cui;	//first match
							else {cui = -1; break;}		//second match
						}
						cui++;
					}
					if(cusi == -1) break;	//break if nothing matched
					if(cui == -1) {		//show list if more than one match found
						lcc_asay(1, "multiple nickname matches:");
						cui = 0;
						while(culist[cui]) {
							if(strncasecmp(cts, culist[cui], ctslen) == 0)
								lcc_asay(0, " [%s]", culist[cui]);
							cui++;
						}
						lcc_asay(0, "\n");
						break;
					} else {
						cw = culist[cusi];
					}
				}
{
				//execution only gets here if only one match was found
//fixme: warning! wicked hack used here to avoid an alloc
				int cl = strlen(cw);
				cw = strdup(cw);	//duplicate string because commandnames are constant
				*(cw+cl) = ' ';	//change \0 to space
				lcc_input_edit_wizard(&iqueue[ibqi][0],
						      cw + ctslen,
						      cl + 1 - ctslen,
						      &ibuf_pos,
				                      insertmode);
				//*(cw+cl) = '\0';	//change space back to \0
				free(cw);	//free dupped string
}
				break;
			}
			case KEY_HOME:
				if(ibuf_pos > 0) {
					ibuf_pos = 0;
					update_inwin = 1;
				}
				cont_loop = 1;
				break;
			case KEY_END:
				if(ibuf_pos < iqueue[ibqi][0].ulen) {
					ibuf_pos = iqueue[ibqi][0].ulen;
					update_inwin = 1;
				}
				cont_loop = 1;
				break;
			case KEY_LEFT:
				if(ibuf_pos > 0) {
					ibuf_pos--;
					update_inwin = 1;
				}
				cont_loop = 1;
				break;
			case KEY_RIGHT:
				if(ibuf_pos < iqueue[ibqi][0].ulen) {
					ibuf_pos++;
					update_inwin = 1;
				}
				cont_loop = 1;
				break;
			case KEY_DC:
				if(ibuf_pos < iqueue[ibqi][0].ulen) {
					if(ibuf_pos < iqueue[ibqi][0].ulen-1)
						memmove(iqueue[ibqi][0].ibuf + ibuf_pos,
							iqueue[ibqi][0].ibuf + ibuf_pos + 1,
							iqueue[ibqi][0].ulen - ibuf_pos);
					if(ibuf_pos == iqueue[ibqi][0].ulen-1) {
						waddstr(cwin_in, " \b");
					} else {
						waddnstr(cwin_in, iqueue[ibqi][0].ibuf + ibuf_pos, iqueue[ibqi][0].ulen - ibuf_pos);
						waddch(cwin_in, ' ');
					}
					iqueue[ibqi][0].ulen--;
					update_inwin = 1;
				}
				cont_loop = 1;
				break;
			case KEY_BACKSPACE: case 8:
				if(ibuf_pos > 0) {
					ibuf_pos--;
					if(ibuf_pos < iqueue[ibqi][0].ulen-1)
						memmove(iqueue[ibqi][0].ibuf + ibuf_pos,
							iqueue[ibqi][0].ibuf + ibuf_pos + 1,
							iqueue[ibqi][0].ulen - ibuf_pos);
					if(ibuf_pos == iqueue[ibqi][0].ulen-1) waddstr(cwin_in, "\b \b");
					else {
						waddch(cwin_in, '\b');
						waddnstr(cwin_in, iqueue[ibqi][0].ibuf + ibuf_pos, iqueue[ibqi][0].ulen - ibuf_pos - 1);
						waddch(cwin_in, ' ');
					}
					iqueue[ibqi][0].ulen--;
					update_inwin = 1;
				}
				cont_loop = 1;
				break;
			case 21:	//C-u
				ibuf_pos = iqueue[ibqi][0].ulen = 0; werase(cwin_in); update_inwin = 1; cont_loop = 1;
				break;
			case KEY_IC: case KEY_EIC:
				insertmode = (insertmode ? 0 : 1);
				stupd = 1;
				cont_loop = 1;
				break;
			case KEY_UP:
				if(ibqi < CMDLINE_HISTLEN - 1 && iqueue[ibqi+1][0].ibuf != NULL) {
					ibqi++;
					goto UGLY_CURSOR_MOVEMENT_GOTO;
				}
				cont_loop = 1;
				break;
			case KEY_DOWN:
				if(ibqi > 0) {
					ibqi--;
					goto UGLY_CURSOR_MOVEMENT_GOTO;
				}
				cont_loop = 1;
				break;
UGLY_CURSOR_MOVEMENT_GOTO:
	ibuf_pos = iqueue[ibqi][0].ulen;
	if(ibqi != 0 && iqueue[ibqi][1].ibuf == NULL) {
		iqueue[ibqi][1] = iqueue[ibqi][0];
		iqueue[ibqi][1].ibuf = malloc(iqueue[ibqi][0].ulen);
		memcpy(iqueue[ibqi][1].ibuf, iqueue[ibqi][0].ibuf, iqueue[ibqi][0].ulen);
	}
	wmove(cwin_in, 0, 0);
	werase(cwin_in);
	waddnstr(cwin_in, iqueue[ibqi][0].ibuf, iqueue[ibqi][0].ulen);
	update_inwin = 1;
	cont_loop = 1;
	break;

case KEY_PPAGE: case KEY_NPAGE:
	lcc_asay(1, "Page_up and page_down are reserved for scrollback which is yet to come...be patient.\n");
	cont_loop = 1;
	break;

			case '\n': case '\r': case KEY_ENTER:
				//duplicate current buffer and add a \0, parsing code uses iqueue data
				//while parsing, knowing that ulen is actually +1 for the \0
				bufcopy = malloc(iqueue[ibqi][0].ulen + 1);
				memcpy(bufcopy, iqueue[ibqi][0].ibuf, iqueue[ibqi][0].ulen);
				bufcopy[iqueue[ibqi][0].ulen] = '\0';
				break;
			default:
			{
				char cch = ch;	//to avoid multiple casts
				if (cch > 13 && cch < 31 && cch != 15 && cch != 22) cch = (cch & 127) | 64;
				lcc_input_edit_wizard(&iqueue[ibqi][0], &cch, 1, &ibuf_pos, insertmode);
				cont_loop = 1;
				break;
			}
			}
#ifdef LCC_DEBUG
mvwprintw(cwin_stat, 0, 30, "ttl=%i ul=%i p=%i ibqi=%i       ",
	  iqueue[ibqi][0].ttlen, iqueue[ibqi][0].ulen, ibuf_pos, ibqi); update_statwin = 1;
#endif
			if(cont_loop) continue;


			/* if loop gets here a command is completed and the buffer has been copied to bufcopy
			   ...now let's parse it */
			rtokenp = bufcopy;
			if(*rtokenp == '/') {
				const char *tt;
				rtokenp++;
				tt = lcc_strsepx(&rtokenp, LCC_TOKEN_DELIM);
				tncmd = ncmd_getnum(tt, strlen(tt));
			} else {
				if(!priv) {
					tncmd = C_SAY;
				} else {
					tncmd = C_SAY;//C_PSAY;
					//...blahblah...
				}
			}
			sendit = 0;
			switch(tncmd) {	//pointer bufp should be set after command (which is start of first argument)
			case -2:	//wrong!
				lcc_asay(1, "Sorry I don't know that command, try '/help' for help or make no typing errors.\n");
				break;
			case C_BS:	//local
			{
				const char *tt = lcc_strsepx(&rtokenp, LCC_TOKEN_DELIM);
				if(tt) {
					if(!isalnum((int)*tt) && *tt != '/') {
						backspace_char = *tt;
						lcc_asay(1, "Backspace key set to %#o.\n", backspace_char);
					} else {
						lcc_asay(1, "Sorry, refusing to set alphanumeric chars or '/' as backspace key.\n");
					}
				} else lcc_asay(1, "Please give a key as argument.\n");
				break;
			}
			case C_CLEAR:	//local
				wclear(cwin_out); update_outwin = 1;
				break;
			case C_HELP:	//local
			{
				const char *tt = lcc_strsepx(&rtokenp, LCC_TOKEN_DELIM);

				if(!tt)
					lcc_asay(4, "%s", lcc_help_general);
				else if((strcmp(tt, "c") == 0) || (strcmp(tt, "cmd") == 0) ||
					(strcmp(tt, "cmds") == 0) || (strcmp(tt, "commands") == 0))
					lcc_asay(4, "%s", lcc_help_commands);
				else if((strcmp(tt, "i") == 0) || (strcmp(tt, "iface") == 0) ||
					(strcmp(tt, "interface") == 0))
					lcc_asay(4, "%s", lcc_help_interface);
				else
					lcc_asay(1, "Unknown help topic, please leave out argument for a list.\n");
				break;
			}
			case C_LOG:	//local
				if(logfile) {
					if(closelog() == 0) lcc_asay(1, "session log closed\n");
					else lcc_asay(3, "error closing session log\n");
				} else {
					if(openlog(LCC_DFL_LOGFILE) == 0)
						lcc_asay(1, "session log opened to %s\n", LCC_DFL_LOGFILE);
					else
						lcc_asay(3, "error opening session log to '%s'\n", LCC_DFL_LOGFILE);
				}
				stupd = 1;
				break;
			case C_PRIV:	//local
				lcc_asay(1, "Command not implemented.\n");
				//if priv!=NULL set priv to nick otherwise free priv and set priv to NULL
				break;
			case C_QUIT:	//local
			{
				lcc_skip_to_token_start(&rtokenp, LCC_TOKEN_DELIM);
				if(rtokenp) *reason = strdup(rtokenp);
				lcc_asay(1, "Quitting...\n");
				exitloop = 1;
				break;
			}
			case C_PSAY: case C_SAY: case C_ME:	//client->server but all args (except the first for /psay) count as one
				ncmd_set(&sbuf, &sbuf_len, tncmd);
				if(tncmd == C_PSAY) {
					const char *tt = lcc_strsepx(&rtokenp, LCC_TOKEN_DELIM);
					if(tt && tt[0] != '\0') {
						ncmd_add(&sbuf, &sbuf_len, tt, strlen(tt));
					} else {
						lcc_asay(1, "Unable to find nick in /psay command.\n");
						break;
					}
				}
				lcc_skip_to_token_start(&rtokenp, LCC_TOKEN_DELIM);
				//fixme: this is ugly but it works (sends empty message in case of nullpointer)
				if(rtokenp) ncmd_add(&sbuf, &sbuf_len, rtokenp, strlen(rtokenp));
				else ncmd_add(&sbuf, &sbuf_len, "", 0);
				sendit = 1;
				break;
			case C_AWAY:
				lcc_skip_to_token_start(&rtokenp, LCC_TOKEN_DELIM);
				ncmd_set(&sbuf, &sbuf_len, tncmd);
				if(rtokenp) ncmd_add(&sbuf, &sbuf_len, rtokenp, strlen(rtokenp));
				sendit = 1;
				break;
			case C_WHO:
			{
				char itype = 0;	//0=chat 1=sys 2=all
				const char *tt = lcc_strsepx(&rtokenp, LCC_TOKEN_DELIM);

				if(!tt)
					;	//nothing to be done in this case
				else if((strcmp(tt, "c") == 0) || (strcmp(tt, "chat") == 0))
					itype = 0;
				else if((strcmp(tt, "s") == 0) || (strcmp(tt, "sys") == 0) ||
					(strcmp(tt, "system") == 0))
					itype = 1;
				else if((strcmp(tt, "a") == 0) || (strcmp(tt, "all") == 0))
					itype = 2;
				else {
					lcc_asay(1, "Unknown argument for who command.\n");
					break;
				}

				ncmd_set(&sbuf, &sbuf_len, tncmd);
				ncmd_add(&sbuf, &sbuf_len, &itype, 1);
				sendit = 1;
				break;
			}
			case C_BAN: case C_BEEP: case C_DEOP: case C_KICK: case C_NICK:
			case C_OP: case C_OPER: case C_UNBAN:
if(tncmd == C_BAN || tncmd == C_UNBAN) {
	lcc_asay(1, "banning is temporarily disabled (until it works well...)\n");
	break;
}
if(tncmd == C_BEEP) {
	lcc_asay(1, "looking for attention...\n");
}
				ncmd_set(&sbuf, &sbuf_len, tncmd);
				while(rtokenp) {
					const char *tt = lcc_strsepx(&rtokenp, LCC_TOKEN_DELIM);
					if(tt) ncmd_add(&sbuf, &sbuf_len, tt, strlen(tt));
				}
				sendit = 1;
				break;
			case C_DOOR: default:
				lcc_asay(1, "You typed command %s, don't know what to do.\n", ncmd_getname(tncmd));
				break;
			}

			if(sendit && sendbuf(connfd, sbuf, sbuf_len, 0) == -1) {
				lcc_asay(3, "error sending command (%s)\n", strerror(errno));
			}

			if(ibqi != 0) {
				free(iqueue[0][0].ibuf); iqueue[0][0] = iqueue[ibqi][0];
				iqueue[ibqi][0] = iqueue[ibqi][1]; iqueue[ibqi][1].ibuf = NULL;
			}
			free(iqueue[CMDLINE_HISTLEN-1][0].ibuf); free(iqueue[CMDLINE_HISTLEN-1][1].ibuf);
{
int i;
			for(i=CMDLINE_HISTLEN-1; i>0; i--) {
				if(iqueue[ibqi][1].ibuf != NULL) {free(iqueue[i-1][1].ibuf); iqueue[i-1][1].ibuf = NULL;}
				iqueue[i][0] = iqueue[i-1][0];
			}
}
			iqueue[0][0].ibuf = NULL;
			ibqi = 0;
			ibuf_pos = 0;
			werase(cwin_in); update_inwin = 1;
		}
		if(FD_ISSET(connfd, &read_fds)) {
			NCMDOp ttncmd;
			char *targ0 = NULL, *targ1 = NULL;
			long int targ0_len = 0, targ1_len = 0;
			int n_bytes = recvbuf(connfd, &rbuf, &rbuf_len, 0);

			if(n_bytes == -1) {	//error
				lcc_asay(3, "error receiving data from server (%s)\n", strerror(errno));
				continue;
			} else if(n_bytes == 0) {	//connection closed
				if(kicked == 2) lcc_asay(2, "Server closed connection because you are banned. Quitting...\n");
				else if(kicked == 1) lcc_asay(2, "Server closed connection because you are kicked. Quitting...\n");
				else lcc_asay(2, "Server closed connection without warning. Quitting...\n");
				rv = -2;
				break;
			}

			while(ncmd_iscomplete(rbuf, rbuf_len)) {
				//if we got here rbuf contains a(nother) complete netcommand...let's check it out!
				switch((ttncmd = ncmd_get(rbuf, rbuf_len))) {
				case C_DOOR:
					ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 0);
					ncmd_getarg(rbuf, rbuf_len, &targ1, &targ1_len, 1, 1);
					if(targ0[0] == 1) {
						lcc_asay(2, "%s entered chat\n", targ1);
					} else {
						if(ncmd_getnumargs(rbuf, rbuf_len) > 2) {
							ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 2, 1);
							lcc_asay(2, "%s left chat (%s)\n", targ1, targ0);
						} else {
							lcc_asay(2, "%s left chat\n", targ1);
						}
					}
					req_who = 1;
					break;
				case C_AWAY:
					//fixme? no check for enough arguments present
					ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 1);
					ncmd_getarg(rbuf, rbuf_len, &targ1, &targ1_len, 1, 1);
					if(targ0[0] == 0) lcc_asay(2, "%s is not away anymore", targ1);
					else lcc_asay(2, "%s is now away", targ1);
					if(strcmp(targ1, nick) == 0) {
						stupd = 1;
						if(targ0[0] == 0) flags &= ~UDC_FLAG_AWAY;
						else flags |= UDC_FLAG_AWAY;
					}
					if(ncmd_getnumargs(rbuf, rbuf_len) == 3) {
						ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 2, 1);
						lcc_asay(0, " (%s)\n", targ0);
					} else {
						lcc_asay(0, "\n");
					}
					req_who = 1;
					break;
				case C_ME:
					//fixme? no check for enough arguments present
					ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 1);
					ncmd_getarg(rbuf, rbuf_len, &targ1, &targ1_len, 1, 1);

					lcc_attrset(cwin_out, LCC_OSE_C); lcc_asay(0, LCC_OEMOTE_S);
					lcc_attrset(cwin_out, LCC_OEMOTE_C); lcc_asay(0, "%s %s", targ0, targ1);
					lcc_attrset(cwin_out, LCC_OSE_C); lcc_asay(0, LCC_OEMOTE_E"\n");
					lcc_attrset(cwin_out, LCC_ODEFAULT_C);
					break;
				case C_OPER:
					if(ncmd_getnumargs(rbuf, rbuf_len) > 0) {
						ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 1);
						lcc_asay(2, "mode +op %s\n", targ0);
						if(strcmp(targ0, nick) == 0) {
							flags |= UDC_FLAG_OPERATOR;
							stupd = 1;
						}
					} else {
						lcc_asay(2, "you did not supply the correct operator password\n");
					}
					req_who = 1;
					break;
				case C_PSAY:
					switch(ncmd_getnumargs(rbuf, rbuf_len)) {
					case 1:
						ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 1);
						lcc_asay(2, "you tried to sent a private message to %s who is not present\n", targ0);
						break;
					case 2:
						ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 1);
						ncmd_getarg(rbuf, rbuf_len, &targ1, &targ1_len, 1, 1);
						lcc_attrset(cwin_out, LCC_OSE_C); lcc_asay(0, LCC_OMSG_S);
						lcc_attrset(cwin_out, LCC_OMSG_C); lcc_asay(0, "(p)%s", targ0);
						lcc_attrset(cwin_out, LCC_OSE_C); lcc_asay(0, LCC_OMSG_E);
						lcc_attrset(cwin_out, LCC_ODEFAULT_C); lcc_asay(0, " %s\n", targ1);
						break;
					case 3:
						ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 1);
						ncmd_getarg(rbuf, rbuf_len, &targ1, &targ1_len, 1, 1);
						lcc_attrset(cwin_out, LCC_OSE_C); lcc_asay(0, LCC_OMSG_S);
						lcc_attrset(cwin_out, LCC_OMSG_C); lcc_asay(0, "(p)%s->%s", targ0, targ1);
						lcc_attrset(cwin_out, LCC_OSE_C); lcc_asay(0, LCC_OMSG_E);
						ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 2, 1);
						lcc_attrset(cwin_out, LCC_ODEFAULT_C); lcc_asay(0, " %s\n", targ0);
						break;
					default:
						lcc_asay(2, "server sent a psay command with wrong number of args\n");
						break;
					}
					break;
				case C_NICK:
				{
					long int na = ncmd_getnumargs(rbuf, rbuf_len);
					if(na == 0) {	//this a notification the nick you wanted is already in use
						lcc_asay(2, "the nick you want is already in use\n", targ0);
					} else if(na == 1) {	//this is the response to an nick request
						ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 1);
						free(nick); nick = strdup(targ0); stupd = 1;
						lcc_asay(2, "you're known as %s here\n", targ0);
					} else if(na == 2) {	//this is an nick change notification
						ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 1);
						ncmd_getarg(rbuf, rbuf_len, &targ1, &targ1_len, 1, 1);
						if(strcmp(targ1, nick) == 0) {
							free(nick);
							nick = strdup(targ0);
							stupd = 1;
						}
						lcc_asay(2, "%s is now called %s\n", targ1, targ0);
					} else {
						lcc_asay(2, "server sent an nick command with wrong number of args\n");
					}
					req_who = 1;
					break;
				}
				case C_SAY:
				{
					int ign_chars = (flags&UDC_FLAG_AWAY ? 1 : 0) + (flags&UDC_FLAG_OPERATOR ? 1 : 0);
					//fixme? no check for enough arguments present
					ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 1);
					ncmd_getarg(rbuf, rbuf_len, &targ1, &targ1_len, 1, 1);

					lcc_attrset(cwin_out, LCC_OSE_C); lcc_asay(0, LCC_OSAY_S);
					if(strcmp(targ0+ign_chars, nick) == 0) lcc_attrset(cwin_out, LCC_OSAYSELF_C);
					else if(strstr(targ1, nick)) lcc_attrset(cwin_out, LCC_OSAYPERS_C);
					else lcc_attrset(cwin_out, LCC_OSAY_C);
					lcc_asay(0, "%s", targ0);
					lcc_attrset(cwin_out, LCC_OSE_C); lcc_asay(0, "]");
					lcc_attrset(cwin_out, LCC_ODEFAULT_C); lcc_asay(0, " %s\n", targ1);
					break;
				}
				case C_BAN:
				{
					int na = ncmd_getnumargs(rbuf, rbuf_len);
					if(na == 0) {
						lcc_asay(2, "you can't ban anyone because you're not an operator (or wrong # of args)\n");
					} else if(na == 1) {
						ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 1);
						lcc_asay(2, "%s can't be banned because he/she is not present\n", targ0);
					} else {
						ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 1);
						ncmd_getarg(rbuf, rbuf_len, &targ1, &targ1_len, 1, 1);
						lcc_asay(2, "%s is banned by %s\n", targ0, targ1);
						if(strcmp(targ0, nick) == 0) kicked = 2;
					}
					break;
				}
				case C_BEEP:
					if(ncmd_getnumargs(rbuf, rbuf_len) == 0) {
						//fixme: tell also who seeks your attention
						lcc_asay(2, "someone needs attention...\n");
						beep();
					} else {
						ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 1);
						lcc_asay(2, "you can't beep the terminal of %s because he/she is not present\n", targ0);
					}
					break;
				case C_DEOP:
				{
					int na = ncmd_getnumargs(rbuf, rbuf_len);
					if(na == 0) {
						lcc_asay(2, "you can't deop anyone because you're not an operator (or wrong # of args)\n");
					} else if(na == 1) {
						ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 1);
						lcc_asay(2, "%s can't be de-opped because he/she is not present\n", targ0);
					} else {
						ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 1);
						ncmd_getarg(rbuf, rbuf_len, &targ1, &targ1_len, 1, 1);
						lcc_asay(2, "mode -op %s by %s\n", targ0, targ1);
						if(strcmp(targ0, nick) == 0) {
							flags &= ~UDC_FLAG_OPERATOR;
							stupd = 1;
						}
					}
					req_who = 1;
					break;
				}
				case C_KICK:
				{
					int na = ncmd_getnumargs(rbuf, rbuf_len);
					if(na == 0) {
						lcc_asay(2, "you can't kick anyone because you're not an operator (or wrong # of args)\n");
					} else if(na == 1) {
						ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 1);
						lcc_asay(2, "%s can't be kicked because he/she is not present\n", targ0);
					} else {
						ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 1);
						ncmd_getarg(rbuf, rbuf_len, &targ1, &targ1_len, 1, 1);
						lcc_asay(2, "%s is kicked by %s\n", targ0, targ1);
						if(strcmp(targ0, nick) == 0) kicked = 1;
					}
					req_who = 1;
					break;
				}
				case C_OP:
				{
					int na = ncmd_getnumargs(rbuf, rbuf_len);
					if(na == 0) {
						lcc_asay(2, "you can't op anyone because you're not an operator (or wrong # of args)\n");
					} else if(na == 1) {
						ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 1);
						lcc_asay(2, "%s can't be opped because he/she is not present\n", targ0);
					} else {
						ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 1);
						ncmd_getarg(rbuf, rbuf_len, &targ1, &targ1_len, 1, 1);
						lcc_asay(2, "mode +op %s by %s\n", targ0, targ1);
						if(strcmp(targ0, nick) == 0) {
							flags |= UDC_FLAG_OPERATOR;
							stupd = 1;
						}
					}
					req_who = 1;
					break;
				}
				case C_UNBAN:
				{
					int na = ncmd_getnumargs(rbuf, rbuf_len);
					if(na == 0) {
						lcc_asay(2, "you can't unban anyone because you're not an operator (or wrong # of args)\n");
					} else if(na == 1) {
						ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 1);
						lcc_asay(2, "%s can't be unbanned because he/she is not present\n", targ0);
					} else {
						ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 1);
						ncmd_getarg(rbuf, rbuf_len, &targ1, &targ1_len, 1, 1);
						lcc_asay(2, "%s is unbanned by %s\n", targ0, targ1);
						if(strcmp(targ0, nick) == 0) kicked = 0;
					}
					break;
				}
				case C_WHO:
				{
//if itype is 0, update the sidebar, otherwise dump old-style list to chatwin
					int i = 0, itype = 0, na = ncmd_getnumargs(rbuf, rbuf_len);
					if(na & 1) {	//if odd number of arguments the first one is infotype
						ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, 0, 1);
						if(targ0 && *targ0 < 3) itype = *targ0;
						i = 1;
					}

					switch(itype) {
					//case 0: lcc_asay(2, "Updating sidebar with chatting users\n"); break;
					case 1: lcc_asay(2, "Persons logged on to system\n"); break;
					case 2: lcc_asay(2, "Persons in chat and on system\n"); break;
					}

					if(itype == 0) lcc_culist_clear();
					for(/*i is inited*/; i<na; i+=2) {
						ncmd_getarg(rbuf, rbuf_len, &targ0, &targ0_len, i, 1);
						if(itype == 0) {
							lcc_culist_add(targ0);
						} else {
							ncmd_getarg(rbuf, rbuf_len, &targ1, &targ1_len, i+1, 1);
							lcc_asay(2, "%-20s %s\n", targ0, targ1);
						}
					}

					if(itype == 0) lcc_sidebar_repaint();
					break;
				}
				default:
					lcc_asay(2, "Received netcommand %s from server, ignoring.\n", ncmd_getname(ttncmd));
					break;
				}

				if(req_who) {
				        ncmd_set(&sbuf, &sbuf_len, C_WHO);
				        if(sendbuf(connfd, sbuf, sbuf_len, 0) == -1) {
				                lcc_asay(3, "error sending command (%s)\n", strerror(errno));
				        }
					req_who = 0;
				}

				ncmd_remove(&rbuf, &rbuf_len);
			}
		}
	}

//******* de-init
	endwin();
	if(logfile) fclose(logfile);
	return rv;
}
