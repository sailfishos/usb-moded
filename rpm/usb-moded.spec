Name:     usb-moded
Version:  0.85
Release:  2
Summary:  USB mode controller
Group:    System/System Control
License:  LGPLv2
URL:      https://github.com/nemomobile/usb-moded
Source0:  %{name}-%{version}.tar.bz2
Source1:  usb_moded.conf

BuildRequires: pkgconfig(dbus-1)
BuildRequires: pkgconfig(dbus-glib-1)
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(udev)
BuildRequires: pkgconfig(libkmod)
BuildRequires: doxygen
BuildRequires: pkgconfig(libsystemd-daemon)

Requires: lsof
Requires: usb-moded-configs
Requires: usb-moded-diagnostics-config
Requires: busybox-symlinks-dhcp
Requires(post): systemd
Requires(postun): systemd

%description
Usb_moded is a daemon to control the USB states. For this
it loads unloads the relevant usb gadget modules, keeps track
of the filesystem(s) and notifies about changes on the DBUS
system bus.

%package devel
Summary:  USB mode controller - development files
Group:    Development/Libraries

%description devel
Usb_moded is a daemon to control the USB states. For this
it loads unloads the relevant usb gadget modules, keeps track
of the filesystem(s) and notifies about changes on the DBUS
system bus.

This package contains the files needed to program for usb_moded.

%package doc
Summary:  USB mode controller - documentation
Group:    Documentation

%description doc
Usb_moded is a daemon to control the USB states. For this
it loads unloads the relevant usb gadget modules, keeps track
of the filesystem(s) and notifies about changes on the DBUS
system bus.

This package contains the documentation.

%package developer-mode
Summary:  USB mode controller - developer mode config
Group:  Config

%description developer-mode
Usb_moded is a daemon to control the USB states. For this
it loads unloads the relevant usb gadget modules, keeps track
of the filesystem(s) and notifies about changes on the DBUS
system bus.

This package contains the developer mode config, which enables
usb networking.

%package mtp-mode
Summary:  USB mode controller - mtp mode config
Group:  Config
Requires: buteo-mtp

%description mtp-mode
Usb_moded is a daemon to control the USB states. For this
it loads unloads the relevant usb gadget modules, keeps track
of the filesystem(s) and notifies about changes on the DBUS
system bus.

This package contains the mtp mode config.

%package mass-storage-mode
Summary:  USB mode controller - mass-storage mode config
Group:  Config

%description mass-storage-mode
Usb_moded is a daemon to control the USB states. For this
it loads unloads the relevant usb gadget modules, keeps track
of the filesystem(s) and notifies about changes on the DBUS
system bus.

This package contains the mass-storage mode config.

%package adb-mode
Summary:  USB mode controller - android adb mode config
Group:  Config

%description adb-mode
Usb_moded is a daemon to control the USB states. For this
it loads unloads the relevant usb gadget modules, keeps track
of the filesystem(s) and notifies about changes on the DBUS
system bus.

This package contains the adb config for use with the android
gadget driver.

%package diag-mode-android
Summary:  USB mode controller - android diag mode config
Group:  Config

%description diag-mode-android
Usb_moded is a daemon to control the USB states. For this
it loads unloads the relevant usb gadget modules, keeps track
of the filesystem(s) and notifies about changes on the DBUS
system bus.

This package contains the diag config for use with the android
gadget driver.

%package diag-mode-androidv5-qcom
Summary:  USB mode controller - android v5 or newer diag mode config for qcom
Group:  Config

%description diag-mode-androidv5-qcom
Usb_moded is a daemon to control the USB states. For this
it loads unloads the relevant usb gadget modules, keeps track
of the filesystem(s) and notifies about changes on the DBUS
system bus.

This package contains the diag config for use with the android
gadget driver.


%package acm-mode-android
Summary:  USB mode controller - android acm mode config
Group:  Config

%description acm-mode-android
Usb_moded is a daemon to control the USB states. For this
it loads unloads the relevant usb gadget modules, keeps track
of the filesystem(s) and notifies about changes on the DBUS
system bus.

This package contains the acm config for use with the android
gadget driver.

%package developer-mode-android
Summary:  USB mode controller - android developer mode config
Group:  Config

%description developer-mode-android
Usb_moded is a daemon to control the USB states. For this
it loads unloads the relevant usb gadget modules, keeps track
of the filesystem(s) and notifies about changes on the DBUS
system bus.

This package contains the developer mode config for use with
the android gadget. This will provide usb networking.

