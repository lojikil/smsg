#!/bin/sh

echo -e "\nOnly use this if the Makefile doesn't work. This script can only compile (hopefully), not install.\n"

. config.mk

DEFINES="${DEFINES} -DPORT=${PORT} -DOS=\"${OS}\" -DVERSION=\"${VERSION}\" -DPACKAGE=\"${PACKAGE}\""

CFLAGS="${CFLAGS} ${lCFLAGS}"
LDFLAGS="${LDFLAGS} ${lLDFLAGS}"
LIBS=-lncurses

if [ ! $NCURSES_PREFIX = "" ]; then
	CFLAGS="${CFLAGS} -I$NCURSES_PREFIX/include"
	LDFLAGS="${LDFLAGS} -L$NCURSES_PREFIX/lib"
fi

SMSG_SRC="clchat.c client.c servchat.c server.c servmisc.c smsg.c udata.c"
SMSG_OBJ="src/clchat.o src/client.o src/servchat.o src/server.o src/servmisc.o src/smsg.c src/udata.o"
SMSGWHO_SRC="smsgwho.c"
SMSGWHO_OBJ="src/smsgwho.o"


echo "gcc -c ${CFLAGS} ${DEFINES} $@ ncmd.c"
      gcc -c ${CFLAGS} ${DEFINES} -I. $@ ncmd.c
if [ $? -ne 0 ]; then
	echo "$0: Error detected... aborting"
	exit 1
fi

echo "cd src"
cd src
for srcfile in $SMSG_SRC $SMSGWHO_SRC; do
	echo "gcc -c ${CFLAGS} ${DEFINES} -I.. $@ $srcfile"
	      gcc -c ${CFLAGS} ${DEFINES} -I.. $@ $srcfile
	if [ $? -ne 0 ]; then
		echo "$0: Error detected... aborting"
		break
	fi
done
echo "cd .."
cd ..

echo "gcc ${CFLAGS} ${LDFLAGS} ${LIBS} ${DEFINES} -I. $@ $SMSG_OBJ ncmd.o -o src/smsg"
      gcc ${CFLAGS} ${LDFLAGS} ${LIBS} ${DEFINES} -I. $@ $SMSG_OBJ ncmd.o -o src/smsg
if [ $? -ne 0 ]; then
	echo "$0: Error detected... aborting"
	break
fi
echo "gcc ${CFLAGS} ${DEFINES} -I. $@ $SMSGWHO_OBJ ncmd.o -o src/smsgwho"
      gcc ${CFLAGS} ${DEFINES} -I. $@ $SMSGWHO_OBJ ncmd.o -o src/smsgwho
if [ $? -ne 0 ]; then
	echo "$0: Error detected... aborting"
	break
fi
