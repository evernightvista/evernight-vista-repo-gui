Name:           evernight-vista-repo-gui
Version:        1.0.1
Release:        1%{?dist}
Summary:        GUI tool for managing Evernight Vista repository mirrors

License:        GPL-3.0-or-later
URL:            https://github.com/evernightvista/evernight-vista-repo-gui
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  gettext
BuildRequires:  desktop-file-utils
BuildRequires:  cmake(Qt6Core)
BuildRequires:  cmake(Qt6Widgets)
BuildRequires:  cmake(Qt6Network)
BuildRequires:  cmake(KF6CoreAddons)
BuildRequires:  cmake(KF6I18n)
BuildRequires:  cmake(KF6WidgetsAddons)

Requires:       polkit
Requires:       evernight-vista-repos

%description
Evernight Vista Repo GUI is a graphical utility for managing repository
mirror configuration. It ships with a privileged helper integrated through
Polkit and localized user interface translations.


%prep
%autosetup -n %{name}-%{version}


%build
%cmake
%cmake_build


%install
%cmake_install

%find_lang evernight-vista-repo-gui
%find_lang evernight-vista-repo-helper
cat evernight-vista-repo-helper.lang >> evernight-vista-repo-gui.lang


%check
desktop-file-validate %{buildroot}%{_datadir}/applications/evernight-vista-repo-gui.desktop


%files -f evernight-vista-repo-gui.lang
%{_bindir}/evernight-vista-repo-gui
%{_libexecdir}/evernight-vista-repo-helper
%{_datadir}/applications/evernight-vista-repo-gui.desktop
%{_datadir}/polkit-1/actions/org.evernight.vista.repo.policy


%changelog
* Sat Aug 15 2026 KairikiFedora <13278297951@sina.cn> - 1.0.1-1
- Fix Detect repo bug

* Tue Aug 11 2026 KairikiFedora <13278297951@sina.cn> - 1.0.0-1
- Package GUI executable, helper executable, desktop file, Polkit policy, and translations
