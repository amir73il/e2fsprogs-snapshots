#!/bin/sh
# Install ext4dev patched e2fsprogs

# build e2fsprogs from e2fsprogs-snapshots git
#./configure
make
# install progs with .ext4dev suffix
install -T e2fsck/e2fsck /sbin/fsck.ext4dev
install -T misc/mke2fs /sbin/mkfs.ext4dev
install -T misc/tune2fs /sbin/tunefs.ext4dev
install -T misc/dumpe2fs /sbin/dumpfs.ext4dev
install -T misc/lsattr /sbin/lsattr.ext4dev
install -T misc/chattr /sbin/chattr.ext4dev
install -T misc/lsattr /sbin/lssnap
install -T misc/chattr /sbin/chsnap
install -T resize/resize2fs /sbin/resize.ext4dev
# next3 script is generic and chooses the FS type by it's own name
install -T next3 /sbin/ext4dev
