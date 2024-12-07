#!/bin/bash
# Script outline to install and build kernel.
# Author: Alex Hall.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.1.10
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

if [ ! -d "$OUTDIR" ]; then
    mkdir -p ${OUTDIR}
    if [ ! -d "$OUTDIR" ]; then
        echo "Directory {$OUTDIR} failed to create."
        exit 1
    fi
fi

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # DONE: Add your kernel build steps here
    # 1) Clean
    echo "I'm at step 1"
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
    # 2) Default Config
    echo "I'm at step 2"
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    # 3) Build Kernel Image
    echo "I'm at step 3"
    make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
    # 4) Build Modules and Device Tree
    echo "I'm at step 4"
    # make ARCH={$ARCH} CROSS_COMPILE={$CROSS_COMPILE} modules (he says we can skip modules for saving time)
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs
fi

cd ${OUTDIR}/linux-stable
echo "Adding the Image in outdir"
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}/

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# DONE: Create necessary base directories
mkdir -p {$OUTDIR}/rootfs/{bin,dev,etc,home,lib,lib64,proc,sbin,sys,tmp,usr,var}
mkdir -p {$OUTDIR}/rootfs/{usr/lib,usr/bin,usr/sbin}
mkdir -p {$OUTDIR}/rootfs/{var/log}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # DONE:  Configure busybox
    make distclean
    make defconfig
else
    cd busybox
fi

# DONE: Make and install busybox
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install

echo "Library dependencies"
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "Shared library"

# DONE: Add library dependencies to rootfs
SYSROOT=$(aarch64-none-linux-gnu-gcc -print-sysroot)
cp -a ${SYSROOT}/lib/ld-linux-aarch64.so.1 ${OUTDIR}/rootfs/lib
cp -a ${SYSROOT}/lib64/libm.so.6 ${OUTDIR}/rootfs/lib64
cp -a ${SYSROOT}/lib64/libresolv.so.2 ${OUTDIR}/rootfs/lib64
cp -a ${SYSROOT}/lib64/libc.so.6 ${OUTDIR}/rootfs/lib64

# DONE: Make device nodes
ROOTFS=${OUTDIR}/rootfs
cd ${ROOTFS}

sudo -s << _EOF
mknod ${ROOTFS}/dev/null c 1 3
chmod 666 ${ROOTFS}/dev/null
_EOF

# sudo mknod -m 666 ${ROOTFS}/dev/null c 1 3
sudo mknod -m 600 ${ROOTFS}/dev/console c 5 1

# DONE: Clean and build the writer utility
cd ${FINDER_APP_DIR}
make clean
make CROSS_COMPILE=${CROSS_COMPILE} -o writer writer.c

# DONE: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cd ${OUTDIR}
mkdir -p rootfs/home/conf

cd ${FINDER_APP_DIR}
cp ./conf/username.txt ${OUTDIR}/rootfs/home/conf
cp ./conf/assignment.txt ${OUTDIR}/rootfs/home/conf
cp ./finder.sh ${OUTDIR}/rootfs/home
cp ./finder-test.sh ${OUTDIR}/rootfs/home
cp ./autorun-qemu.sh ${OUTDIR}/rootfs/home

# DONE: Chown the root directory
sudo chown -R root:root ${OUTDIR}/rootfs

# TODO: Create initramfs.cpio.gz
cd ${OUTDIR}/rootfs
sudo chown -R root:root *
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
cd ${OUTDIR}
gzip -f initramfs.cpio