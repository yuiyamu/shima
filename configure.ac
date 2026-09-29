AC_INIT([shima], [0.1.0])
AM_INIT_AUTOMAKE([-Wall -Wextra foreign])
AC_CANONICAL_HOST

PKG_CHECK_MODULES([ZLIB], [zlib], [], [
    AC_MSG_ERROR([zlib is needed for heliotrope to work, which is the zip library shima uses~])
])

AC_USE_SYSTEM_EXTENSIONS
CFLAGS="" # wipes out default -O2 -g
AC_PROG_CC
AM_PROG_AR
AC_PROG_RANLIB # needed for some builds @_@ even tho we have LT_INIT!!
LT_INIT
AC_CONFIG_MACRO_DIRS([m4])

CFLAGS="$CFLAGS -std=gnu99"
if test "x$enable_debug" = "xyes"; then
    CFLAGS="$CFLAGS -O2 -g -std -fsanitize=address"
else
    CFLAGS="$CFLAGS -O0 -g"
fi

AC_ARG_ENABLE([debug], AS_HELP_STRING([--enable-debug], [build with debug flags >w<!!]), [], [enable_debug=no])

CFLAGS="$CFLAGS -std=gnu99"

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

