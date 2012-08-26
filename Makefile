CC=gcc
STRIP=strip
CFLAGS=-g -Wall -Wextra -O0
LDFLAGS=-shared -fPIC -DPIC
DEBUGFLAGS=
DEBUGFLAGS=-DCHECK_COOKIE
DEBUGFLAGS=-DCHECK_COOKIE -DDEBUG
LD_LIBS=-ldl
TARGET=clean_malloc.so

all: clean $(TARGET)

clean_malloc.so: clean_malloc.c
	$(CC) $(CFLAGS) $(DEBUGFLAGS) $(LDFLAGS) -o $@ $< $(LD_LIBS)
	$(STRIP) $@

clean:
	rm -f $(TARGET)

reformat: 
	indent -linux clean_malloc.c
	rm clean_malloc.c~
