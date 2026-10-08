# Guest C helpers shared by the production PIC library and static test profile.
# The production library additionally compiles allocator.c; the static profile
# obtains its allocator from stdlib.wat.
LIBC_C_SOURCES := \
	stdio.c wchar.c locale.c identity.c string.c math.c quad.c \
	stdlib.c pattern.c misc.c time.c termcap.c termios.c dirent.c \
	netdb.c dlfcn.c unistd.c sys/random.c sys/resource.c \
	sys/select.c sys/ioctl.c sys/socket.c
