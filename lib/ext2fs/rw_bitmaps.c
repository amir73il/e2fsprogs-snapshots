/*
 * rw_bitmaps.c --- routines to read and write the  inode and block bitmaps.
 *
 * Copyright (C) 1993, 1994, 1994, 1996 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include <stdio.h>
#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <fcntl.h>
#include <time.h>
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include "ext2_fs.h"
#include "ext2fs.h"

#define blk64_t blk_t
#define __u64 __u32 
#define io_channel_read_blk64 io_channel_read_blk
#define io_channel_write_blk64 io_channel_write_blk
#define ext2fs_get_block_bitmap_range2 ext2fs_get_block_bitmap_range
#define ext2fs_set_block_bitmap_range2 ext2fs_set_block_bitmap_range
#define ext2fs_block_bitmap_loc(fs, group) \
	fs->group_desc[group].bg_block_bitmap
#define ext2fs_inode_bitmap_loc(fs, group) \
	fs->group_desc[group].bg_inode_bitmap
#define ext2fs_bg_flags_test(fs, group, bg_flag) \
	fs->group_desc[group].bg_flags & bg_flag
#define ext2fs_blocks_count(sb) sb->s_blocks_count

#include "ext2_fs.h"
#include "ext2fs.h"
#include "e2image.h"

#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
static errcode_t write_bitmaps(ext2_filsys fs, int do_inode, int do_block,
		int do_exclude)
#else
static errcode_t write_bitmaps(ext2_filsys fs, int do_inode, int do_block)
#endif
{
	dgrp_t 		i;
	unsigned int	j;
	int		block_nbytes, inode_nbytes;
	unsigned int	nbits;
	errcode_t	retval;
#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
	char 		*block_buf, *inode_buf, *exclude_buf;
#else
	char 		*block_buf, *inode_buf;
#endif
	int		csum_flag = 0;
	blk64_t		blk;
	blk64_t		blk_itr = fs->super->s_first_data_block;
	ext2_ino_t	ino_itr = 1;

	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	if (!(fs->flags & EXT2_FLAG_RW))
		return EXT2_ET_RO_FILSYS;

#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
	if (!EXT2_HAS_COMPAT_FEATURE(fs->super,
				EXT2_FEATURE_COMPAT_EXCLUDE_INODE))
		do_exclude = 0;

	if (do_exclude && !fs->exclude_blks) {
		retval = ext2fs_create_exclude_inode(fs, 0);
		if (retval)
			return retval;
	}

#endif
	if (EXT2_HAS_RO_COMPAT_FEATURE(fs->super,
				       EXT4_FEATURE_RO_COMPAT_GDT_CSUM))
		csum_flag = 1;

	inode_nbytes = block_nbytes = 0;
	if (do_block) {
		block_nbytes = EXT2_BLOCKS_PER_GROUP(fs->super) / 8;
		retval = ext2fs_get_mem(fs->blocksize, &block_buf);
		if (retval)
			return retval;
		memset(block_buf, 0xff, fs->blocksize);
	}
#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
	if (do_exclude) {
		block_nbytes = EXT2_BLOCKS_PER_GROUP(fs->super) / 8;
		retval = ext2fs_get_mem(fs->blocksize, &exclude_buf);
		if (retval)
			return retval;
		memset(exclude_buf, 0xff, fs->blocksize);
	}
#endif
	if (do_inode) {
		inode_nbytes = (size_t)
			((EXT2_INODES_PER_GROUP(fs->super)+7) / 8);
		retval = ext2fs_get_mem(fs->blocksize, &inode_buf);
		if (retval)
			return retval;
		memset(inode_buf, 0xff, fs->blocksize);
	}

	for (i = 0; i < fs->group_desc_count; i++) {
#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
		if (!do_block && !do_exclude)
#else
		if (!do_block)
#endif
			goto skip_block_bitmap;

		if (csum_flag && ext2fs_bg_flags_test(fs, i, EXT2_BG_BLOCK_UNINIT)
		    )
			goto skip_this_block_bitmap;

#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
		if (do_block)
			retval = ext2fs_get_block_bitmap_range2(fs->block_map,
					blk_itr, block_nbytes << 3, block_buf);
		if (retval)
			return retval;

		if (do_exclude)
			retval = ext2fs_get_block_bitmap_range2(fs->exclude_map,
					blk_itr, block_nbytes << 3, exclude_buf);
#else
		retval = ext2fs_get_block_bitmap_range2(fs->block_map,
				blk_itr, block_nbytes << 3, block_buf);
#endif
		if (retval)
			return retval;

#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
		if (do_block && i == fs->group_desc_count - 1) {
#else
		if (i == fs->group_desc_count - 1) {
#endif
			/* Force bitmap padding for the last group */
			nbits = ((ext2fs_blocks_count(fs->super)
				  - (__u64) fs->super->s_first_data_block)
				 % (__u64) EXT2_BLOCKS_PER_GROUP(fs->super));
			if (nbits)
				for (j = nbits; j < fs->blocksize * 8; j++)
					ext2fs_set_bit(j, block_buf);
		}
		blk = ext2fs_block_bitmap_loc(fs, i);
