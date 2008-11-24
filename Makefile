
default: test-mozembed

mozheadless: mozheadless.c
	gcc -o mozheadless mozheadless.c -Wall -g `pkg-config --cflags --libs glib-2.0 sqlite3` -lxpcomglue_s -lxul -lxpcom -L/home/cwiiis/Projects/mozilla/obj-i686-pc-linux-gnu/dist/lib -I/home/cwiiis/Projects/mozilla/obj-i686-pc-linux-gnu/dist/include -I/home/cwiiis/Projects/mozilla/obj-i686-pc-linux-gnu/dist/include/nspr -lmozjs -lsoftokn3

test-mozembed-working: test-mozembed.c clutter-mozembed-old.c
	gcc -o test-mozembed clutter-mozembed-old.c test-mozembed.c -Wall -g `pkg-config --cflags --libs glib-2.0 clutter-0.8 sqlite3` -lxpcomglue_s -lxul -lxpcom -L/home/cwiiis/Projects/mozilla/obj-i686-pc-linux-gnu/dist/lib -I/home/cwiiis/Projects/mozilla/obj-i686-pc-linux-gnu/dist/include -I/home/cwiiis/Projects/mozilla/obj-i686-pc-linux-gnu/dist/include/nspr -lmozjs -lsoftokn3

test-mozembed: test-mozembed.c clutter-mozembed.c mozheadless
	gcc -o test-mozembed clutter-mozembed.c test-mozembed.c -Wall -g `pkg-config --cflags --libs glib-2.0 clutter-0.8 sqlite3` -lxpcomglue_s -lxul -lxpcom -L/home/cwiiis/Projects/mozilla/obj-i686-pc-linux-gnu/dist/lib -I/home/cwiiis/Projects/mozilla/obj-i686-pc-linux-gnu/dist/include -I/home/cwiiis/Projects/mozilla/obj-i686-pc-linux-gnu/dist/include/nspr -lmozjs -lsoftokn3

run: test-mozembed
	LD_LIBRARY_PATH=/home/cwiiis/Projects/mozilla/obj-i686-pc-linux-gnu/dist/lib MOZILLA_FIVE_HOME=/home/cwiiis/Projects/mozilla/obj-i686-pc-linux-gnu/dist/bin PATH=./:$(PATH) ./test-mozembed

debug: test-mozembed
	LD_LIBRARY_PATH=/home/cwiiis/Projects/mozilla/obj-i686-pc-linux-gnu/dist/lib MOZILLA_FIVE_HOME=/home/cwiiis/Projects/mozilla/obj-i686-pc-linux-gnu/dist/bin PATH=./:$(PATH) gdb --args ./test-mozembed

clean:
	rm -f mozheadless test-mozembed
