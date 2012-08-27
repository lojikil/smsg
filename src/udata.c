/*
 * this file contains functions to manage user preferences and chat information (only used by the server)
 */
#include <stdlib.h>
#include <string.h>
#include "smsg.h"

//this is temporary for the udata_* tracing log messages
static int log_indent = 0;

static struct udata_ent *udata_start = NULL;

//returns pointer to entry for user *user if present, otherwise NULL
struct udata_ent *udata_get(char *user)
{
	struct udata_ent *e = udata_start;
	if(!user) return NULL;
slog(2, " udata_get      %*c\\: trying to get udata_ent for '%s'", ++log_indent, ' ', user);
	while(e != NULL) {
		if(e->user && strcmp(user, e->user) == 0) break;
		e = e->next;
	}
slog(2, " udata_get      %*c/: returning 0x%x for '%s'", log_indent--, ' ', e, user);
	return e;
}

//returns pointer to entry for user with *nick if found and present in chat, otherwise NULL
struct udata_ent *udata_get_by_nick(char *nick)
{
	struct udata_ent *e = udata_start;
	if(!nick) return NULL;
slog(2, " udata_get_by_nick      %*c\\: trying to get udata_ent with nick '%s' which is to be present in chat", ++log_indent, ' ', nick);
	while(e != NULL) {
		if(e->fd >= 0 && (e->flags&UDC_FLAG_PRESENT) && e->nick && strcmp(nick, e->nick) == 0) break;
		e = e->next;
	}
slog(2, " udata_get_by_nick      %*c/: returning 0x%x for chatting user with nick '%s'", log_indent--, ' ', e, nick);
	return e;
}

struct udata_ent *udata_get_by_fd(int fd)
{
	struct udata_ent *e = udata_start;
slog(2, " udata_get      \\: trying to get udata_ent for fd %i which is to be present in chat", fd);
	while(e != NULL) {
		if(fd == e->fd && (e->flags&UDC_FLAG_PRESENT)) break;
		e = e->next;
	}
slog(2, " udata_get      /: returning 0x%x for chatting user with fd %i", e, fd);
	return e;
}

//*****this function might need some revision for optimization
//adds user *user with tty *tty to list if *user is not already present
//to see what a return value of -1 means, set errno to 0 before making the call and
//check its' value afterwards
int udata_add(struct udata_ent *ne)
{
	struct udata_ent *last, *e;

slog(2, " udata_add      %*c\\: about to add udata_ent for '%s'", ++log_indent, ' ', ne->user);
	//username must be given
	if(!ne->user) return -1;
	//check if user is already present and if so, exit
	if((e = udata_get(ne->user)) != NULL) return -1;

slog(2, " udata_add      %*c|: adding...", log_indent, ' ', ne->user);
	//allocate new entry and fill in data (except prev which is done in the last part)
	if((e = malloc(sizeof(struct udata_ent))) == NULL) return -1;
	memcpy(e, ne, sizeof(struct udata_ent));
	e->queue = NULL;
	if((e->user = strdup(ne->user)) == NULL) {
		free(e);
		return -1;
	}
	if(ne->tty != NULL) {
		if((e->tty = strdup(ne->tty)) == NULL) {
			free(e->user);
			free(e);
			return -1;
		}
	} else {
		if((e->tty = strdup(TTYSET_UNSET)) == NULL) {
			free(e->user);
			free(e);
			return -1;
		}
	}
	e->beep = (ne->beep==-2 ? -1 : ne->beep);
	e->mode = (ne->mode==-2 ? -1 : ne->mode);
	e->queue = NULL;
	e->fd = (ne->fd<0 ? -1 : ne->fd);
	if(ne->nick && (e->nick = strdup(ne->nick)) == NULL) {
		free(e->user);
		free(e->tty);
		free(e);
		return -1;
	}
	e->flags = 0;
	e->next = NULL;
	//link entry to end of list or, if no entries are present, let udata_start point to it
	last = udata_start;
	if(last != NULL) {
		while(last->next != NULL) last = last->next;
		last->next = e;
		e->prev = last;
	} else {
		udata_start = e;
		e->prev = NULL;
	}
slog(2, " udata_add      %*c/: done adding udata_ent for '%s': ('%s' '%s' %i 0x%x)", log_indent--, ' ', ne->user, e->user, e->nick, e->fd, e->flags);

	return 0;
}

