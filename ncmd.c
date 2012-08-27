#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "ncmd.h"
//#include "smsg.h"

#define LNCMD_BUF_SIZE 1024

//stores value val as doubleword in network byte order in *p (and second byte in *(p+1), etc.)
void store_nl(char *p, unsigned long int val)
{
	*p = (val & 0xFF000000) >> 24;
	*(p+1) = (val & 0xFF0000) >> 16;
	*(p+2) = (val & 0xFF00) >> 8;
	*(p+3) = val & 0xFF;
}
//reads a doubleword in network byte order from *p (and second byte from *(p+1), etc.)
unsigned long int read_nl(const char *p)
{
	unsigned long int r;
	r = (*(p) & 0xFF) << 24;
	r += (*(p+1) & 0xFF) << 16;
	r += (*(p+2) & 0xFF) << 8;
	r += *(p+3) & 0xFF;
	return r;
}

//stores value val in network byte order in *p (and second byte in *(p+1))
void store_ns(char *p, unsigned short int val)
{
	*p = (val & 0xFF00) >> 8;
	*(p+1) = val & 0xFF;
}
//reads a word in network byte order from *p (and second byte from *(p+1))
unsigned short int read_ns(const char *p)
{
	unsigned short int r;
	r = (*(p) & 0xFF) << 8;
	r += *(p+1) & 0xFF;
	return r;
}


//returns the name of command ncmd_code if it exists, otherwise it
//returns a string containing the command code
//space for the returned string is reallocated each time
//char *ncmd_getname(unsigned short int ncmd_code)
char *ncmd_getname(NCMDOp ncmd_code)
{
	static char *s = NULL;

	free(s);
	s = NULL;	//in case strdup() fails, s still has a valid value
	if(ncmd_code <= ncmd_max_code && ncmds[ncmd_code].name) {
		if((s = strdup(ncmds[ncmd_code].name)) == NULL) {
			//fixme: how should this be done? errorprinting by subsystems seems not good
			fprintf(stderr, "string duplication error (%s)\n", strerror(errno));
			return NULL;
		}
	} else {
		if((s = malloc(6)) == NULL) {
			//fixme: how should this be done? errorprinting by subsystems seems not good
			fprintf(stderr, "memory allocation error (%s)\n", strerror(errno));
			return NULL;
		}
		snprintf(s, 6, "%i", ncmd_code);
		*(s+5) = '\0';
	}

	return s;
}