#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
		if (do_block && blk) {
#else
		if (blk) {
#endif
			retval = io_channel_write_blk64(fs->io, blk, 1,
							block_buf);
			if (retval)
				return EXT2_ET_BLOCK_BITMAP_WRITE;
		}
#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
		blk = fs->exclude_blks[i];
		if (do_exclude && blk) {
			retval = io_channel_write_blk64(fs->io, blk, 1,
						      exclude_buf);
			if (retval)
				return EXT2_ET_BLOCK_BITMAP_WRITE;
		}
#endif
	skip_this_block_bitmap:
		blk_itr += block_nbytes << 3;
	skip_block_bitmap:

		if (!do_inode)
			continue;

		if (csum_flag && ext2fs_bg_flags_test(fs, i, EXT2_BG_BLOCK_UNINIT)
		    )
			goto skip_this_inode_bitmap;

		retval = ext2fs_get_inode_bitmap_range(fs->inode_map,
				ino_itr, inode_nbytes << 3, inode_buf);
		if (retval)
			return retval;

		blk = ext2fs_inode_bitmap_loc(fs, i);
		if (blk) {
			retval = io_channel_write_blk64(fs->io, blk, 1,
						      inode_buf);
			if (retval)
				return EXT2_ET_INODE_BITMAP_WRITE;
		}
	skip_this_inode_bitmap:
		ino_itr += inode_nbytes << 3;

	}
	if (do_block) {
		fs->flags &= ~EXT2_FLAG_BB_DIRTY;
		ext2fs_free_mem(&block_buf);
	}
	if (do_inode) {
		fs->flags &= ~EXT2_FLAG_IB_DIRTY;
		ext2fs_free_mem(&inode_buf);
	}
	return 0;
}

#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
static errcode_t read_bitmaps(ext2_filsys fs, int do_inode, int do_block,
		int do_exclude)
#else
static errcode_t read_bitmaps(ext2_filsys fs, int do_inode, int do_block)
#endif
{
	dgrp_t i;
#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
	char *block_bitmap = 0, *inode_bitmap = 0, *exclude_bitmap = 0;
#else
	char *block_bitmap = 0, *inode_bitmap = 0;
#endif
	char *buf;
	errcode_t retval;
	int block_nbytes = EXT2_BLOCKS_PER_GROUP(fs->super) / 8;
	int inode_nbytes = EXT2_INODES_PER_GROUP(fs->super) / 8;
	int csum_flag = 0;
	int do_image = fs->flags & EXT2_FLAG_IMAGE_FILE;
	unsigned int	cnt;
	blk64_t	blk;
	blk64_t	blk_itr = fs->super->s_first_data_block;
	blk64_t   blk_cnt;
	ext2_ino_t ino_itr = 1;
	ext2_ino_t ino_cnt;

	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	fs->write_bitmaps = ext2fs_write_bitmaps;

#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
	if (!EXT2_HAS_COMPAT_FEATURE(fs->super,
				EXT2_FEATURE_COMPAT_EXCLUDE_INODE))
		do_exclude = 0;

	if (do_exclude && !fs->exclude_blks) {
		retval = ext2fs_create_exclude_inode(fs, 0);
		if (retval)
			return retval;
	}

#endif
	if (EXT2_HAS_RO_COMPAT_FEATURE(fs->super,
				       EXT4_FEATURE_RO_COMPAT_GDT_CSUM))
		csum_flag = 1;

	retval = ext2fs_get_mem(strlen(fs->device_name) + 80, &buf);
	if (retval)
		return retval;
	if (do_block) {
		if (fs->block_map)
			ext2fs_free_block_bitmap(fs->block_map);
		strcpy(buf, "block bitmap for ");
		strcat(buf, fs->device_name);
		retval = ext2fs_allocate_block_bitmap(fs, buf, &fs->block_map);
		if (retval)
			goto cleanup;
		retval = ext2fs_get_mem(do_image ? fs->blocksize :
					(unsigned) block_nbytes, &block_bitmap);
		if (retval)
			goto cleanup;
#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
	}
	if (do_exclude) {
		if (fs->exclude_map)
			ext2fs_free_block_bitmap(fs->exclude_map);
		strcpy(buf, "exclude bitmap for ");
		strcat(buf, fs->device_name);
		retval = ext2fs_allocate_block_bitmap(fs, buf, &fs->exclude_map);
		if (retval)
			goto cleanup;
		retval = ext2fs_get_mem(do_image ? fs->blocksize :
					(unsigned) block_nbytes, &exclude_bitmap);
		if (retval)
			goto cleanup;
	}
	if (!do_block && !do_exclude)
#else
	} else
