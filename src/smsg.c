/*
 * this is the main file for smsg containing only the main function and some support functions
 * fixme: prefix all local things (not used for 'extern'!) with s (or something...)
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#ifdef HAVE_GETOPT_LONG
# include <getopt.h>
#endif
#include <libgen.h>
#include "smsg.h"

#define MODE_CHAT_NAME "chat"
#define MODE_CHAT_DFLHOST "localhost"

short int usegui = 0;

/*
 * say() and error() try to use the gui first if usegui != 0
 */
void say(char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
}
void error(char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
}

/*
 * DON'T give an uninitialized rdata to this function!!!
 * Either give a NULL pointer, or give a pointer to allocated memory (memory
 * will be realloc()ed)...
 */
int get_stdin(char **rdata)
{
	int n = 0;
	char *p;

	if((*rdata = malloc(MSG_MAX_LEN)) == NULL) return 1;
	p = *rdata;

	while((*p++ = (char)getchar()) != EOF && n < MSG_MAX_LEN - 1) n++;
	p--;
//set buffering mode to canonical
//	*p = (char)getchar(); n++;
//	while((char)*p != EOF && n < MSG_MAX_LEN-1) {
//		*p = (char)getchar();
//		p++;
//		n++;
//	}
//set buffering mode to line buffer
	*p = '\0';
	*rdata = realloc(*rdata, n);

	return 0;
}


