CC = gcc
CFLAGS = -O3 -I/usr/local/include/libs9c -I/usr/include/libs9c
s9cwebsdr:s9cwebsdr.c
	 $(CC) $(CFLAGS) s9cwebsdr.c -o s9cwebsdr -lpthread -ls9c

