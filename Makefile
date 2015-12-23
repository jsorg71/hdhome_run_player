
OBJS = hdhome_run_player.o mpeg_ts.o hdhome_run_x11.o hdhome_run_pa.o list.o

CFLAGS = -Wall -O2 -I/usr/lib/libhdhomerun -I/usr/include/libhdhomerun

LDFLAGS =

LIBS = -lhdhomerun -lavcodec -lavresample -lavutil -lX11 -lXv -lXext -lm -ldl -lpthread -lpulse

all: hdhome_run_player

hdhome_run_player: $(OBJS)
	$(CC) -o hdhome_run_player $(OBJS) $(LDFLAGS) $(LIBS)

clean:
	rm -f $(OBJS) hdhome_run_player
