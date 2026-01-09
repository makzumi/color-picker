#!/bin/bash
# set -e
sudo rm /usr/local/bin/colorpicker
echo "Removed /usr/local/bin/colorpicker (if it existed)"
sudo rm /usr/local/share/applications/colorpicker.desktop
echo "Removed /usr/local/share/applications/colorpicker.desktop (if it existed)"
sudo rm /usr/local/share/icons/hicolor/scalable/apps/colorpicker.svg
echo "Removed /usr/local/share/icons/hicolor/scalable/apps/colorpicker.svg (if it existed)"
rm -f ~/.config/autostart/colorpicker.desktop
echo "Removed ~/.config/autostart/colorpicker.desktop (if it existed)"