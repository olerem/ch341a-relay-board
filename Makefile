# minimal makefile
# for relay board control program
# (c) Edwin van den Oetelaar GPL2
# some info about the USB chip
# The board was purchased at
# http://www.electronic-software-shop.com/hardware/relais/usb-relaiskarte-lrb-8-fach.html
# 
 
CC=gcc
CFLAGS=-Wall -Wextra -std=gnu99 -O2 -ggdb -g
CFLAGS+= `pkg-config --cflags libusb-1.0`
SOURCES=main.c logging.c
LIBS=-lusb-1.0
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=switch_relay

all: $(EXECUTABLE) 

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) $(LIBS) -o $@

%.o : %.c
	$(CC) $(CFLAGS) -c $<
	
clean:
	rm -f $(OBJECTS) $(EXECUTABLE)


