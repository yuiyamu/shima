AC_INIT([shima], [0.1.0])
AM_INIT_AUTOMAKE([-Wall foreign])
AC_CANONICAL_HOST

AC_USE_SYSTEM_EXTENSIONS
CFLAGS="" # wipes out default -O2 -g
AC_PROG_CC
LT_INIT

CFLAGS="$CFLAGS -std=gnu99"
if test "x$enable_debug" = "xyes"; then
    CFLAGS="$CFLAGS -O2 -g -std -fsanitize=address"
else
    CFLAGS="$CFLAGS -O0 -g"
fi

AC_ARG_ENABLE([debug], AS_HELP_STRING([--enable-debug], [build with debug flags >w<!!]), [], [enable_debug=no])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

