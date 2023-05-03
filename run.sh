#!/bin/bash -e


export CROSS_COMPILE=arm-none-linux-gnueabihf-  # This may be different if using a different compiler!
export ARCH=arm
export LOADADDR=0x10008000
export WILC=y


# Always build zImage, most common. If LOADADDR is set, then uImage is also built
TARGETS="zImage"
if [ -n "${LOADADDR}" ]; then TARGETS+=" uImage"; fi

# Build the actual kernel, binary files, and loadable modules.
# Use as many CPUs to do this as possible.
make -j"$(nproc)" && make ${TARGETS} && make modules

# Create a temporary directory to install the kernel to in order to use that as a base directory for a tarball.
# Also creates a temporary file that is used as the tarball name.
TEMPDIR=/tmp/mods

rm -rf "$TEMPDIR"
mkdir -p "$TEMPDIR"
mkdir "${TEMPDIR}/boot/"

# Adds "arch/arm/boot/" path prefix to each TARGET
cp $(for i in ${TARGETS}; do echo arch/arm/boot/$i; done) "${TEMPDIR}"/boot/

# Copy the full .config file to the target, this is optional and can be removed
cp .config "${TEMPDIR}"/boot/config

# Copy all of the generated FDT binary files to the target
cp arch/arm/boot/dts/*ts*.dtb  "${TEMPDIR}"/boot/

# Install kernel modules to the target
INSTALL_MOD_PATH="${TEMPDIR}" make modules_install

# Install kernel headers to the target, this is optional in most cases and can be removed to save space on the target
make headers_install INSTALL_HDR_PATH="${TEMPDIR}"

# If WILC is set to "y", then build the external module for the WILC300 Wi-Fi/BLE device.
# Note that this expects the source to be available as a subfolder in the kernel. See the above sections 
# for details on getting the driver source if it is used on this specific platform.
if [ "${WILC}" == "y" ]; then
    CONFIG_WILC_SPI=m INSTALL_MOD_PATH="${TEMPDIR}" make M=../wilc3000-external-module modules modules_install
fi

# Use fakeroot to properly set permissions on the target folder as well as create a tarball from this.
fakeroot sh -c "chmod 755 ${TEMPDIR};
        chown -R root:root ${TEMPDIR};
        tar czf kernel.tar.gz -C ${TEMPDIR} .";

# Create a final output tarball and cleanup all of the temporary files and folder.
# cp ${TEMPFILE}.tar.gz embeddedTS-linux-lts-"$(date +"%Y%m%d")"-"$(git describe --abbrev=8 --dirty --always)".tar.gz
