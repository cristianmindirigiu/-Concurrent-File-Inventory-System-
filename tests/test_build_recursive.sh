#!/bin/bash


TEST_DIR="tmp/scenariu_test_src"
mkdir -p "$TEST_DIR/app" "$TEST_DIR/lib/include"

cat << 'EOF' > "$TEST_DIR/lib/include/util.h"
int util_add(int a, int b);
EOF

cat << 'EOF' > "$TEST_DIR/lib/util.c"
#include "include/util.h"
int util_add(int a, int b) {
	return a + b;
}
EOF

cat << 'EOF' > "$TEST_DIR/app/main_demo.c"
#include <stdio.h>
#include "../lib/include/util.h"

int main() {
	printf("%d\n", util_add(2,3));
	return 0;
}
EOF


./tools/fileops.sh build --src "$TEST_DIR" > /dev/null

if [ ! -x "bin/demo" ]
then
	echo "FAIL. Executabilul nu a fost gasit"
	exit 1
fi

./bin/demo > tmp/demo_out.txt

REZULTAT=$(cat tmp/demo_out.txt)

if [ "$REZULTAT" != "5" ]
then
	echo "FAIL. Rezultatul nu este valid."
	exit 1
fi

rm -rf "$TEST_DIR" tmp/demo_out.txt
exit 0

	
