#!/bin/sh
#
#HOW:
# The script performs a number of tests and populates $SUBSTERS with key/value
# definitions containing the test results. This variable is then used to
# generate the output file from the input file using my self-written extra-small
# macro parser.
#
#TODO:
# -provide -d, --dump option to have a default config.mk written (in case this
#  script fails, you can edit it manually)
# -take options? (compile flags/options/install path/chatlink name/etc.(debug|optimize|strip))
# -should the netstat test always try to get a full path?
# -the make style test is too stupid

infile=config.mk.substah
outfile=config.mk

SUBSTERS=""
WARNINGS=""

#first figure out echo style
echo -e "aap" | grep '^aap$' >/dev/null 2>/dev/null
if [ $? -ne 0 ]; then
	xecho='echo'
	$xecho "xecho has been set to 'echo'"
else
	xecho='echo -e'
	$xecho "xecho has been set to 'echo -e'"
fi

#get the OS name
$xecho "Getting OS name...\c"
OSNAME=`uname -s | tr A-Z a-z`
SUBSTERS="$SUBSTERS OSNAME=$OSNAME"
$xecho "'$OSNAME'"
#also test for solaris or something?
if [ "x$OSNAME" = "xSunOS" ]; then
	SUBSTERS="$SUBSTERS SOLARIS_LIBS=-lresolv -lsocket -lnsl"
	$xecho "sun os found...linking against resolv, socket and nsl (yeh bad test I know)"
else
	SUBSTERS="$SUBSTERS SOLARIS_LIBS="
fi

$xecho "Looking for ncurses...\c"
cat > cmpt~01~.c << TEOF
#include <ncurses.h>
int main(void) {
	initscr();
	return 0;
}
TEOF
gcc -lncurses -o cmpt~01~ cmpt~01~.c >/dev/null 2>/dev/null
if [ -s cmpt~01~ ]; then
	SUBSTERS="$SUBSTERS NCURSES_PATH= HAVE_NCURSES_DEF=-DHAVE_NCURSES"
	$xecho "found"
else
#	ncurspath=`locate libncurses.so | sed -n 's#^\(.*\)/libncurses\.so$#\1#p'`
	#if the locate did not return a positive result, try in /usr/pkg (*cough* bad form *cough*)
	if [ "x$ncurspath" = "x" ] && [ -s /usr/pkg/lib/libncurses.so ]; then
		ncurspath=/usr/pkg
	fi

	if [ "x$ncurspath" != "x" ]; then
		SUBSTERS="$SUBSTERS NCURSES_PATH=$ncurspath HAVE_NCURSES_DEF=-DHAVE_NCURSES"
		$xecho "found ($ncurspath)"
	else
		SUBSTERS="$SUBSTERS NCURSES_PATH= HAVE_NCURSES_DEF="
		$xecho "not found"
		WARNINGS="$WARNINGS\n\
 -Ncurses could not be found. This script is not too smart so it might have\n\
  missed your install. In that case edit the path manually in config.mk (and\n\
  tell me about your situation).\n
  I will now guess that you have curses in a default place."
	fi
fi
rm -rf cmpt~01~.c cmpt~01~

$xecho "Looking for getopt_long()...\c"
cat > cmpt~01~.c << TEOF
#include <getopt.h>
int main(int argc, char *argv[]) {
	char *s = "";
	struct option o = {};
	int i;
	getopt_long(argc, argv, s, &o, &i);
	return 0;
}
TEOF
gcc -o cmpt~01~ cmpt~01~.c >/dev/null 2>/dev/null
if [ -s cmpt~01~ ]; then
	SUBSTERS="$SUBSTERS HAVE_GETOPT_LONG_DEF=-DHAVE_GETOPT_LONG"
	$xecho "found"
else
	SUBSTERS="$SUBSTERS HAVE_GETOPT_LONG_DEF="
	$xecho "not found"
fi
rm -rf cmpt~01~.c cmpt~01~

#find netstat
$xecho "Looking for netstat...\c"
netstat >/dev/null 2>/dev/null
if [ $? -eq 0 ]; then
	SUBSTERS="$SUBSTERS NETSTAT=netstat"
	$xecho "just netstat is sufficient"
else
	NETSTAT=`whereis -b netstat | awk '{print $2}'`
	$xecho "$NETSTAT"
	exit
fi

#figure out make style (just GNU or not GNU at the moment)
$xecho "Figuring out make style...\c"
make --version 2> /dev/null | grep "^GNU" >/dev/null 2>/dev/null
if [ $? -eq 0 ]; then
	ln -s Makefile.gnu Makefile >/dev/null 2>/dev/null
	#cp Makefile.gnu Makefile >/dev/null 2>/dev/null
	$xecho "GNU"
else
	ln -s Makefile.bsd Makefile >/dev/null 2>/dev/null
	#cp Makefile.bsd Makefile >/dev/null 2>/dev/null
	$xecho "BSD"
	WARNINGS="$WARNINGS\n\
 -It's possible my guess about your make program (bsd) is incorrect, in that\n\
  case copy Makefile.gnu to Makefile (only in the sourceroot!)..if neither one\n\
  works your only option is the makeme.sh script."
fi

#generate output from $infile with settings to $outfile
$xecho "\nWriting $outfile:"
./substah.sh $infile $SUBSTERS > $outfile

if [ "x$WARNINGS" != "x" ]; then
	$xecho "\nWarnings issued by one or more tests:\n$WARNINGS"
fi
