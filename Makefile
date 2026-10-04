CC := gcc

PKG_CFG := gstreamer-1.0 gstreamer-base-1.0 orc-0.4
PKG_CFG_ALL := gstreamer-1.0 gstreamer-base-1.0 gstreamer-video-1.0 orc-0.4

#CFLAGS := -O2 -Wall -Werror -fPIC $(shell pkg-config --cflags $(PKG_CFG_ALL))
CFLAGS := -O2 -Wall -fPIC $(shell pkg-config --cflags $(PKG_CFG))
#CFLAGS := -Wall -fPIC $(shell pkg-config --cflags $(PKG_CFG))

LDFLAGS := $(shell pkg-config --libs $(PKG_CFG_ALL))

objects := gstbayerorc.o gstbayerfuncs.o gstbayer2bayer.o gstbayer2grey.o gstbayer-ir.o
plugin := gstbayer_ir.so

#build_deprecated := true
ifdef build_deprecated
objects := gstbayer2rgb.o $(objects)
CFLAGS := -D BUILD_DEPRECATED $(CFLAGS)
endif

all: $(plugin)

gstbayerorc.h: gstbayerorc.orc
	orcc --header --include glib.h -o gstbayerorc.h gstbayerorc.orc

gstbayerorc.c: gstbayerorc.orc gstbayerorc.h
	orcc --implementation --include glib.h -o gstbayerorc.c gstbayerorc.orc

gstbayerorc.o: gstbayerorc.c
	$(CC) $(CFLAGS) -c -o gstbayerorc.o gstbayerorc.c

gstbayerfuncs.o: gstbayerfuncs.c
	$(CC) $(CFLAGS) -c -o gstbayerfuncs.o gstbayerfuncs.c

gstbayer2bayer.o: gstbayer2bayer.c
	$(CC) $(CFLAGS) -c -o gstbayer2bayer.o gstbayer2bayer.c

gstbayer2grey.o: gstbayer2grey.c
	$(CC) $(CFLAGS) -c -o gstbayer2grey.o gstbayer2grey.c

gstbayer2rgb.o: gstbayer2rgb.c
	$(CC) $(CFLAGS) -c -o gstbayer2rgb.o gstbayer2rgb.c

gstbayer-ir.o: gstbayer-ir.c
	$(CC) $(CFLAGS) -c -o gstbayer-ir.o gstbayer-ir.c

$(plugin): $(objects)
	$(CC) -shared -o $(plugin) $(objects) $(LDFLAGS)

install: $(plugin)
	install -o root -g root -m 644 $(plugin) /usr/lib/aarch64-linux-gnu/gstreamer-1.0/

clean:
	rm -f *.so *.o gstbayerorc.c gstbayerorc.h
