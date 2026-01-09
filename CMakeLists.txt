cmake_minimum_required(VERSION 3.16)
project(colorpicker VERSION 0.1.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# Executable
add_executable(colorpicker
    main.cpp
    resources.qrc
)

# Link Qt libraries
target_link_libraries(colorpicker
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

# Install target
install(TARGETS colorpicker
    RUNTIME DESTINATION bin
)

# Desktop file for auto-start
install(FILES colorpicker.desktop
    DESTINATION share/applications
)

# Install icon
install(FILES icon.svg
    DESTINATION share/icons/hicolor/scalable/apps
    RENAME colorpicker.svg
)