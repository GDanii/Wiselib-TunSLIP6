CC=gcc

CFLAGS= -Wall

all: wiselib_tunslip6

wiselib_tunslip6:
	$(CC) $(CFLAGS) wiselib_tunslip6.c -o wiselib_tunslip6

clean:
	rm -rf *o wiselib_tunslip6