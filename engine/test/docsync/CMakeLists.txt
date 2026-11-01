add_executable(docsync_test WIN32
    docsync_test.cpp docsync_test.h
)
target_include_directories(docsync_test PRIVATE
    ../../../plugins/interfaces
    ../../src
)

target_link_libraries(docsync_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Test
    qlcplusengine
)
