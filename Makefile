
MOZILLA_PREFIX ?= ${PWD}/../obj-i686-pc-linux-gnu/dist
INCLUDES = -I${MOZILLA_PREFIX}/include -I${MOZILLA_PREFIX}/include/nspr -I${MOZILLA_PREFIX}/include/dom
LIBS = -L${MOZILLA_PREFIX}/lib -lxpcomglue_s -lxul -lxpcom -lmozjs -lsoftokn3 -lsqlite3

default: test-mozembed

mozheadless: mozheadless.c
	gcc -o mozheadless mozheadless.c -Wall -g `pkg-config --cflags --libs glib-2.0` ${INCLUDES} ${LIBS}

test-mozembed-working: test-mozembed.c clutter-mozembed-old.c
	gcc -o test-mozembed clutter-mozembed-old.c test-mozembed.c -Wall -g `pkg-config --cflags --libs clutter-0.8` ${INCLUDES} ${LIBS}

test-mozembed: test-mozembed.c clutter-mozembed.c mozheadless
	gcc -o test-mozembed clutter-mozembed.c test-mozembed.c -Wall -g `pkg-config --cflags --libs clutter-0.8` ${INCLUDES} ${LIBS}

browser: web-browser.c web-browser.h clutter-mozembed.c mozheadless
	gcc -o browser web-browser.c clutter-mozembed.c -Wall -g `pkg-config --cflags --libs clutter-0.8` ${INCLUDES} ${LIBS} -DWITH_MOZILLA

run: test-mozembed
	LD_LIBRARY_PATH=${MOZILLA_PREFIX}/lib MOZILLA_FIVE_HOME=${MOZILLA_PREFIX}/bin PATH=./:${PATH} ./test-mozembed

run-browser: browser
	LD_LIBRARY_PATH=${MOZILLA_PREFIX}/lib MOZILLA_FIVE_HOME=${MOZILLA_PREFIX}/bin PATH=./:${PATH} ./browser

debug: test-mozembed
	echo "Run the following:"
	echo "LD_LIBRARY_PATH=${MOZILLA_PREFIX}/lib MOZILLA_FIVE_HOME=${MOZILLA_PREFIX}/bin PATH=./:${PATH} gdb --args ./test-mozembed"

clean:
	rm -f mozheadless test-mozembed