//returns command number for the command contained in s or -2 if no such command exists
NCMDOp ncmd_getnum(const char *s, long int sl)
{
	NCMDOp ncmd = -2;
	int i;
	for(i=0; ncmds[i].name; i++) {
		//due to the length thing (sl), abbreviated commands also match ("h"=="help" if only looking at char 1...)
		//so abbreviated commands work, to prevent this us this if:
#ifndef NO_ABBREVIATED_COMMANDS_PLEASE
		if(strncasecmp(ncmds[i].name, s, sl) == 0 && strlen(ncmds[i].name) == sl) {
#else
		if(strncasecmp(ncmds[i].name, s, sl) == 0) {
#endif
			ncmd = ncmds[i].opcode;
			break;
		}
	}
	return ncmd;
}

/*
 * functions for parsing/reading/assembling commands
 *
 * NOTE: make sure you only give buffers validated by ncmd_iscomplete()
 * to the other function for they don't check for enough room etc.
 */

//returns command size if complete command in buf found, otherwise 0
int ncmd_iscomplete(const char *buf, unsigned long int len)
{
	unsigned int argsleft;
	const char *i;

	if(len < 4) return 0;
	i = buf + 2;
	argsleft = read_ns(i);
	i += 2;
	while(argsleft > 0) {
		argsleft--;
		if(buf+len < i+4) return 0;
		if(buf+len < i+read_nl(i)+4) return 0;
		i += read_nl(i) + 4;
	}

	return i - buf;
}

//returns opcode of command in buf
NCMDOp ncmd_get(const char *buf, unsigned long int len)
{
	return read_ns(buf);
}

//returns number of arguments of command in buf
unsigned short int ncmd_getnumargs(const char *buf, unsigned long int len)
{
	return read_ns(buf+2);
}

//reallocates ap and puts the requested argument in it
//if the argument does not exist or the memory cannot be allocated,
//-1 is returned, 0 will be returned otherwise
//NOTE: make sure ap points to allocated memory or is NULL
int ncmd_getarg(char *buf, unsigned long int len, char **ap, unsigned long int *aplen, unsigned int argnum, int addzero)
{
	char *i, *t;
	int currarg = 0;

	if(argnum >= read_ns(buf+2)) return -1;
	i = buf + 4;
	while(currarg < argnum) {
		i += read_nl(i) + 4;
		currarg++;
	}
	if((t = realloc(*ap, read_nl(i)+addzero)) == NULL) return -1;
	*ap = t;
	memcpy(*ap, i+4, read_nl(i));
	if(addzero) *(*ap+read_nl(i)) = '\0';
	*aplen = read_nl(i) + addzero;

	return 0;
}

//removes the first complete command (has to be at start of buffer) from buf
int ncmd_remove(char **buf, unsigned long int *len)
{
	long int cl = ncmd_iscomplete(*buf, *len);
	char *t;

	if(cl == 0) return -1;
	memmove(*buf, *buf+cl, *len-cl);
	errno = 0;
	if((t = realloc(*buf, *len-cl)) == NULL && errno != 0) return -1;
	*buf = t;
	*len = *len - cl;

	return 0;
}



/*
 * functions for building commands
 */

//reallocs buf to 4 bytes, sets the first two to the command and
//sets the second two to 0 (means zero arguments)
int ncmd_set(char **buf, unsigned long int *len, NCMDOp ncmd)
{
	char *t;
	if((t = realloc(*buf, 4)) == NULL) return -1;
	*buf = t;
	store_ns(*buf, ncmd);
	store_ns(*buf+2, 0);
	*len = 4;
	return 0;
}

//adds argument s to buf (and of course reallocates buf)
//if s is a null pointer, an empty argument is added
int ncmd_add(char **buf, unsigned long int *buflen, const char *s, unsigned long int slen)
{
	char *t;

	//if(slen > 65535) return -1;	//this should be 2^32-1...don't think that's necessary
					//because bigger values don't fit in a long int
	if(s == NULL) {
		if((t = realloc(*buf, *buflen+4)) == NULL) return -1;
		*buf = t;
		store_nl(*buf+*buflen, 0);
		*buflen += 4;
		store_ns(*buf+2, read_ns(*buf+2)+1);	//increase number of arguments
	} else {
		if((t = realloc(*buf, *buflen+4+slen)) == NULL) return -1;
		*buf = t;
		store_nl(*buf+*buflen, slen);
		memcpy(*buf+*buflen+4, s, slen);
		*buflen += 4 + slen;
		store_ns(*buf+2, read_ns(*buf+2)+1);	//increase number of arguments
	}

	return 0;
}


//wrapper function for sento(), but makes sure the whole buffer gets sent (by sending multiple packets)
int sendbuf(int s, const void *msg, size_t len, int flags)
{
//	int sent = 0;
//	while(sent < len) {
//		if((sent = send(s, msg+sent, len-sent, flags)) == -1) return -1;
//	}
//	return len;
	return send(s, msg, len, flags);
}

//wrapper function for recvfrom(), but adds received data to buffer
int recvbuf(int s, char **buf, unsigned long int *len, int flags)
{
	char *t;
	char rbuf[LNCMD_BUF_SIZE];
	int rlen = LNCMD_BUF_SIZE - 1, rval;

	rval = recv(s, &rbuf, rlen, flags);

	if(rval > 0) {
		if((t = realloc(*buf, *len+rval)) == NULL) {
			//fixme: how should this be done? errorprinting by subsystems seems not good
			fprintf(stderr, "error reallocating memory for receive buffer (%s)\n", strerror(errno));
			return -1;
		}
		*buf = t;
		memcpy(*buf+*len, rbuf, rval);
		*len += rval;
	}

	return rval;
}
