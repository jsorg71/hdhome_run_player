
OBJS = hdhome_run_player.o mpeg_ts.o hdhome_run_x11.o hdhome_run_pa.o list.o \
       hdhome_run_mycodec.o hdhome_run_log.o

CFLAGS = -Wall -O2 -g \
         -I/usr/lib/libhdhomerun \
         -I/usr/include/libhdhomerun \
         -I/home/jay/hdhome_run/libhdhomerun

LDFLAGS = -L/home/jay/hdhome_run/libhdhomerun

LIBS = -lhdhomerun -lX11 -lXv -lXext -lm -ldl -lpthread -lpulse -la52 -lmpeg2

all: hdhome_run_player

hdhome_run_player: $(OBJS)
	$(CC) -o hdhome_run_player $(OBJS) $(LDFLAGS) $(LIBS)

clean:
	rm -f $(OBJS) hdhome_run_player
