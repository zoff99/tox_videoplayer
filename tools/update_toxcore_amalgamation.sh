#! /bin/sh
url='https://raw.githubusercontent.com/zoff99/c-toxcore/zoff99/zoxcore_local_fork/amalgamation/toxcore_amalgamation.c'

_HOME2_=$(dirname $0)
export _HOME2_
_HOME_=$(cd $_HOME2_;pwd)
export _HOME_

basedir="$_HOME_""/../"

cd "$basedir"
wget "$url" -O toxcore_amalgamation.c
