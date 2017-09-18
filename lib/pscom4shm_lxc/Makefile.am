#
# Copyright (C) 2016 Thorsten Born
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# Author: Thorsten Born <thorsten.born@rwth-aachen.de>
#
include $(top_srcdir)/common.am

AM_LDFLAGS += -avoid-version
AM_CPPFLAGS += -DLIBDIR=\"$(libdir)\" -I$(top_srcdir)/lib/pscom

lib_LTLIBRARIES = libpscom4shm_lxc.la

libpscom4shm_lxc_la_SOURCES = pscom_shm_lxc.c pscom_shm_lxc.h shm_lxc.c shm_lxc.h
libpscom4shm_lxc_la_CPPFLAGS = $(AM_CPPFLAGS) $(SHM_LXC_CPPFLAGS)
libpscom4shm_lxc_la_LDFLAGS = $(AM_LDFLAGS) $(SHM_LXC_LDFLAGS)
libpscom4shm_lxc_la_LIBADD =  -lpthread -luuid $(top_builddir)/lib/pscom/libpscom.la

install-exec-hook:
	cd $(DESTDIR)$(libdir) && $(RM) $(lib_LTLIBRARIES)
