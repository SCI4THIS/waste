# Guest C helpers compiled into the production PIC library.
# allocator.c is added by libc.mk.
LIBC_C_SOURCES := \
	stdio.c wchar.c locale.c identity.c string.c math.c quad.c \
	stdlib.c pattern.c misc.c time.c termcap.c termios.c dirent.c \
	netdb.c dlfcn.c unistd.c sys/random.c sys/resource.c \
	sys/select.c sys/ioctl.c sys/socket.c
