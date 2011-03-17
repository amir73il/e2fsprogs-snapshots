#!/bin/sh
# Install next4 patched e2fsprogs

# build e2fsprogs from e2fsprogs-snapshots git
#./configure
make
# install progs with .next4 suffix
install -T e2fsck/e2fsck /sbin/fsck.next4
install -T misc/mke2fs /sbin/mkfs.next4
install -T misc/tune2fs /sbin/tunefs.next4
install -T misc/dumpe2fs /sbin/dumpfs.next4
install -T misc/lsattr /sbin/lsattr.next4
install -T misc/chattr /sbin/chattr.next4
install -T misc/lsattr /sbin/lssnap
install -T misc/chattr /sbin/chsnap
install -T resize/resize2fs /sbin/resize.next4
# next3 script is generic and chooses the FS type by it's own name
install -T next3 /sbin/next4