#endif
		block_nbytes = 0;
	if (do_inode) {
		if (fs->inode_map)
			ext2fs_free_inode_bitmap(fs->inode_map);
		strcpy(buf, "inode bitmap for ");
		strcat(buf, fs->device_name);
		retval = ext2fs_allocate_inode_bitmap(fs, buf, &fs->inode_map);
		if (retval)
			goto cleanup;
		retval = ext2fs_get_mem(do_image ? fs->blocksize :
					(unsigned) inode_nbytes, &inode_bitmap);
		if (retval)
			goto cleanup;
	} else
		inode_nbytes = 0;
	ext2fs_free_mem(&buf);

	if (fs->flags & EXT2_FLAG_IMAGE_FILE) {
		blk = (fs->image_header->offset_inodemap / fs->blocksize);
		ino_cnt = fs->super->s_inodes_count;
		while (inode_nbytes > 0) {
			retval = io_channel_read_blk64(fs->image_io, blk++,
						     1, inode_bitmap);
			if (retval)
				goto cleanup;
			cnt = fs->blocksize << 3;
			if (cnt > ino_cnt)
				cnt = ino_cnt;
			retval = ext2fs_set_inode_bitmap_range(fs->inode_map,
					       ino_itr, cnt, inode_bitmap);
			if (retval)
				goto cleanup;
			ino_itr += fs->blocksize << 3;
			ino_cnt -= fs->blocksize << 3;
			inode_nbytes -= fs->blocksize;
		}
		blk = (fs->image_header->offset_blockmap /
		       fs->blocksize);
		blk_cnt = (blk64_t)EXT2_BLOCKS_PER_GROUP(fs->super) *
			fs->group_desc_count;
		while (block_nbytes > 0) {
#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
			if (do_exclude) {
				retval = EXT2_ET_BLOCK_BITMAP_READ;
				goto cleanup;
			}

#endif
			retval = io_channel_read_blk64(fs->image_io, blk++,
						     1, block_bitmap);
			if (retval)
				goto cleanup;
			cnt = fs->blocksize << 3;
			if (cnt > blk_cnt)
				cnt = blk_cnt;
			retval = ext2fs_set_block_bitmap_range2(fs->block_map,
				       blk_itr, cnt, block_bitmap);
			if (retval)
				goto cleanup;
			blk_itr += fs->blocksize << 3;
			blk_cnt -= fs->blocksize << 3;
			block_nbytes -= fs->blocksize;
		}
		goto success_cleanup;
	}

	for (i = 0; i < fs->group_desc_count; i++) {
		if (block_bitmap) {
			blk = ext2fs_block_bitmap_loc(fs, i);
			if (csum_flag &&
			    ext2fs_bg_flags_test(fs, i, EXT2_BG_BLOCK_UNINIT) &&
			    ext2fs_group_desc_csum_verify(fs, i))
				blk = 0;
			if (blk) {
				retval = io_channel_read_blk64(fs->io, blk,
					     -block_nbytes, block_bitmap);
				if (retval) {
					retval = EXT2_ET_BLOCK_BITMAP_READ;
					goto cleanup;
				}
			} else
				memset(block_bitmap, 0, block_nbytes);
			cnt = block_nbytes << 3;
			retval = ext2fs_set_block_bitmap_range2(fs->block_map,
					       blk_itr, cnt, block_bitmap);
			if (retval)
				goto cleanup;
#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
		}
		if (exclude_bitmap) {
			blk = fs->exclude_blks[i];
			if (csum_flag &&
			    ext2fs_bg_flags_test(fs, i, EXT2_BG_BLOCK_UNINIT) &&
			    ext2fs_group_desc_csum_verify(fs, i))
				blk = 0;
			if (blk) {
				retval = io_channel_read_blk64(fs->io, blk,
					     -block_nbytes, exclude_bitmap);
				if (retval) {
					retval = EXT2_ET_BLOCK_BITMAP_READ;
					goto cleanup;
				}
			} else
				memset(exclude_bitmap, 0, block_nbytes);
			cnt = block_nbytes << 3;
			retval = ext2fs_set_block_bitmap_range2(fs->exclude_map,
					       blk_itr, cnt, exclude_bitmap);
			if (retval)
				goto cleanup;
		}
		if (block_nbytes)
			blk_itr += block_nbytes << 3;
#else
			blk_itr += block_nbytes << 3;
		}
