SUBDIRS = src man init testing

if USE_SCRIPTS
  SUBDIRS += scripts
endif

if USE_SILENT_RULES
  AM_MAKEFLAGS = -s
endif

ACLOCAL_AMFLAGS = -I m4/config

EXTRA_DIST = Doxyfile.in LICENSE Android.common.mk.in Android.common.mk Android.mk
CLEANFILES = Doxyfile
BUILT_SOURCES = Android.common.mk
MAINTAINERCLEANFILES = Android.common.mk

if USE_DEV_HEADERS
config_includedir = $(ipseclibdir)/include
nodist_config_include_HEADERS = config.h
endif

Android.common.mk :	Android.common.mk.in configure.in
		$(AM_V_GEN) \
		sed \
		-e "s:\@PACKAGE_VERSION\@:$(PACKAGE_VERSION):" \
		$(srcdir)/$@.in > $@

Doxyfile :	Doxyfile.in
		$(AM_V_GEN) \
		sed \
		-e "s:\@PACKAGE_VERSION\@:$(PACKAGE_VERSION):" \
		-e "s:\@PACKAGE_NAME\@:$(PACKAGE_NAME):" \
		-e "s:\@SRC_DIR\@:$(srcdir):" \
		$(srcdir)/$@.in > $@

apidoc :	Doxyfile
		doxygen

clean-local:
		rm -rf apidoc
