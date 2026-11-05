# The terminal's tests, on Linux.  make: build them and run the unit tests;
# make check: the unit tests and the renderer against real programs.
CFLAGS = -std=c11 -Wall -Wextra -g -fsanitize=address,undefined
SRC = ../src

all: test_vt ptyrun screentest menutest
	./test_vt
	./menutest /dev/null

test_vt: test_vt.c $(SRC)/vt.c $(SRC)/vt.h
	$(CC) $(CFLAGS) -o $@ test_vt.c $(SRC)/vt.c

ptyrun: ptyrun.c $(SRC)/vt.c $(SRC)/vt.h
	$(CC) $(CFLAGS) -o $@ ptyrun.c $(SRC)/vt.c -lutil

screentest: screentest.c $(SRC)/vt.c $(SRC)/screen.c $(SRC)/shot.c $(SRC)/termfont.c $(SRC)/font4.c
	$(CC) $(CFLAGS) -Istub -Istub/drivers -o $@ screentest.c $(SRC)/vt.c $(SRC)/screen.c $(SRC)/shot.c $(SRC)/termfont.c $(SRC)/font4.c -lutil

menutest: menutest.c $(SRC)/menu.c $(SRC)/vt.c $(SRC)/screen.c $(SRC)/termfont.c $(SRC)/font4.c
	$(CC) $(CFLAGS) -std=gnu11 -Istub -Istub/drivers -DCALCTERM_BUILD='"test"' -DPICO_PROGRAM_VERSION_STRING='"0.1"' -o $@ menutest.c $(SRC)/menu.c $(SRC)/vt.c $(SRC)/screen.c $(SRC)/termfont.c $(SRC)/font4.c

check: all
	./screentest sh -c 'seq 1 2000; ls --color=always -la /usr/bin | head -40'
	./screentest -k 'Go\e:help\r' vim -u NONE -N -n
	./screentest -k '1\r' -k '\r' -k '\r' vttest 32x64
	./screentest -k q top -d 0.2
	rm -rf shots && mkdir shots && ./screentest -t calcterm -s shots mc && rm -rf shots

clean:
	rm -f test_vt ptyrun screentest menutest
