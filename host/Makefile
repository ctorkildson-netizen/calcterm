# The terminal's tests, on Linux.  make: build them and run the unit tests;
# make check: the unit tests and the renderer against real programs.
CFLAGS = -std=c11 -Wall -Wextra -g -fsanitize=address,undefined
SRC = ../src

all: test_vt ptyrun screentest
	./test_vt

test_vt: test_vt.c $(SRC)/vt.c $(SRC)/vt.h
	$(CC) $(CFLAGS) -o $@ test_vt.c $(SRC)/vt.c

ptyrun: ptyrun.c $(SRC)/vt.c $(SRC)/vt.h
	$(CC) $(CFLAGS) -o $@ ptyrun.c $(SRC)/vt.c -lutil

screentest: screentest.c $(SRC)/vt.c $(SRC)/screen.c $(SRC)/termfont.c
	$(CC) $(CFLAGS) -Istub -Istub/drivers -o $@ screentest.c $(SRC)/vt.c $(SRC)/screen.c $(SRC)/termfont.c -lutil

check: all
	./screentest sh -c 'seq 1 2000; ls --color=always -la /usr/bin | head -40'
	./screentest -k 'Go\e:help\r' vim -u NONE -N
	./screentest -k '1\r' -k '\r' -k '\r' vttest 32x64
	./screentest -k q top -d 0.2

clean:
	rm -f test_vt ptyrun screentest
