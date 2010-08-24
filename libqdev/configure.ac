AC_PREREQ(2.65)

AC_INIT([qdev],[0.0.1],[http://bugs.launchpad.net/qemu],[qdev],[http://www.qemu.org/])
AC_CONFIG_SRCDIR([src/qdev.c])
AC_CONFIG_HEADERS([config.h])

dnl Make automake keep quiet about wildcards & other GNUmake-isms
AM_INIT_AUTOMAKE([1.10])

LT_PREREQ([2.2.6])
LT_INIT([win32-dll disable-static])

# Use the silent-rules feature when possible.
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AC_CANONICAL_HOST

AC_PROG_CC_STDC
AM_PROG_CC_C_O

AC_CONFIG_FILES(
  Makefile
  src/Makefile
  qdev-0.1.pc
  )

AC_OUTPUT
