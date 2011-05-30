#!/bin/sh
# Install next3 patched e2fsprogs

# build e2fsprogs from e2fsprogs-snapshots git
#./configure
make
# install progs with .next3 suffix
install -T e2fsck/e2fsck /sbin/fsck.next3
install -T misc/mke2fs /sbin/mkfs.next3
install -T misc/tune2fs /sbin/tunefs.next3
install -T misc/dumpe2fs /sbin/dumpfs.next3
install -T misc/lsattr /sbin/lsattr.next3
install -T misc/chattr /sbin/chattr.next3
install -T resize/resize2fs /sbin/resize.next3
install -T contrib/e4snapshot /sbin/snapshot.next3

