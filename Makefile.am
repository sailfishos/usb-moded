SUBDIRS = dbus-gmain src

EXTRA_DIST = \
	autogen.sh \
	debian/* 

doc:
	cd $(top_srcdir)/docs && $(MAKE) && cd ..

.PHONY: doc

deb: dist
	-mkdir $(top_builddir)/debian-build
	cd $(top_builddir)/debian-build && tar zxf ../$(top_builddir)/$(PACKAGE)-$(VERSION).tar.gz
	cd $(top_builddir)/debian-build/$(PACKAGE)-$(VERSION) && dpkg-buildpackage -rfakeroot 
	-rm -rf $(top_builddir)/debian-build/$(PACKAGE)-$(VERSION)

DISTCLEANFILES = *~
