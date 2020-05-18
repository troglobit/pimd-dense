AC_PREREQ(2.61)
AC_INIT([pimd-dense], [0.2.1.0-beta1], [https://github.com/troglobit/pimdd/issues])
AC_CONFIG_AUX_DIR(aux)
AM_INIT_AUTOMAKE([1.11 foreign])
AM_SILENT_RULES([yes])

AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_FILES([Makefile src/Makefile include/Makefile pimdd.service])

# Check for standard programs, headers, and functions
AC_PROG_CC
AC_PROG_INSTALL

# Required to check for libsystemd-dev
PKG_PROG_PKG_CONFIG

AC_HEADER_STDC
AC_CHECK_HEADERS([fcntl.h netinet/pim.h net/if_dl.h sys/time.h linux/netlink.h])
AC_CHECK_HEADERS([net/if.h], [], [], [
#include <stdio.h>
#ifdef STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# ifdef HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif
#ifdef HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif])

# Check for usually missing API's, which we can replace
AC_REPLACE_FUNCS([strlcpy strlcat])
AC_CONFIG_LIBOBJ_DIR([lib])

AC_ARG_ENABLE(rsrr,
	AS_HELP_STRING([--enable-rsrr], [Routing Support for Resource Reservation
			currently used by RSVP (EXPERIMENTAL).  For details, see
			http://tools.ietf.org/html/draft-ietf-rsvp-routing-02]),,
	enable_rsrr=no)

AC_ARG_WITH([systemd],
     [AS_HELP_STRING([--with-systemd=DIR], [Directory for systemd service files])],,
     [with_systemd=auto])

# Create config.h from selected features and fallback defaults
AS_IF([test "x$enable_rsrr" = "xyes"], [
        AC_DEFINE(RSRR, 1, [Routing Support for Resource Reservation.])])

AS_IF([test "x$with_systemd" = "xyes" -o "x$with_systemd" = "xauto"], [
     def_systemd=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)
     AS_IF([test "x$def_systemd" = "x"],
         [AS_IF([test "x$with_systemd" = "xyes"],
	     [AC_MSG_ERROR([systemd support requested but pkg-config unable to query systemd package])])
	     with_systemd=no], [with_systemd="$def_systemd"])]
)
AS_IF([test "x$with_systemd" != "xno"],
     [AC_SUBST([systemddir], [$with_systemd])])

AM_CONDITIONAL(BSD,     [test "x$ac_cv_header_net_if_dl_h"     = "xyes"])
AM_CONDITIONAL(LINUX,   [test "x$ac_cv_header_linux_netlink_h" = "xyes"])
AM_CONDITIONAL(RSRR,    [test "x$enable_rsrr"                  = "xyes"])
AM_CONDITIONAL(SYSTEMD, [test "x$with_systemd"                != "xno"])

# Expand $sbindir early, into $SBINDIR, for systemd unit file
# NOTE: This does *not* take prefix/exec_prefix override at "make
#       install" into account, unfortunately.
test "x$prefix" = xNONE && prefix=
test "x$exec_prefix" = xNONE && exec_prefix='${prefix}'
DOCDIR=`eval echo $docdir`
DOCDIR=`eval echo $DOCDIR`
AC_SUBST(DOCDIR)
SYSCONFDIR=`eval echo $sysconfdir`
SYSCONFDIR=`eval echo $SYSCONFDIR`
AC_SUBST(SYSCONFDIR)
SBINDIR=`eval echo $sbindir`
SBINDIR=`eval echo $SBINDIR`
AC_SUBST(SBINDIR)

# Generate all files
AC_OUTPUT

cat <<EOF

------------------ Summary ------------------
 $PACKAGE_NAME version $PACKAGE_VERSION
  Prefix................: $prefix
  Sysconfdir............: `eval echo $sysconfdir`
  Localstatedir.........: `eval echo $localstatedir`
  C Compiler............: $CC $CFLAGS $CPPFLAGS $LDFLAGS $LIBS
  systemd...............: $with_systemd

Optional features:
  RSRR (experimental)...: $enable_rsrr

------------- Compiler version --------------
$($CC --version || true)
---------------------------------------------

Check the above options and compile with:
 ${MAKE-make}

EOF
