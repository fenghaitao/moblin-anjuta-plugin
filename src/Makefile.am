# Sample Makefile for a anjuta plugin.

# Plugin Icon file
anjuta_plugin_sdk_pixmapsdir = $(anjuta_image_dir)
anjuta_plugin_sdk_pixmaps_DATA = anjuta-plugin-sdk.png

# Plugin UI file
anjuta_plugin_sdk_uidir = $(anjuta_ui_dir)
anjuta_plugin_sdk_ui_DATA = anjuta-plugin-sdk.ui

# Plugin description file
plugin_in_files = anjuta-plugin-sdk.plugin.in
%.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

anjuta_plugin_sdk_plugindir = $(anjuta_plugin_dir)
anjuta_plugin_sdk_plugin_DATA = $(plugin_in_files:.plugin.in=.plugin)

# NOTE :
# The naming convention is very intentional
# We are forced to use the prefix 'lib' by automake and libtool
#    There is probably a way to avoid it but it is not worth to effort
#    to find out.
# The 'anjuta_' prfix is a safety measure to avoid conflicts where the
#    plugin 'libpython.so' needs to link with the real 'libpython.so'

# Include paths
INCLUDES = \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	-DANJUTA_DATA_DIR=\"$(anjuta_data_dir)\" \
	-DANJUTA_PLUGIN_DIR=\"$(anjuta_plugin_dir)\" \
	-DANJUTA_IMAGE_DIR=\"$(anjuta_image_dir)\" \
	-DANJUTA_GLADE_DIR=\"$(anjuta_glade_dir)\" \
	-DANJUTA_UI_DIR=\"$(anjuta_ui_dir)\" \
	-DPACKAGE_DATA_DIR=\"$(datadir)\" \
	-DPACKAGE_SRC_DIR=\"$(srcdir)\" \
	$(LIBANJUTA_CFLAGS) \
	$(GNOME_VFS_CFLAGS)

# Where to install the plugin
plugindir = $(anjuta_plugin_dir)

# The plugin
plugin_LTLIBRARIES = libanjuta-plugin-sdk.la

# Plugin sources
libanjuta_plugin_sdk_la_SOURCES = plugin.c plugin.h \
				  beaver-target.c beaver-target.h \
				  beaver-target-qemu.c beaver-target-qemu.h \
				  beaver-target-device.c beaver-target-device.h \
				  beaver-util.c beaver-util.h \
				  beaver-settings-page.c beaver-settings-page.h \
				  beaver.h

# Plugin dependencies
libanjuta_plugin_sdk_la_LIBADD = \
	$(LIBANJUTA_LIBS) \
	$(GNOME_VFS_LIBS)

# Plugin link flags
libanjuta_plugin_sdk_la_LDFLAGS = -avoid-version -Wl,--no-undefined

EXTRA_DIST = \
	$(plugin_in_files) \
	$(anjuta_plugin_sdk_plugin_DATA) \
	$(anjuta_plugin_sdk_ui_DATA)  \
	$(anjuta_plugin_sdk_glade_DATA)  \
	$(anjuta_plugin_sdk_pixmaps_DATA)
