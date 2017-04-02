CC=gcc
CFLAGS=-c -std=c99 -Wall -fPIC
SOURCES=rainbowdetect.c
INCLUDE=../include/vapoursynth
OBJECTS=$(notdir $(SOURCES:.c=.o))
LIBNAME=rainbowdetect
PREFIX=/usr/local

all:
	$(CC) $(CFLAGS) -I$(INCLUDE) $(SOURCES)
	ar cru $(LIBNAME).a $(OBJECTS)
	$(CC) -shared -o $(LIBNAME).so $(OBJECTS)

.PHONY: clean
clean:
	rm -f $(OBJECTS) $(LIBNAME).a $(LIBNAME).so

.PHONY: install
install:
	mkdir -p $(DESTDIR)$(PREFIX)/lib
	cp $(LIBNAME).a $(DESTDIR)$(PREFIX)/lib
	cp $(LIBNAME).so $(DESTDIR)$(PREFIX)/lib

.PHONY: uninstall
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/lib/$(LIBNAME).a $(DESTDIR)$(PREFIX)/lib/$(LIBNAME).so