//removes entry for *user if present
int udata_remove(char *user)
{
//need to free queue if != NULL
	struct udata_ent *e;

slog(2, " udata_remove   >: removing udata_ent for '%s'", user);
	//check if user is already present and if not, exit
	if((e = udata_get(user)) == NULL) return -1;

	free(e->user);
	free(e->tty);

	if(e->next != NULL) e->next->prev = e->prev;
	if(e->prev != NULL)
		e->prev->next = e->next;
	else
		udata_start = e->next;
	free(e);

	return 0;
}

//*****this function might need some revision for optimization
//as of now it does not reallocate the structure so there is no need to reget the udata_ent
//update function (get?copy non-'leave alones':add if something is set)
int udata_update(struct udata_ent *e)
{
	char *t;
	int r = 0;
	struct udata_ent *tent = udata_get(e->user);

	if(tent == NULL) {
slog(2, " udata_update   \\: about to change udata_ent for (didn't exist, creating...) '%s'", e->user);
//no test for nick existence because if all chat flags are unset it is not relevant
//fixme: test only for presence and banned flags
		if(!((e->tty == NULL || strcmp(e->tty, TTYSET_UNSET) == 0) &&
		   (e->beep == -2 || e->beep == -1) && (e->mode == -2 || e->mode == -1) &&
		   (e->flags == 0)))
			r = udata_add(e);
	} else {
slog(2, " udata_update   \\: about to change udata_ent for '%s'", e->user);
		if(e->tty != NULL) {
			if((t = strdup(e->tty)) == NULL) return -1;
			else {
				free(tent->tty);
				tent->tty = t;
			}
		}
		if(e->beep != UDATA_LEAVE) tent->beep = e->beep;
		if(e->mode != UDATA_LEAVE) tent->mode = e->mode;
		//why did I think this was right 'tent->fd = (e->fd<0 ? -1 : e->fd);' instead of this?:
		if(e->fd >= 0) tent->fd = e->fd;
		if(e->nick != NULL) {	//nick cannot be reset to NULL...but there also is no need to
			if((t = strdup(e->nick)) == NULL) return -1;
			else {
				free(tent->nick);
				tent->nick = t;
			}
		}
slog(2, " udata_update   /: done changing udata_ent for '%s': ('%s' '%s' %i 0x%x)", e->user, tent->user, tent->nick, tent->fd, tent->flags);
/*
	//this 'auto-removal' code causes trouble so I disabled it
		//no test for nick existence because if all chat flags are unset it is not relevant
		if(strcmp(tent->tty, TTYSET_UNSET) == 0 && tent->beep == -1 &&
		   tent->mode == -1 && tent->queue == NULL &&
		   (tent->flags&UDC_FLAG_BANNED) == 0 && (tent->flags&UDC_FLAG_PRESENT) == 0)
			r = udata_remove(e->user);
*/
	}

	return r;
}

int udata_set_qp(char *user, char *queue_p)
{
	int r = 0;
	struct udata_ent te = UDATA_DEF_ENTRY;
	struct udata_ent *tent = udata_get(user);

	if(tent == NULL) {
		te.user = user;
		r = udata_add(&te);
		if(r >= 0 && (tent = udata_get(user)) != NULL)
			tent->queue = queue_p;
		else
			r = -1;
	} else {
		tent->queue = queue_p;
	}

	return r;
}

char *udata_get_qp(char *user)
{
	char *rq = NULL;
	struct udata_ent *tent = udata_get(user);
	if(tent != NULL) rq = tent->queue;
	return rq;
}

//fixme: should this use *user or fd for entry specification?
//sets all flags set to one and leaves the others alone
int udata_flagset(char *user, unsigned short int f)
{
	struct udata_ent *e;
slog(2, " udata_flagset  \\: trying to get udata_ent for '%s'", user);
	if((e = udata_get(user)) == NULL) return -1;
slog(2, " udata_flagset  /: ~(%s, 0x%x): 0x%x -> 0x%x", user, f, e->flags, e->flags|f);
	e->flags |= f;
	return 0;
}

//fixme: should this use *user or fd for entry specification?
//unsets all flags set to one and leaves the others alone
int udata_flagunset(char *user, unsigned short int f)
{
	struct udata_ent *e;
slog(2, " udata_flagunset\\: trying to get udata_ent for '%s'", user);
	if((e = udata_get(user)) == NULL) return -1;
slog(2, " udata_flagunset/: ~(%s, 0x%x): 0x%x -> 0x%x", user, f, e->flags, e->flags&(~f));
	e->flags &= ~f;
	return 0;
}
