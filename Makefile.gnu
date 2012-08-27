###################
# TODO: replace subdir mechanism for clean and distclean by
#        something like that for the default rule
#	make something good of the install-strip target
###################

include config.mk

DISTROOT := Makefile.bsd Makefile.gnu config.sh config.mk.substah makeme.sh README TODO substah.sh ncmd.c ncmd.h dustbin/* java/*.java
DISTDOC := doc/*
DISTSRC := src/Makefile.gnu src/*.c src/*.h src/smsg.1.substah src/rchat.substah src/rchat.pl

top_srcdir := $(shell pwd)

SUBSTAH = ${top_srcdir}/substah.sh
SUBDIRS = src
CFLAGS += ${lCFLAGS}
LDFLAGS += ${lLDFLAGS}
ifneq (${NCURSES_PREFIX},)
 CFLAGS += -I${NCURSES_PREFIX}/include
 LDFLAGS += -L${NCURSES_PREFIX}/lib
endif
ifdef HAVE_NCURSES
 LIBS = =lncurses
else
 LIBS = -lcurses
endif

prefix := ${INSTALL_PREFIX}
bindir := ${prefix}/bin
mandir := ${prefix}/share/man
localstatedir=${prefix}/var

DEFINES += -DPACKAGE=\"${PACKAGE}\" -DVERSION=\"${VERSION}\" -DOS=\"${OS}\" -DPORT=${PORT} -DCMDLINE_HISTLEN=${CMDLINE_HISTLEN} -DMBOXDIR=\"${MBOXDIR}\"

export

all: ncmd.o subdirs

install:
	for dir in $(SUBDIRS); do \
	  $(MAKE) -f Makefile.gnu -C $$dir install; \
	done

install-strip:
	for dir in $(SUBDIRS); do \
	  $(MAKE) -f Makefile.gnu -C $$dir install-strip; \
	done

dist:
	@echo "Creation of a distribution archive is not yet implemented, however you"
	@echo "can try the dist__ rule but it's not guaranteed it will work correctly."

dist__:
	if [ -d "${PACKAGE}-${VERSION}" ]; then rm -r ${PACKAGE}-${VERSION}; fi
	mkdir "${PACKAGE}-${VERSION}"
	cp -R --parent ${DISTROOT} "${PACKAGE}-${VERSION}"
	cp -R --parent ${DISTDOC} "${PACKAGE}-${VERSION}"
	cp -R --parent ${DISTSRC} "${PACKAGE}-${VERSION}"
#	for i in ${DISTROOT} ${DISTDOC} ${DISTSRC}; do \
#		cp -R "$$i" "${PACKAGE}-${VERSION}"; \
#	done
	tar cvf - "${PACKAGE}-${VERSION}" | bzip2 -9 > "${PACKAGE}-${VERSION}.tar.bz2"
	rm -r "${PACKAGE}-${VERSION}"
	@echo ""
	@echo "WARNING: check if everything you want to be included is in the DIST* variables!"

clean:
	rm -f ncmd.o
	for dir in $(SUBDIRS); do \
	  $(MAKE) -f Makefile.gnu -C $$dir clean; \
	done

distclean: clean
	rm -f Makefile config.mk
	for dir in $(SUBDIRS); do \
	  $(MAKE) -f Makefile.gnu -C $$dir distclean; \
	done


ncmd.o: ncmd.c ncmd.h
	gcc -c ${CFLAGS} ${DEFINES} ncmd.c -o ncmd.o

subdirs: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -f Makefile.gnu -C $@

.PHONY : subdirs $(SUBDIRS) clean distclean
