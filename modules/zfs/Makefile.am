## Process this file with automake to produce Makefile.in

SUBDIRS = data

NULL =

AM_CPPFLAGS =                                                                  \
	-I$(top_builddir) -I$(top_srcdir)                                      \
	-DPACKAGE_LIBEXEC_DIR=\""$(libexecdir)"\"                              \
	-DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\"                              \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\"                                    \
	-DPACKAGE_BIN_DIR=\""$(bindir)"\"                                      \
	-DPACKAGE_LOCALSTATE_DIR=\""$(localstatedir)"\"                        \
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\"                                \
	-DPACKAGE_LIB_DIR=\""$(libdir)"\"                                      \
	-D_POSIX_PTHREAD_SEMANTICS -D_REENTRANT                                \
	-DUDISKS_COMPILATION                                                   \
	$(POLKIT_GOBJECT_1_CFLAGS)                                             \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(GUDEV_CFLAGS)                                                        \
	$(BLOCKDEV_CFLAGS)                                                     \
	$(WARN_CFLAGS)                                                         \
	$(NULL)

if ENABLE_DAEMON

ZFS_MODULE_DIR = $(top_srcdir)/modules/zfs

MODULE_SO = libudisks2_zfs.so

BUILT_SOURCES =                                                                \
	$(NULL)

libudisks2_zfsdir = $(libdir)/udisks2/modules
libudisks2_zfs_LTLIBRARIES = libudisks2_zfs.la

libudisks2_zfsincludedir = $(includedir)/udisks2/udisks

libudisks2_zfs_la_SOURCES =                                                    \
	$(BUILT_SOURCES)                                                       \
	udiskslinuxmodulezfs.h                                                 \
	udiskslinuxmodulezfs.c                                                 \
	udiskslinuxmanagerzfs.h                                                \
	udiskslinuxmanagerzfs.c                                                \
	udiskslinuxpoolobjectzfs.h                                             \
	udiskslinuxpoolobjectzfs.c                                             \
	udiskslinuxblockzfs.h                                                  \
	udiskslinuxblockzfs.c                                                  \
	udiskslinuxfilesystemzfs.h                                             \
	udiskslinuxfilesystemzfs.c                                             \
	udiskszfsdaemonutil.h                                                  \
	udiskszfsdaemonutil.c                                                  \
	udiskszfstypes.h                                                       \
	$(NULL)

libudisks2_zfs_la_CPPFLAGS =                                                   \
	$(AM_CPPFLAGS)                                                         \
	-DG_LOG_DOMAIN=\"libudisks2-zfs\"                                      \
	$(NULL)

libudisks2_zfs_la_CFLAGS =                                                     \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(GUDEV_CFLAGS)                                                        \
	$(POLKIT_GOBJECT_1_CFLAGS)                                             \
	$(BLOCKDEV_CFLAGS)                                                     \
	$(NULL)

libudisks2_zfs_la_LDFLAGS =                                                    \
	-export-dynamic                                                        \
	-avoid-version                                                         \
	-module                                                                \
	-no-undefined                                                          \
	-export-symbols-regex                                                  \
	 '^udisks_.*|g_io_module_load|g_io_module_unload|g_io_module_query'    \
	$(NULL)

libudisks2_zfs_la_LIBADD =                                                     \
	$(top_builddir)/src/libudisks-daemon.la                                \
	$(GLIB_LIBS)                                                           \
	$(GIO_LIBS)                                                            \
	$(GUDEV_LIBS)                                                          \
	$(POLKIT_GOBJECT_1_LIBS)                                               \
	$(BLOCKDEV_LIBS)                                                       \
	$(NULL)

all-local: module_link

endif # ENABLE_DAEMON

# ------------------------------------------------------------------------------

CLEANFILES =

EXTRA_DIST =                                                                   \
	$(NULL)

include ../Makefile.uninstalled

dist-hook:
	(for i in $(BUILT_SOURCES); do rm -f $(distdir)/$$i; done)

clean-local: module_unlink
	rm -f *~ $(BUILT_SOURCES)
