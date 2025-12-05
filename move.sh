#!/bin/zsh

# Script to copy PortaPack Mayhem firmware files

# Device ID is the SD card ID
# The DEVICE_ID is "3536-3330"
#
DEVICE_ID="3536-3330" # SD card ID connected to this computer. REPLACE THIS WITH THE ACTUAL SD CARD ID.
USER=$(whoami) # Current user

# Source paths
FIRMWARE_SRC="/home/${USER}/mayhem-firmware/build/firmware"
APP_SRC="/home/${USER}/mayhem-firmware/build/firmware/firmware_tar/APPS"

# Destination paths
FIRMWARE_DEST="/media/${USER}/${DEVICE_ID}/FIRMWARE"
APP_DEST="/media/${USER}/${DEVICE_ID}/APPS"

# Copy firmware files
echo "Copying firmware files..."
cp -f "$FIRMWARE_SRC/portapack-mayhem-firmware.bin" "$FIRMWARE_DEST/" && \
echo "✓ Copied portapack-mayhem-firmware.bin"

cp -f "$FIRMWARE_SRC/portapack-mayhem_OCI.ppfw.tar" "$FIRMWARE_DEST/" && \
echo "✓ Copied portapack-mayhem_OCI.ppfw.tar"

# Copy app file
echo "Copying app file..."
cp -f "$APP_SRC/ext_scanner.ppma" "$APP_DEST/" && \
echo "✓ Copied ext_scanner.ppma"

echo "All files copied successfully!"