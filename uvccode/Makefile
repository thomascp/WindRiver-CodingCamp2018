CC = gcc
TARGET = uvc

DEBUG +=
STRIP +=

CFLAGS += -O2 -Wall -I./
LDFLAGS += -ljpeg

# If new C files add. The OBJS will sync too.
OBJS = uvc.o \
	jpeg.o \

all: build 

build: $(OBJS)
	$(CC) $(DEBUG) -o $(TARGET) $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(DEBUG) $(CFLAGS) -c -o $@ $< 

clean: 
	@rm -rf $(TARGET) $(OBJS)
	@rm -rf *.jpg

