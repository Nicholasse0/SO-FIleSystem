CCOPTS= -Wall -g -std=gnu99 -Wstrict-prototypes
LIBS= 
CC=gcc
AR=ar


BINS= simplefs_test

OBJS = #add here your object files

HEADERS=bitmap.h\
	disk_driver.h\
	simplefs.h

%.o:	%.c $(HEADERS)
	$(CC) $(CCOPTS) -c -o $@  $<

.phony: clean all

so_game: bitmap.c disk_driver.c simplefs_test.c $(HEADERS)
		$(CC) $(CCOPTS) bitmap.c disk_driver.c simplefs_test.c -o simplefs_test

clean:
	rm -rf *.o *~  *.txt $(BINS)