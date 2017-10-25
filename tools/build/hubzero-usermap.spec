Name:       hubzero-usermap
Version:    2.1.0
Release:    1
Summary:    HUBzero Python common library code
License:    MIT
Packager:   David Benham <dbenham@purdue.edu>
Vendor:     Hubzero, http://hubzero.org
Url:        http://hubzero.org
BuildArch:  x86_64
BuildRequires: fuse-devel
Requires:   fuse
Source0:    %{name}-%{version}.tar.gz

%description

%prep
%setup -q -n %{name}-%{version}

%build

%install
rm -rf $RPM_BUILD_ROOT
make --makefile=$RPM_SOURCE_DIR/Makefile install DESTDIR=$RPM_BUILD_ROOT

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
/sbin/

%doc docs/copyright

%changelog
* Fri Sep 4 2015 David Benham <dbenham@purdue.edu>
- rpm packaging files modified in preparation of v2.0.0

* Sun Sep 14 2014 David Benham <dbenham@purdue.edu>
- rpm packaging files modified in preparation of v1.3.0

* Fri Sep 12 2014 David Benham <dbenham@purdue.edu>
- normalize rpm.mk
