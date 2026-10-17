# Guest C helpers compiled into the production PIC library.
# allocator.c is added by libc.mk.
LIBC_C_SOURCES := \
	stdio.c wchar.c wide-format.c locale.c identity.c string.c math.c fenv.c quad.c \
	stdlib.c integer.c floating.c abort.c exit.c immediate-exit.c pattern.c misc.c time.c clock.c termcap.c termios.c dirent.c \
	netdb.c dlfcn.c unistd.c process.c signal.c alarm.c sysconf.c mman.c realpath.c sys/random.c sys/resource.c \
	sys/select.c sys/ioctl.c sys/socket.c
