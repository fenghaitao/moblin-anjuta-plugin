dnl Process this file with autoconf to produce a configure script.

AC_INIT(anjuta-plugin-sdk, 0.6)

AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AM_CONFIG_HEADER(config.h)
AM_MAINTAINER_MODE

AC_ISC_POSIX
AC_PROG_CC
AM_PROG_CC_STDC
AC_HEADER_STDC

GETTEXT_PACKAGE=anjuta-plugin-sdk
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [GETTEXT package name])
AM_GLIB_GNU_GETTEXT
IT_PROG_INTLTOOL([0.35.0])

AM_DISABLE_STATIC
AM_PROG_LIBTOOL

PKG_CHECK_MODULES(GLIB, [glib-2.0])
PKG_CHECK_MODULES(GTK, [gtk+-2.0 >= 2.12.0])
PKG_CHECK_MODULES(LIBANJUTA, [libanjuta-1.0])
PKG_CHECK_MODULES(GNOME_VFS, [gnome-vfs-2.0])
AC_SUBST(LIBANJUTA_CFLAGS)
AC_SUBST(LIBANJUTA_LIBS)
AC_SUBST(GNOME_VFS_CFLAGS)
AC_SUBST(GNOME_VFS_LIBS)

AC_ARG_ENABLE(gdb_integration,
  AS_HELP_STRING([--enable-gdb-integration], 
                 ["Enable experimental gdb integration"]),
                 [if test "x$enableval" = "xyes"; then
                 AC_DEFINE(GDB_INTEGRATION, 1, Enable experimental GDB integration)
                 fi])


dnl Setup Plugin directories
dnl ------------------------
anjutalibdir=`pkg-config --variable=libdir libanjuta-1.0`
anjutadatadir=`pkg-config --variable=datadir libanjuta-1.0`
AC_SUBST(anjutalibdir)
AC_SUBST(anjutadatadir)
anjuta_plugin_dir='$(anjutalibdir)/anjuta'
anjuta_data_dir='$(anjutadatadir)/anjuta'
anjuta_ui_dir='$(anjutadatadir)/anjuta/ui'
anjuta_glade_dir='$(anjutadatadir)/anjuta/glade'
anjuta_image_dir='$(anjutadatadir)/pixmaps/anjuta'
AC_SUBST(anjuta_plugin_dir)
AC_SUBST(anjuta_data_dir)
AC_SUBST(anjuta_ui_dir)
AC_SUBST(anjuta_glade_dir)
AC_SUBST(anjuta_image_dir)

AC_OUTPUT([
Makefile
src/Makefile
po/Makefile.in
])