%package mtp-mode-android
Summary:  USB mode controller - android mtp mode config
Group:  Config

%description mtp-mode-android
Usb_moded is a daemon to control the USB states. For this
it loads unloads the relevant usb gadget modules, keeps track
of the filesystem(s) and notifies about changes on the DBUS
system bus.

This package contains the mtp mode config.

%package pc-suite-mode-android
Summary:  USB mode controller - android pc suite  mode config
Group:  Config

%description pc-suite-mode-android
Usb_moded is a daemon to control the USB states. For this
it loads unloads the relevant usb gadget modules, keeps track
of the filesystem(s) and notifies about changes on the DBUS
system bus.

This package contains the android pc suite mode config.

%package at-mode-android
Summary:  USB mode controller - android at modem mode config
Group:  Config

%description at-mode-android
Usb_moded is a daemon to control the USB states. For this
it loads unloads the relevant usb gadget modules, keeps track
of the filesystem(s) and notifies about changes on the DBUS
system bus.

This package contains the android at modem port mode config.

%package host-mode-jolla
Summary:  USB mode controller - host mode switch for Jolla
Group:  Config

%description host-mode-jolla
Usb_moded is a daemon to control the USB states. For this
it loads unloads the relevant usb gadget modules, keeps track
of the filesystem(s) and notifies about changes on the DBUS
system bus.

This package contains the config to switch the first Jolla phone 
in host mode.

%package defaults
Summary: USB mode controller - default configuration
Group: Config
Provides: usb-moded-configs
Requires: usb-moded-developer-mode

%description defaults
This package provides the default configuration for usb-moded, so
basic functionality is provided (i.e. usb networking, ask and charging
modes)

%package defaults-android
Summary: USB mode controller - default configuration
Group: Config
Provides: usb-moded-configs
Requires: usb-moded-developer-mode-android

%description defaults-android
This package provides the default configuration for usb-moded, so
basic functionality is provided (i.e. usb networking, ask and charging
modes with the android gadget driver)

%package diagnostics-config
Summary: USB mode controller - config data for diagnostics mode
Group: Config

%description diagnostics-config
This package contains the diagnostics info needed to configure a
diagnotic mode

%package connection-sharing-android-config
Summary:  USB mode controller - USB/cellular data connection sharing config
Group:  Config

%description connection-sharing-android-config
Usb_moded is a daemon to control the USB states. For this
it loads unloads the relevant usb gadget modules, keeps track
of the filesystem(s) and notifies about changes on the DBUS
system bus.

This package contains configuration to enable sharing the cellular data
connection over the USB with the android gadget driver.

%package connection-sharing-android-connman-config
Summary:  USB mode controller - USB/cellular data connection sharing config
Group:  Config

%description connection-sharing-android-connman-config
Usb_moded is a daemon to control the USB states. For this
it loads unloads the relevant usb gadget modules, keeps track
of the filesystem(s) and notifies about changes on the DBUS
system bus.

This package contains configuration to enable sharing the cellular data
connection over the USB with the connman gadget driver.

%package mass-storage-android-config
Summary:  USB mode controller - mass-storage config with android gadget
Group:  Config

%description mass-storage-android-config
Usb_moded is a daemon to control the USB states. For this
it loads unloads the relevant usb gadget modules, keeps track
of the filesystem(s) and notifies about changes on the DBUS
system bus.

This package contains configuration to enable sharing over mass-storage
with the android gadget driver.

%package vfat-android-config
Summary:  USB mode controller - vfat config with tojblockd
Group:  Config
Requires: tojblockd

%description vfat-android-config
Usb_moded is a daemon to control the USB states. For this
it loads unloads the relevant usb gadget modules, keeps track
of the filesystem(s) and notifies about changes on the DBUS
system bus.

This package contains configuration to enable sharing over vfat
emulation with tojblockd and nbd.

%package systemd-rescue-mode
Summary: USB mode controller - systemd rescue mode support
Group:	Config

%Description systemd-rescue-mode
Usb_moded is a daemon to control the USB states. For this
it loads unloads the relevant usb gadget modules, keeps track
of the filesystem(s) and notifies about changes on the DBUS
system bus.

This package contains the configuration files for systemd to 
provide the rescue mode, so device does not get locked down
when the UI fails.

%prep
%setup -q

%build
%autogen
%configure --enable-app-sync --enable-meegodevlock --enable-debug --enable-connman --enable-systemd
make all doc %{?_smp_mflags}

