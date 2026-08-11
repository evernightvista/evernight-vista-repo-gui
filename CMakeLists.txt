cmake_minimum_required(VERSION 3.16)
project(evernight-vista-repo-gui VERSION 1.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6I18n REQUIRED)
find_package(KF6WidgetsAddons REQUIRED)

# ki18n_install() installs the compiled .mo file using the .po basename as the
# translation domain.  If the source tree stores translations as po/ja.po,
# po/fr.po, etc., that would install ja.mo/fr.mo and Fedora's %find_lang
# evernight-vista-repo-gui will not find them.
#
# Normalize flat PO layouts to the KI18n layout expected by ki18n_install():
#   po/ja.po -> <build>/po-evernight-vista-repo-gui/ja/evernight-vista-repo-gui.po
# This keeps installation handled by ki18n_install() while producing:
#   /usr/share/locale/ja/LC_MESSAGES/evernight-vista-repo-gui.mo
function(evernight_ki18n_install po_dir domain)
    set(src_dir "${CMAKE_CURRENT_SOURCE_DIR}/${po_dir}")

    if(NOT EXISTS "${src_dir}")
        return()
    endif()

    file(GLOB flat_po_files CONFIGURE_DEPENDS "${src_dir}/*.po")
    if(flat_po_files)
        set(normalized_dir "${CMAKE_CURRENT_BINARY_DIR}/po-${domain}")
        file(MAKE_DIRECTORY "${normalized_dir}")

        foreach(po_file IN LISTS flat_po_files)
            get_filename_component(lang "${po_file}" NAME_WE)
            file(MAKE_DIRECTORY "${normalized_dir}/${lang}")
            configure_file(
                "${po_file}"
                "${normalized_dir}/${lang}/${domain}.po"
                COPYONLY
            )
        endforeach()

        ki18n_install("${normalized_dir}")
        return()
    endif()

    # If the tree already uses KI18n's native layout, keep it unchanged.
    ki18n_install("${src_dir}")
endfunction()

add_executable(evernight-vista-repo-gui
    src/main.cpp
    src/MainWindow.cpp
    src/RepoManager.cpp
    src/MirrorTester.cpp
)

target_link_libraries(evernight-vista-repo-gui
    Qt6::Core Qt6::Widgets Qt6::Network
    KF6::CoreAddons KF6::I18n KF6::WidgetsAddons
)

add_executable(evernight-vista-repo-helper
    helper/evernight-vista-repo-helper.cpp
)

target_link_libraries(evernight-vista-repo-helper
    Qt6::Core
    KF6::I18n
)

evernight_ki18n_install(po evernight-vista-repo-gui)
evernight_ki18n_install(helper/po evernight-vista-repo-helper)

# 安装 desktop 文件到 applications 目录
install(FILES data/evernight-vista-repo-gui.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

# 安装 Polkit policy 文件
install(FILES data/org.evernight.vista.repo.policy
        DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)

install(TARGETS evernight-vista-repo-gui
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(TARGETS evernight-vista-repo-helper
        RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