int main(int argc, char *argv[])
{
	char *prog_name = NULL;
	char *msgdata = NULL;
	int i, c;
#ifdef HAVE_GETOPT_LONG
	int option_index = 0;
	const static struct option long_options[] = {
		{"help", 0, NULL, 'h'},
		{"nofork", 0, NULL, 'F'},
		{"gui", 0, NULL, 'g'},
		{"color", 0, NULL, 'c'},
		{"destination", 1, NULL, 'd'},
		{"dest", 1, NULL, 'd'},		//this isn't strictly necessary as it's an abbrev. of the full word
		{"port", 1, NULL, 'p'},
		{"tty", 1, NULL, 't'},
		{"beep", 1, NULL, 'b'},
		{"mode", 1, NULL, 'm'},
		{"loglevel", 1, NULL, 'l'},
		{"logfile", 1, NULL, 'L'},
		{"nick", 1, NULL, 'n'},
		{"sidebar", 1, NULL, 's'},
		{"bar", 1, NULL, 's'},
		{"erase", 1, NULL, 'e'},
		{"room", 1, NULL, 'r'},
		{0, 0, NULL, 0}
	};
#endif
	const static char *short_options = "-z:1FghxXcd:1p:1t:1b:1m:1l:1L:1n:1s:1e:1r:1";

// * I used \t instead of real tabs for in case this file gets fucked up by a lousy editor
// * the %s on the first line (for printf()) and the like are not totally
//   clean but it saves me a lot of trouble
	char *helpmsg = "Tiny messenger v%s -- written by Ed de Wonderkat and Matt Logsdon\n\
Usage: smgs command [arguments]\n\
These commands can be used:\n\
  help\t\tdisplays this help message and quits\n\
  server\tstarts a server if none is running yet\n\
  list\t\tdisplays a list of users logged on at target host\n\
\t\t(-d is required)\n\
  chat\t\tjoins you the chatroom (if -d is omitted localhost is used)\n\
  message\tsends a message to specified user on target host,\n\
\t\t(-d is required)\n\
  msg,mesg\tsame as message\n\
  get,getmsg\tretrieve the oldest queued message (if there is one)\n\
  prefs,pref\tsets your preferences on the local running server\n\n\
These arguments can be used:\n\
  -h,--help\taliases for the 'command' help\n\
  -z\t\talternative way to specify a command\n\
  -n,--nick\tsets nickname for chatting (your username by default)\n\
  --sidebar\tconfigures sidebar in chat, use l or r to place it on\n\
\t\tthe left or right side and use a number to specify width [disabled]\n\
  -s,--bar\taliases for --sidebar [disabled due to fuzzy resize code]\n\
  -e,--erasekey\tspecifies 'backspace alias' for chat, this might solve\n\
\t\tsome problems (stty like spec or number?)\n\
  -p,--port\tspecifies a target port, if used in conjunction with the\n\
\t\tserver command it specifies which port to run the server on\n\
  --destination\tspecifies a destination in the form 'host' or 'user@host'\n\
  -d,--dest\taliases for --destination\n\
  -t,--tty\tspecifies a tty (only for prefs command, not for messages!)\n\
\t\tto remove your preference use '%s' and to set it to your\n\
\t\tcurrent tty use '%s' or '%c'\n\
  -c,--color\tforce usage of colors in chatclient\n\
  -b,--beep\tswitches beeping on message arrival on/off\n\
\t\t(only for prefs command, not for messages!)\n\
  -m,--mode\tsets delivery mode, use 'i' for immediate delivery or\n\
\t\t'q' for queued delivery (use getmsg command to retrieve them)\n\
  -F,--nofork\tprevent server from forking to background\n\
\t\t(only used in conjunction with server command)\n\
  -g,-X,--gui\tthe actions taken will try to use the gui, if that fails\n\
\t\tconsole will be used\n\
  -l,--loglevel\tsets the loglevel (default 0), use -1 or below to disable\n\
\t\tlogging (currently only two levels exist)\n\
  -L,--logfile\tsets the file to log to, stderr is used by default\n\n\
Messages are read from standard input (in case of console usage of course).\n\
This program takes the messages on/off setting on *nix systems into account.\n";

	typedef enum cmdcode {help, server, list, chat, message, getmsg, prefs} cmdcode;
	struct cmd {
		char *name;
		cmdcode code;
	};
	static struct cmd cmds[] = {
		{"help", help},
		{"server", server},
		{"list", list},
		{"chat", chat},
		{"msg", message},
		{"mesg", message},
		{"message", message},
		{"getmsg", getmsg},
		{"get", getmsg},
		{"prefs", prefs},
		{"pref", prefs},
		{NULL, -1}
	};

	cmdcode command = -1;
	char *tptr;
	short int tport = -1, port;
	short int autofork = 1;	//do fork by default
	//fixme: what if the compiler default char to unsigned char?
	char beepspec = -2, mode = -2;	//no preferences by default
	char *targethost = NULL, *targetuser = NULL;
	char *ttyspec = NULL, *nick = NULL;
	//barspec def: -1=none,lower 9bits=width(0x1ff is unset),10th bit=put on right (if set)
	//so, width=barspec&0x1FF and onright=barspec&0x200
	struct chatopts_s chatopts = {0, -1, -1, 0, NULL};

	if((prog_name = strdup(basename(argv[0]))) == NULL) {
		error("%s: wow...I could not duplicate my own name...(%s)", argv[0], strerror(errno));
		exit(1);
	}

	while (1) {
#ifdef HAVE_GETOPT_LONG
		c = getopt_long(argc, argv, short_options, long_options, &option_index);
#else
		c = getopt(argc, argv, short_options);
#endif
		if(c == -1) break;

		switch (c) {
		case 'z':
		case 1:
			if(command != -1) {
				error("%s: more than one command found\n", prog_name);
				exit(1);
			}
			i = 0;
			while(cmds[i].name != NULL) {
				if(strcmp(optarg, cmds[i].name) == 0) {
					command = cmds[i].code;
					break;
				}
				i++;
			}
			if(command == -1) {
				error("%s: unknown command: `%s', try help\n", prog_name, optarg);
				exit(1);
			}
			break;
		case 'g': case 'x': case 'X':
			usegui = 1;
			break;
		case 'c':
			chatopts.forcecolor = 1;
			break;
		case 'h':
			command = help;
			break;
		case 'F':
			autofork = 0;
			break;
		case 'p':
			tport = strtol(optarg, &tptr, 10);
			if(tptr[0] != '\0') {
				error("%s: found incorrect argument for port, please specify a number\n", prog_name);
				exit(1);
			} else if(tport < 1024) {
				error("%s: incorrect port: %i (must be above 1023 and below 65535)\n", prog_name, tport);
				exit(1);
			}
			break;
		case 'd':
			tptr = index(optarg, '@');
			if(tptr == NULL) {
				if((targethost = strdup(optarg)) == NULL) {
					error("%s: error copying hostname (probably due to insufficient memory)\n", prog_name);
					exit(1);
				}
			} else {
				if((targethost = strdup(tptr+1)) == NULL) {
					error("%s: error copying hostname (probably due to insufficient memory)\n", prog_name);
					exit(1);
				}
				if((targetuser = malloc(tptr-optarg+1)) == NULL) {
					error("%s: error copying username (probably due to insufficient memory)\n", prog_name);
					exit(1);
				}
				memcpy(targetuser, optarg, tptr-optarg);
				*(targetuser+(tptr-optarg)+1) = '\0';
			}
			break;
		case 't':
			//warning on (xstrlen(optarg) > LMAX_LINE)?
			if(strcmp(optarg, TTYSET_THISTTY) == 0 ||
			   (xstrlen(optarg) == 1 && optarg[0] == TTYSET_THISTTY[0])) {
				ttyspec = ttyname(1);	//or would stdin be better?
				if(ttyspec == NULL) {
					error("%s: error getting tty name\n", prog_name);
					exit(1);
				}
			} else {
				ttyspec = strdup(optarg);
				if(ttyspec == NULL) {
					error("%s: error duplicating tty specification (%s)\n", prog_name, strerror(errno));
					exit(1);
				}
			}
			break;
		case 'b':
			if(strcmp(optarg, "on") == 0 || strcmp(optarg, "yes") == 0 || strcmp(optarg, "1") == 0) {
				beepspec = 1;
			} else if(strcmp(optarg, "off") == 0 || strcmp(optarg, "no") == 0 || strcmp(optarg, "0") == 0) {
				beepspec = 0;
			} else {
				error("%s: incorrect argument for beep option specified, use 'on', 'off' and the like\n", prog_name);
				exit(1);
			}
			break;
		case 'm':
			if(strcmp(optarg, "i") == 0 || strcmp(optarg, "imm") == 0 || strcmp(optarg, "immediate") == 0) {
				mode = 0;
			} else if(strcmp(optarg, "q") == 0 || strcmp(optarg, "queue") == 0 || strcmp(optarg, "queued") == 0) {
				mode = 1;
			} else {
				error("%s: incorrect argument for mode option specified, use 'q' or 'i'\n", prog_name);
				exit(1);
			}
			break;
		case 'l':
			loglevel = strtol(optarg, &tptr, 10);
			if(tptr[0] != '\0') {
				error("%s: found incorrect argument for loglevel, please specify a number\n", prog_name);
				exit(1);
			}
			break;
		case 'L':
			logfilename = strdup(optarg);
			if(logfilename == NULL) {
				error("%s: error duplicating logging filename (%s)\n", prog_name, strerror(errno));
				exit(1);
			}
			break;
		case 'n':
			nick = strdup(optarg);
			if(nick == NULL) {
				error("%s: error duplicating nick (%s)\n", prog_name, strerror(errno));
				exit(1);
			}
			break;
		case 's':
		{
error("%s: sidebar customization has been disabled temporarily due to resize code bugs [incompleteness]\n", prog_name);
exit(1);
#if 0
			char *p = optarg;
			int i;
			chatopts.barspec = 0x1FF;
			if(p[0] == 'l' || p[0] == 'L') {
				chatopts.barspec = 0x1FF;
				p++;
			} else if(p[0] == 'r' || p[0] == 'R') {
				chatopts.barspec = 0x3FF;
				p++;
			}
			i = strtol(p, &tptr, 10);
			if(tptr[0] != '\0' || (tptr == p && p == optarg)) {
				error("%s: incorrect argument for barspec, please specify l/r and/or a number\n", prog_name);
				exit(1);
			} else if(p == optarg && (i < 0 || i > 0x1FE)) {
				error("%s: incorrect argument for barspec, please use a number >= 0 and <= 510\n", prog_name);
				exit(1);
			} else {
				chatopts.barspec &= 0x200;
				chatopts.barspec |= i;
			}
#endif
			break;
		}
		case 'e':
			chatopts.erase_key = strtol(optarg, &tptr, 0);
			if(tptr[0] != '\0') {
				error("%s: found incorrect argument for erasekey, please specify a number\n", prog_name);
				exit(1);
			} else if(chatopts.erase_key < 1 || chatopts.erase_key > 255
			          || isalnum(chatopts.erase_key) || chatopts.erase_key == '/') {
				error("%s: incorrect erasekey: 0%o (may not be below 1, above 255, alphanumeric or '/')\n", prog_name, chatopts.erase_key);
				exit(1);
			}

say("erase_key: '%c' == 0%o == 0x%x == %i\n", chatopts.erase_key, chatopts.erase_key, chatopts.erase_key, chatopts.erase_key);
			break;
		case 'r':
			chatopts.room = strdup(optarg);
			if(chatopts.room == NULL) {
				error("%s: error duplicating roomname (%s)\n", prog_name, strerror(errno));
				exit(1);
			}
			break;
		case '?':
			//error("%s: unknown option found\n", prog_name);
			exit(1);
			break;
		default:
			error("%s: getopt returned character code 0%o ...what did you do??\n", prog_name, c);
			//exit(1);
		}
	}
	if(optind < argc) error("%s: unknown arguments found\n", prog_name);

	port = (tport == -1 ? PORT_DEFAULT : tport);
	/* lol I can write obfuscated c too!! (this filters illegal chars from the nick by the way)) */
	if(nick){char *p=nick;for(;*p;p++)if(*p<33||*p>126)memmove(p,p+1,strlen(p)+1);}

//fixme? quick hack to let chat linked to smsg work as 'smsg chat'
	if(strcmp(prog_name, MODE_CHAT_NAME) == 0) command = chat;

	switch(command) {
	case server:
		if(runserver(port, autofork) == -1) {
			error("server error\n");
		}
		break;
	case message:
		if(targethost == NULL || xstrlen(targethost) == 0) {
			error("%s: no destination given\n", prog_name);
			exit(1);
		}
		if(usegui) {
#ifdef WITH_X
			//just start gui interface here with message handling in front
			//gui_iface(0, ...all data...);	//or something alike...
			say("%s: een schaap zou een aap zijn zonder de sch\n", prog_name);
#else
			say("%s: ehh sorry...no X support is compiled into this binary\n", prog_name);
#endif
		} else {
			say("Please enter your message:\n");
			if(get_stdin(&msgdata) != 0) {
				error("%s: error getting message data from standard input\n", prog_name);
				exit(1);
			}
			sendmessage(msgdata, targethost, targetuser, port);
		}
		break;
	case getmsg:
		if(usegui) {
#ifdef WITH_X
			//just start gui interface here with message handling in front
			//gui_iface(0, ...all data...);	//or something alike...
			say("%s: ...helaas helaas...\n", prog_name);
#else
			say("%s: ehh sorry...no X support is compiled into this binary\n", prog_name);
#endif
		} else {
			getmessage(port);
		}
		break;
	case list:
		if(targethost == NULL) {
			error("%s: no destination given\n", prog_name);
			exit(1);
		}
		if(usegui) {
#ifdef WITH_X
			//just start gui interface here with list panel in front
			//gui_iface(1, ...all data...);
			say("%s: en als de aket niet bestond zou het een koe zijn...\n", prog_name);
#else
			say("%s: this binary doesn't support X...bad luck\n", prog_name);
#endif
		} else {
			getlist(targethost, port);
		}
		break;
	case chat:
		if(!targethost) {
			if((targethost = strdup(MODE_CHAT_DFLHOST)) == NULL) {
				error("%s: error copying hostname (probably due to insufficient memory)\n", prog_name);
				exit(1);
			}
		}
chatopts.mailchecktime = 5;
		chatter(nick, targethost, port, &chatopts);
		break;
	case prefs:
		if(usegui) {
			error("%s: setting preferences for a gui interface makes no sense\n", prog_name);
			//hmm sure? about error that is...
		} else {
			sendprefs(ttyspec, beepspec, mode, port);
		}
		break;
	case help:
		say(helpmsg, VERSION, TTYSET_UNSET, TTYSET_THISTTY, TTYSET_THISTTY[0]); //not quite clean...but easy for sure
#ifndef HAVE_GETOPT_LONG
		say("\nWARNING: due to lack of support for getopt_long() only the one-char arguments work.\n");
#endif
		break;
	default:
		error("%s: no command found, try 'help'\n", prog_name);
		exit(1);
	}

	exit(0);
}