%install
%make_install
install -m 644 -D src/usb_moded-dbus.h %{buildroot}/%{_includedir}/%{name}/usb_moded-dbus.h
install -m 644 -D src/usb_moded-modes.h %{buildroot}/%{_includedir}/%{name}/usb_moded-modes.h
install -m 644 -D src/usb_moded-appsync-dbus.h %{buildroot}/%{_includedir}/%{name}/usb_moded-appsync-dbus.h
install -m 644 -D src/com.meego.usb_moded.xml %{buildroot}/%{_includedir}/%{name}/com.meego.usb_moded.xml
install -m 644 -D usb_moded.pc %{buildroot}/%{_libdir}/pkgconfig/usb_moded.pc
install -d %{buildroot}/%{_docdir}/%{name}/html/
install -m 644 docs/html/* %{buildroot}/%{_docdir}/%{name}/html/
install -m 644 docs/usb_moded-doc.txt %{buildroot}/%{_docdir}/%{name}/
install -m 644 -D debian/manpage.1 %{buildroot}/%{_mandir}/man1/usb-moded.1
install -m 644 -D debian/usb_moded.conf %{buildroot}/%{_sysconfdir}/dbus-1/system.d/usb_moded.conf
install -m 644 -D %{SOURCE1} %{buildroot}/%{_sysconfdir}/modprobe.d/usb_moded.conf
install -d %{buildroot}/%{_sysconfdir}/usb-moded
install -d %{buildroot}/%{_sysconfdir}/usb-moded/run
install -d %{buildroot}/%{_sysconfdir}/usb-moded/run-diag
install -d %{buildroot}/%{_sysconfdir}/usb-moded/dyn-modes
install -d %{buildroot}/%{_sysconfdir}/usb-moded/diag
install -m 644 -D config/dyn-modes/* %{buildroot}/%{_sysconfdir}/usb-moded/dyn-modes/
install -m 644 -D config/diag/* %{buildroot}/%{_sysconfdir}/usb-moded/diag/
install -m 644 -D config/run/* %{buildroot}/%{_sysconfdir}/usb-moded/run/
install -m 644 -D config/run-diag/* %{buildroot}/%{_sysconfdir}/usb-moded/run-diag/
install -m 644 -D config/mass-storage-jolla.ini %{buildroot}/%{_sysconfdir}/usb-moded/

touch %{buildroot}/%{_sysconfdir}/modprobe.d/g_ether.conf
touch %{buildroot}/%{_sysconfdir}/udhcpd.conf
#systemd stuff
install -d $RPM_BUILD_ROOT/lib/systemd/system/basic.target.wants/
install -m 644 -D systemd/%{name}.service %{buildroot}/lib/systemd/system/%{name}.service
ln -s ../%{name}.service $RPM_BUILD_ROOT/lib/systemd/system/basic.target.wants/%{name}.service
install -m 644 -D systemd/usb-moded-args.conf %{buildroot}/var/lib/environment/usb-moded/usb-moded-args.conf
install -m 755 -D systemd/turn-usb-rescue-mode-off %{buildroot}/%{_bindir}/turn-usb-rescue-mode-off
install -m 644 -D systemd/usb-rescue-mode-off.service %{buildroot}/lib/systemd/system/usb-rescue-mode-off.service
install -m 644 -D systemd/usb-rescue-mode-off.service %{buildroot}/lib/systemd/system/graphical.target.wants/usb-rescue-mode-off.service
install -m 644 -D systemd/usb-moded.conf %{buildroot}/%{_sysconfdir}/tmpfiles.d/usb-moded.conf


%preun
systemctl daemon-reload || :

%post
systemctl daemon-reload || :

%files
%defattr(-,root,root,-)
%doc debian/copyright
%dir %{_sysconfdir}/usb-moded
%dir %{_sysconfdir}/usb-moded/dyn-modes
%dir %{_sysconfdir}/usb-moded/run
%config(noreplace) %{_sysconfdir}/dbus-1/system.d/usb_moded.conf
%config(noreplace) %{_sysconfdir}/modprobe.d/usb_moded.conf
%ghost %config(noreplace) %{_sysconfdir}/modprobe.d/g_ether.conf
%ghost %{_sysconfdir}/udhcpd.conf
%{_sbindir}/usb_moded
%{_sbindir}/usb_moded_util
%{_mandir}/man1/usb-moded.1.gz
/lib/systemd/system/%{name}.service
/lib/systemd/system/basic.target.wants/%{name}.service
%config %{_sysconfdir}/tmpfiles.d/usb-moded.conf

%files devel
%defattr(-,root,root,-)
%doc debian/copyright
%{_includedir}/%{name}/*
%{_libdir}/pkgconfig/usb_moded.pc

%files doc
%defattr(-,root,root,-)
%doc debian/changelog debian/copyright LICENSE
%{_docdir}/%{name}/*
%{_docdir}/%{name}/html/*

%files developer-mode
%defattr(-,root,root,-)
%{_sysconfdir}/usb-moded/dyn-modes/developer_mode.ini

%files mtp-mode
%defattr(-,root,root,-)
%{_sysconfdir}/usb-moded/dyn-modes/mtp_mode.ini
%{_sysconfdir}/usb-moded/run/mtp.ini

%files mass-storage-mode
%defattr(-,root,root,-)
%{_sysconfdir}/usb-moded/dyn-modes/mass-storage.ini

%files diag-mode-android
%defattr(-,root,root,-)
%{_sysconfdir}/usb-moded/dyn-modes/diag_mode_old.ini
%{_sysconfdir}/usb-moded/run/adb-diag.ini

%files diag-mode-androidv5-qcom
%defattr(-,root,root,-)
%{_sysconfdir}/usb-moded/dyn-modes/diag_mode.ini
%{_sysconfdir}/usb-moded/run/adb-diag.ini
%{_sysconfdir}/usb-moded/run/diag-adb-prepare.ini

%files acm-mode-android
%defattr(-,root,root,-)
%{_sysconfdir}/usb-moded/dyn-modes/android_acm.ini

%files developer-mode-android
%defattr(-,root,root,-)
%{_sysconfdir}/usb-moded/dyn-modes/developer_mode-android.ini
%{_sysconfdir}/usb-moded/run/udhcpd-developer-mode.ini

%files adb-mode
%defattr(-,root,root,-)
%{_sysconfdir}/usb-moded/dyn-modes/adb_mode.ini
%{_sysconfdir}/usb-moded/run/adb-startserver.ini
%{_sysconfdir}/usb-moded/run/adb-prepare.ini
%{_sysconfdir}/usb-moded/run/udhcpd-adb-mode.ini

%files mtp-mode-android
%defattr(-,root,root,-)
%{_sysconfdir}/usb-moded/dyn-modes/mtp_mode-android.ini

%files pc-suite-mode-android
%defattr(-,root,root,-)
%{_sysconfdir}/usb-moded/dyn-modes/pc_suite-android.ini

%files at-mode-android
%defattr(-,root,root,-)
%{_sysconfdir}/usb-moded/dyn-modes/android_at.ini

%files defaults
%defattr(-,root,root,-)

%files defaults-android
%defattr(-,root,root,-)

%files diagnostics-config
%defattr(-,root,root,-)
%dir %{_sysconfdir}/usb-moded/diag
%dir %{_sysconfdir}/usb-moded/run-diag
%{_sysconfdir}/usb-moded/diag/qa_diagnostic_mode.ini
%{_sysconfdir}/usb-moded/run-diag/qa-diagnostic.ini

%files connection-sharing-android-config
%defattr(-,root,root,-)
%{_sysconfdir}/usb-moded/dyn-modes/connection_sharing.ini
%{_sysconfdir}/usb-moded/run/udhcpd-connection-sharing.ini

%files connection-sharing-android-connman-config
%defattr(-,root,root,-)
%{_sysconfdir}/usb-moded/dyn-modes/connection_sharing-android-connman.ini

%files mass-storage-android-config
%defattr(-,root,root,-)
%{_sysconfdir}/usb-moded/dyn-modes/mass_storage_android.ini
%{_sysconfdir}/usb-moded/mass-storage-jolla.ini

%files vfat-android-config
%defattr(-,root,root,-)
%{_sysconfdir}/usb-moded/dyn-modes/vfat_android.ini
%{_sysconfdir}/usb-moded/run/vfat.ini

%files host-mode-jolla
%defattr(-,root,root,-)
%{_sysconfdir}/usb-moded/dyn-modes/host_mode_jolla.ini

%files systemd-rescue-mode
%defattr(-,root,root,-)
/var/lib/environment/usb-moded/usb-moded-args.conf
%{_bindir}/turn-usb-rescue-mode-off
/lib/systemd/system/usb-rescue-mode-off.service
/lib/systemd/system/graphical.target.wants/usb-rescue-mode-off.service