#endif
		if (inode_bitmap) {
			blk = ext2fs_inode_bitmap_loc(fs, i);
			if (csum_flag &&
			    ext2fs_bg_flags_test(fs, i, EXT2_BG_INODE_UNINIT) &&
			    ext2fs_group_desc_csum_verify(fs, i))
				blk = 0;
			if (blk) {
				retval = io_channel_read_blk64(fs->io, blk,
					     -inode_nbytes, inode_bitmap);
				if (retval) {
					retval = EXT2_ET_INODE_BITMAP_READ;
					goto cleanup;
				}
			} else
				memset(inode_bitmap, 0, inode_nbytes);
			cnt = inode_nbytes << 3;
			retval = ext2fs_set_inode_bitmap_range(fs->inode_map,
					       ino_itr, cnt, inode_bitmap);
			if (retval)
				goto cleanup;
			ino_itr += inode_nbytes << 3;
		}
	}
success_cleanup:
	if (inode_bitmap)
		ext2fs_free_mem(&inode_bitmap);
	if (block_bitmap)
		ext2fs_free_mem(&block_bitmap);
#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
	if (exclude_bitmap)
		ext2fs_free_mem(&exclude_bitmap);
#endif
	return 0;

cleanup:
	if (do_block) {
		ext2fs_free_mem(&fs->block_map);
		fs->block_map = 0;
	}
	if (do_inode) {
		ext2fs_free_mem(&fs->inode_map);
		fs->inode_map = 0;
	}
	if (inode_bitmap)
		ext2fs_free_mem(&inode_bitmap);
	if (block_bitmap)
		ext2fs_free_mem(&block_bitmap);
#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
	if (exclude_bitmap)
		ext2fs_free_mem(&exclude_bitmap);
#endif
	if (buf)
		ext2fs_free_mem(&buf);
	return retval;
}

errcode_t ext2fs_read_inode_bitmap(ext2_filsys fs)
{
#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
	return read_bitmaps(fs, 1, 0, 0);
#else
	return read_bitmaps(fs, 1, 0);
#endif
}

errcode_t ext2fs_read_block_bitmap(ext2_filsys fs)
{
#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
	return read_bitmaps(fs, 0, 1, 0);
#else
	return read_bitmaps(fs, 0, 1);
#endif
}

#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
errcode_t ext2fs_read_exclude_bitmap (ext2_filsys fs)
{
	return read_bitmaps(fs, 0, 0, 1);
}

#endif
errcode_t ext2fs_write_inode_bitmap(ext2_filsys fs)
{
#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
	return write_bitmaps(fs, 1, 0, 0);
#else
	return write_bitmaps(fs, 1, 0);
#endif
}

errcode_t ext2fs_write_block_bitmap (ext2_filsys fs)
{
#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
	return write_bitmaps(fs, 0, 1, 0);
#else
	return write_bitmaps(fs, 0, 1);
#endif
}

#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
errcode_t ext2fs_write_exclude_bitmap (ext2_filsys fs)
{
	return write_bitmaps(fs, 0, 0, 1);
}

#endif
errcode_t ext2fs_read_bitmaps(ext2_filsys fs)
{
#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
	if (fs->inode_map && fs->block_map && fs->exclude_map)
#else
	if (fs->inode_map && fs->block_map)
#endif
		return 0;

#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
	return read_bitmaps(fs, !fs->inode_map, !fs->block_map, !fs->exclude_map);
#else
	return read_bitmaps(fs, !fs->inode_map, !fs->block_map);
#endif
}

errcode_t ext2fs_write_bitmaps(ext2_filsys fs)
{
	int do_inode = fs->inode_map && ext2fs_test_ib_dirty(fs);
	int do_block = fs->block_map && ext2fs_test_bb_dirty(fs);
#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
	int do_exclude = fs->exclude_map && ext2fs_test_exclude_dirty(fs);
#endif

#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
	if (!do_inode && !do_block && !do_exclude)
#else
	if (!do_inode && !do_block)
#endif
		return 0;

#ifdef CONFIG_NEXT3_FS_SNAPSHOT_EXCLUDE_BITMAP
	return write_bitmaps(fs, do_inode, do_block, do_exclude);
#else
	return write_bitmaps(fs, do_inode, do_block);
#endif
}
