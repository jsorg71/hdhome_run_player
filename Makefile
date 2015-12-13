
OBJS = hdhome_run_player.o mpeg_ts.o

CFLAGS = -Wall -O2 -I/usr/lib/libhdhomerun/

LDFLAGS =

LIBS = -lhdhomerun

all: hdhome_run_player

hdhome_run_player: $(OBJS)
	$(CC) -o hdhome_run_player $(OBJS) $(LDFLAGS) $(LIBS)

clean:
	rm -f $(OBJS) hdhome_run_player
