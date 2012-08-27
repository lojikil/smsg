#!/bin/sh
# This is a really simple 'macro processor'.
# Call it with a file to process and -after- that key/value pairs in this
# format: "key=value".
# Then all occurences of @key@ in the input file are replaced by
# the corresponding value. The result is written to stdout.
# WARNING: key/value pairs must be one argument.
#          spaces around the '=' are not cut off. Beware.
#          moral: just quote the pairs...
# NOTE: it does not support escaping of the @'s but unrecognized '@keys@' are
#       left alone.
#first figure out echo style

echo -e "aap" | grep '^aap$' >/dev/null 2>/dev/null
if [ $? -ne 0 ]; then
	xecho='echo'
	#$xecho "xecho has been set to 'echo'"
else
	xecho='echo -e'
	#$xecho "xecho has been set to 'echo -e'"
fi

if [ $# -eq 0 ]; then
	$xecho "$0: please give a filename to process as first argument and key/value pairs as subsequent arguments" 1>&2
	exit 1
elif [ $# -eq 1 ]; then
	$xecho "Hrrrmm...no key/value pairs defined. The output will be the same as the input...do you really want this?" 1>&2
fi

inpfile="$1"
input=`cat $1`
shift

i=1; $xecho "Substituting $# key(s) in file '$inpfile': \c" 1>&2
for kv in $@; do
	$xecho "$i...\c" 1>&2
#	i=$(( $i + 1 ))
#	i=`echo "$i+1" | bc`
#	let i=$i+1
	i=`awk "BEGIN {tmpi=$i; tmpi++; print tmpi}"</dev/null`
	key=`echo "$kv"|sed -e 's/=.*$//; s%/%\\\/%g'`
	value=`echo "$kv"|sed -e 's/^.*=//; s%/%\\\/%g'`
	input=`echo "$input" | sed "s/@$key@/$value/g"`
done
$xecho "done." 1>&2

$xecho "$input"
