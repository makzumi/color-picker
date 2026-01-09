#!/bin/bash
set -e

# Extract version from CMakeLists.txt
VERSION=$(grep -oP 'project\(colorpicker VERSION \K[0-9.]+' CMakeLists.txt)
echo "Building Color Picker version $VERSION"

# Create build directory and configure with CMake
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
cd ..

# Create output directory for AppImage
mkdir -p dist

# Download appimagetool if it doesn't exist
if [ ! -f appimagetool-x86_64.AppImage ]; then
    echo "Downloading appimagetool..."
    wget -q https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage
    chmod +x appimagetool-x86_64.AppImage
fi

# Extract appimagetool if FUSE is not available (e.g., in CI environments)
if ! command -v fusermount &> /dev/null && [ ! -d squashfs-root ]; then
    echo "FUSE not available, extracting appimagetool..."
    ./appimagetool-x86_64.AppImage --appimage-extract > /dev/null
    APPIMAGETOOL="./squashfs-root/AppRun"
else
    APPIMAGETOOL="./appimagetool-x86_64.AppImage"
fi

# Create AppDir structure
mkdir -p ColorPicker.AppDir/usr/bin
mkdir -p ColorPicker.AppDir/usr/lib

# Copy binary
cp build/colorpicker ColorPicker.AppDir/usr/bin/

# Copy desktop file
cp colorpicker.desktop ColorPicker.AppDir/

# Copy icon (use one of the available images as the icon)
cp static/img/picker1.png ColorPicker.AppDir/colorpicker.png

# Create AppRun if it doesn't exist
if [ ! -f ColorPicker.AppDir/AppRun ]; then
    cat > ColorPicker.AppDir/AppRun << 'EOF'
#!/bin/bash
APPDIR="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="$APPDIR/usr/lib:$LD_LIBRARY_PATH"
exec "$APPDIR/usr/bin/colorpicker" "$@"
EOF
    chmod +x ColorPicker.AppDir/AppRun
fi

# Generate the AppImage in dist folder
"$APPIMAGETOOL" ColorPicker.AppDir dist/ColorPicker-${VERSION}-x86_64.AppImage

echo "AppImage created: dist/ColorPicker-${VERSION}-x86_64.AppImage"
