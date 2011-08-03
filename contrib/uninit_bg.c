/*
 * uninit_bg - a tool to uninitialize block groups in an ext2 filesystem
 *
 * Copyright (C) 2011 Amir Goldstein <amir73il@users.sf.net>
 *
 * This file may be redistributed under the terms of the GNU General Public
 * License.
 *
 */

#include <ext2fs/ext2fs.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define USAGE "usage: %s [-n] [-v] [-f] filesystem [start_bg [end_bg]]\n"

int main(int argc, char **argv)
{
	errcode_t ret ;
	int flags ;
	int superblock = 0 ;
	int open_flags = EXT2_FLAG_RW ;
	int blocksize = 0 ;
	ext2_filsys fs = NULL;
	int verbose = 0 ;
	int dryrun = 0 ;
	int force = 0, skip ;
	dgrp_t i, from, to;
	struct ext2_super_block *sb;
	struct ext2_group_desc *gd;
	int c;

	while ( (c=getopt(argc, argv, "nvf")) != -1 ) {
		switch (c) {
		case 'n' :
			dryrun = 1 ;
			break ;
		case 'v' :
			verbose = 1 ;
			break ;
		case 'f' :
			force = 1 ;
			break ;
		default :
			fprintf(stderr, USAGE, argv[0]) ;
			return 1 ;
		}
	}

	if ( argc < optind+1 || argc > optind+3 ) {
		fprintf(stderr, USAGE, argv[0]) ;
		return 1 ;
	}

	ret = ext2fs_check_if_mounted(argv[optind], &flags) ;
	if ( ret ) {
		fprintf(stderr, "%s: failed to determine filesystem mount state  %s\n",
					argv[0], argv[optind]) ;
		return 1 ;
	}

	if ( (flags & EXT2_MF_MOUNTED) && !(flags & EXT2_MF_READONLY) ) {
		fprintf(stderr, "%s: filesystem %s is mounted rw\n",
					argv[0], argv[optind]) ;
		return 1 ;
	}

	ret = ext2fs_open(argv[optind], open_flags, superblock, blocksize,
							unix_io_manager, &fs);
	if ( ret ) {
		fprintf(stderr, "%s: failed to open filesystem %s\n",
					argv[0], argv[optind]) ;
		return 1 ;
	}
	
	sb = fs->super;
	if (!sb->s_feature_ro_compat &
			EXT4_FEATURE_RO_COMPAT_GDT_CSUM) {
		fprintf(stderr, "%s: filesystem %s uninit_bg feature is not set\n",
					argv[0], argv[optind]) ;
		return 1 ;
	}

	from = 1;
	to = fs->group_desc_count - 1;
	if ( argc > optind+1 )
		from = atoi(argv[optind+1]);
	if ( from < 1 || from >= fs->group_desc_count ) {
		fprintf(stderr, "%s: start group '%s' is out of valid range [1..%d]\n",
					argv[0], argv[optind+1], fs->group_desc_count - 1) ;
		return 1 ;
	}

	if ( argc > optind+2 )
		to = atoi(argv[optind+2]);
	if ( to < from || to >= fs->group_desc_count ) {
		fprintf(stderr, "%s: end group '%s' is out of valid range [%d..%d]\n",
					argv[0], argv[optind+2], from, fs->group_desc_count - 1) ;
		return 1 ;
	}
	
	printf("%s: uninitializing filesystem %s groups [%d..%d]\n",
			argv[0], argv[optind], from, to) ;
	gd = fs->group_desc + from;
	for (i = from; i <= to; i++, gd++) {
		skip = 0;
		if (ext2fs_bg_has_super(fs, i)) {
			fprintf(stderr, "%s: found super block backup in group %d!\n",
					argv[0], i);
			skip = 1;
		}
		if (gd->bg_free_blocks_count != sb->s_blocks_per_group) {
			fprintf(stderr, "%s: found %d used blocks in group %d!\n",
					argv[0],
					sb->s_blocks_per_group - gd->bg_free_blocks_count, i);
			skip = 1;
		}
		if (gd->bg_free_inodes_count != sb->s_inodes_per_group) {
			fprintf(stderr, "%s: found %d used inodes in group %d!\n",
					argv[0],
					sb->s_inodes_per_group - gd->bg_free_inodes_count, i);
			skip = 1;
		}
		if (gd->bg_used_dirs_count != 0) {
			fprintf(stderr, "%s: found %d used dirs in group %d!\n",
					argv[0],
					gd->bg_used_dirs_count, i);
			skip = 1;
		}
		if (skip && !force) {
			fprintf(stderr, "%s: skipping group %d - use -f to force uninit.\n",
					argv[0], i) ;
			continue;
		}
		gd->bg_itable_unused = 0;
		gd->bg_flags = EXT2_BG_INODE_UNINIT|EXT2_BG_BLOCK_UNINIT|EXT2_BG_INODE_ZEROED;
		ext2fs_group_desc_csum_set(fs, i);
	}

	if (!dryrun) {
		fs->flags &= ~EXT2_FLAG_SUPER_ONLY;
		ext2fs_mark_super_dirty(fs);
	}

	ret = ext2fs_close(fs) ;
	if ( ret ) {
		fprintf(stderr, "%s: error while closing filesystem\n", argv[0]) ;
		return 1 ;
	}

	return 0 ;
}
