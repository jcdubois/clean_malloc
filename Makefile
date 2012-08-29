CC=gcc
STRIP=strip
RM=rm
CFLAGS=-g -Wall -Wextra -O0
LDFLAGS=-shared -fPIC -DPIC
DEBUGFLAGS=
DEBUGFLAGS=-DCHECK_COOKIE
DEBUGFLAGS=-DCHECK_COOKIE -DDEBUG
LD_LIBS=-ldl
TARGET=clean_malloc.so clean_write.so

all: clean $(TARGET)

%.so: %.c
	$(CC) $(CFLAGS) $(DEBUGFLAGS) $(LDFLAGS) -o $@ $< $(LD_LIBS)
	$(STRIP) $@

clean:
	$(RM) -f $(TARGET)

%.c~: %.c
	indent -linux $<

reformat:  clean_malloc.c~ clean_write.c~
	$(RM) -f $?
