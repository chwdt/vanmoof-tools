if [ $# -ne 2 ]; then
	echo "usage: $0 <dump-file> <binary-file>"
	exit 1
fi

exec cat $1 | sed -e 's/[^\t]*\t\([^\t]*\)\t.*/\1/' | xxd -r -p >$2
