#ifndef WASTE_GNULIB_COMPAT_H
#define WASTE_GNULIB_COMPAT_H

/* The generated coreutils/gnulib headers normally receive these annotations
   through the host portability headers.  Keep them syntax-only for the
   freestanding WASTE target; they do not change the guest ABI. */
#ifndef _GL_ARG_NONNULL
# define _GL_ARG_NONNULL(args)
#endif
#ifndef _GL_ATTRIBUTE_FORMAT_PRINTF_STANDARD
# define _GL_ATTRIBUTE_FORMAT_PRINTF_STANDARD(format_index, first_arg)
#endif
#ifndef GNULIB_defined_F_DUPFD_CLOEXEC
# define GNULIB_defined_F_DUPFD_CLOEXEC 0
#endif

#endif
