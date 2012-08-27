#deps on smsg.h only
OBJS1 = smsg.o udata.o
#deps on smsg.h and ncmd.h
OBJS2 = clchat.o client.o servmisc.o
#deps on smsg.h, ncmd.h and serv.h
OBJS3 = servchat.o server.o
OBJS = $(OBJS1) $(OBJS2) $(OBJS3)


all: __lc smsg smsgwho rchat

install: __lc smsg smsgwho rchat
	install -d $(bindir)
	install -d $(mandir)/man1
	install -d $(localstatedir)/rc-smsg
	install ${strippah} -m 755 -o 0 -g 0 smsg $(bindir)/smsg
	$(SUBSTAH) smsg.1.substah "PORT=${PORT}" > smsg.1
	install -m 644 -o 0 -g 0 smsg.1 $(mandir)/man1/smsg.1
	install -m 755 -o 0 -g 0 rchat $(bindir)/rchat
	install ${strippah} -m 755 -o 0 -g 0 smsgwho $(bindir)/smsgwho
	@echo -e "\nYou can create a link called 'chat' to smsg to avoid typing 'smsg chat' every time.\n"

install-strip:
	make -f Makefile.gnu install "strippah=${strippah} -s"

clean: __lc
	rm -f $(OBJS)
	rm -f core

distclean: __lc clean
	rm -f smsg smsgwho rchat smsg.1

#don't ask for this rule directly (like make smsg) because there's no need to and
#it doesn't depend on __lc because that would screw up the real dep checking...
smsg: $(OBJS) ../ncmd.c ../ncmd.h
	gcc ${CFLAGS} ${LDFLAGS} -I.. ${DEFINES} $(LIBS) $(OBJS) ../ncmd.o -o smsg

#don't call this directly (see smsg rule for explanation)
smsgwho: smsgwho.c smsg.h ../ncmd.c ../ncmd.h
	gcc ${CFLAGS} ${LDFLAGS} -I.. ${DEFINES} smsgwho.c ../ncmd.o -o smsgwho

#don't call this directly (see smsg rule for explanation)
rchat: rchat.substah
	$(SUBSTAH) rchat.substah "SMSG=$(bindir)/smsg" "SMSGWHO=$(bindir)/smsgwho" "ROOMDIR=$(localstatedir)/rc-smsg" "NETSTAT=$(NETSTAT)" > rchat


#these three rules only exist to let make be aware of the right dependencies
#change to bsdmake style (inherited deps)
$(OBJS1): %.o: %.c smsg.h
$(OBJS2): %.o: %.c smsg.h ../ncmd.h
$(OBJS3): %.o: %.c smsg.h ../ncmd.h serv.h

#general compile rule
%.o: %.c
	gcc ${CFLAGS} -I.. ${DEFINES} -c $< -o $@


#this rule checks if make is run from the right place
__lc:
	@if [ $$MAKELEVEL -lt 2 ]; then \
	  echo "Error: please run make from the toplevel source directory"; \
	  exit 15; \
	fi
