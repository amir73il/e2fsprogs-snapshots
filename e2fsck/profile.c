/*
 * profile.c -- A simple configuration file parsing "library in a file"
 * 
 * The profile library was originally written by Theodore Ts'o in 1995
 * for use in the MIT Kerberos v5 library.  It has been
 * modified/enhanced/bug-fixed over time by other members of the MIT
 * Kerberos team.  This version was originally taken from the Kerberos
 * v5 distribution, version 1.4.2, and radically simplified for use in
 * e2fsprogs.  (Support for locking for multi-threaded operations,
 * being able to modify and update the configuration file
 * programmatically, and Mac/Windows portability have been removed.
 * It has been folded into a single C source file to make it easier to
 * fold into an application program.)
 *
 * Copyright (C) 2005 by Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 * 
 * Copyright (C) 1985-2005 by the Massachusetts Institute of Technology.
 * 
 * All rights reserved.
 * 
 * Export of this software from the United States of America may require
 * a specific license from the United States Government.  It is the
 * responsibility of any person or organization contemplating export to
 * obtain such a license before exporting.
 * 
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original MIT software.
 * M.I.T. makes no representations about the suitability of this software
 * for any purpose.  It is provided "as is" without express or implied
 * warranty.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 * 
 */

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <time.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif

#include <et/com_err.h>
#include "profile.h"
#include "prof_err.h"

#undef STAT_ONCE_PER_SECOND
#undef HAVE_STAT

/* Begin prof_int.h */

/*
 * prof_int.h
 */

typedef long prf_magic_t;

/*
 * This is the structure which stores the profile information for a
 * particular configuration file.
 */
struct _prf_data_t {
	prf_magic_t	magic;
	struct profile_node *root;
#ifdef STAT_ONCE_PER_SECOND
	time_t		last_stat;
#endif
	time_t		timestamp; /* time tree was last updated from file */
	int		flags;	/* r/w, dirty */
	int		upd_serial; /* incremented when data changes */
	char		*comment;

	size_t		fslen;

	struct _prf_data_t *next;
	/* Was: "profile_filespec_t filespec".  Now: flexible char
	   array ... except, we need to work in C89, so an array
	   length must be specified.  */
	const char	filespec[sizeof("/etc/krb5.conf")];
};

typedef struct _prf_data_t *prf_data_t;
static prf_data_t profile_make_prf_data(const char *);

struct _prf_file_t {
	prf_magic_t	magic;
	struct _prf_data_t	*data;
	struct _prf_file_t *next;
};

typedef struct _prf_file_t *prf_file_t;

/*
 * The profile flags
 */
#define PROFILE_FILE_RW		0x0001
#define PROFILE_FILE_DIRTY	0x0002

/*
 * This structure defines the high-level, user visible profile_t
 * object, which is used as a handle by users who need to query some
 * configuration file(s)
 */
struct _profile_t {
	prf_magic_t	magic;
	prf_file_t	first_file;
};

/*
 * Used by the profile iterator in prof_get.c
 */
#define PROFILE_ITER_LIST_SECTION	0x0001
#define PROFILE_ITER_SECTIONS_ONLY	0x0002
#define PROFILE_ITER_RELATIONS_ONLY	0x0004

#define PROFILE_ITER_FINAL_SEEN		0x0100

/*
 * Check if a filespec is last in a list (NULL on UNIX, invalid FSSpec on MacOS
 */

#define	PROFILE_LAST_FILESPEC(x) (((x) == NULL) || ((x)[0] == '\0'))

struct profile_node {
	errcode_t	magic;
	char *name;
	char *value;
	int group_level;
	int final:1;		/* Indicate don't search next file */
	int deleted:1;
	struct profile_node *first_child;
	struct profile_node *parent;
	struct profile_node *next, *prev;
};

#define CHECK_MAGIC(node) \
	  if ((node)->magic != PROF_MAGIC_NODE) \
		  return PROF_MAGIC_NODE;

/* profile_parse.c */

static errcode_t profile_parse_file
	(FILE *f, struct profile_node **root);

#ifdef DEBUG_PROGRAM
static errcode_t profile_write_tree_file
	(struct profile_node *root, FILE *dstfile);

static errcode_t profile_write_tree_to_buffer
	(struct profile_node *root, char **buf);
#endif


/* prof_tree.c */

static void profile_free_node
	(struct profile_node *relation);

static errcode_t profile_create_node
	(const char *name, const char *value,
		   struct profile_node **ret_node);

#ifdef DEBUG_PROGRAM
static errcode_t profile_verify_node
	(struct profile_node *node);
#endif

static errcode_t profile_add_node
	(struct profile_node *section,
		    const char *name, const char *value,
		    struct profile_node **ret_node);

static errcode_t profile_find_node
	(struct profile_node *section,
		    const char *name, const char *value,
		    int section_flag, void **state,
		    struct profile_node **node);

static errcode_t profile_node_iterator
	(void	**iter_p, struct profile_node **ret_node,
		   char **ret_name, char **ret_value);

/* prof_file.c */

static errcode_t profile_open_file
	(const char * file, prf_file_t *ret_prof);

#define profile_update_file(P) profile_update_file_data((P)->data)
static errcode_t profile_update_file_data
	(prf_data_t profile);

static void profile_free_file
	(prf_file_t profile);

/* prof_init.c -- included from profile.h */

/* prof_get.c */

static errcode_t profile_get_value(profile_t profile, const char *name,
				   const char *subname, const char *subsubname,
				   const char **ret_value);

/* Others included from profile.h */
	
/* prof_set.c -- included from profile.h */

/* End prof_int.h */

/* Begin prof_init.c */
/*
 * prof_init.c --- routines that manipulate the user-visible profile_t
 * 	object.
 */

errcode_t 
profile_init(const char **files, profile_t *ret_profile)
{
	const char **fs;
	profile_t profile;
	prf_file_t  new_file, last = 0;
	errcode_t retval = 0;

	profile = malloc(sizeof(struct _profile_t));
	if (!profile)
		return ENOMEM;
	memset(profile, 0, sizeof(struct _profile_t));
	profile->magic = PROF_MAGIC_PROFILE;

        /* if the filenames list is not specified return an empty profile */
        if ( files ) {
	    for (fs = files; !PROFILE_LAST_FILESPEC(*fs); fs++) {
		retval = profile_open_file(*fs, &new_file);
		/* if this file is missing, skip to the next */
		if (retval == ENOENT || retval == EACCES) {
			continue;
		}
		if (retval) {
			profile_release(profile);
			return retval;
		}
		if (last)
			last->next = new_file;
		else
			profile->first_file = new_file;
		last = new_file;
	    }
	    /*
	     * If last is still null after the loop, then all the files were
	     * missing, so return the appropriate error.
	     */
	    if (!last) {
		profile_release(profile);
		return ENOENT;
	    }
	}

        *ret_profile = profile;
        return 0;
}

void 
profile_release(profile_t profile)
{
	prf_file_t	p, next;

	if (!profile || profile->magic != PROF_MAGIC_PROFILE)
		return;

	for (p = profile->first_file; p; p = next) {
		next = p->next;
		profile_free_file(p);
	}
	profile->magic = 0;
	free(profile);
}


/* End prof_init.c */

/* Begin prof_file.c */
/*
 * prof_file.c ---- routines that manipulate an individual profile file.
 */

static prf_data_t
profile_make_prf_data(const char *filename)
{
    prf_data_t d;
    size_t len, flen, slen;
    char *fcopy;

    flen = strlen(filename);
    slen = offsetof(struct _prf_data_t, filespec);
    len = slen + flen + 1;
    if (len < sizeof(struct _prf_data_t))
	len = sizeof(struct _prf_data_t);
    d = malloc(len);
    if (d == NULL)
	return NULL;
    memset(d, 0, len);
    fcopy = (char *) d + slen;
    strcpy(fcopy, filename);
    d->comment = NULL;
    d->magic = PROF_MAGIC_FILE_DATA;
    d->root = NULL;
    d->next = NULL;
    d->fslen = flen;
    return d;
}

errcode_t profile_open_file(const char * filespec,
			    prf_file_t *ret_prof)
{
	prf_file_t	prf;
	errcode_t	retval;
	char		*home_env = 0;
	unsigned int	len;
	prf_data_t	data;
	char		*expanded_filename;

	prf = malloc(sizeof(struct _prf_file_t));
	if (!prf)
		return ENOMEM;
	memset(prf, 0, sizeof(struct _prf_file_t));
	prf->magic = PROF_MAGIC_FILE;

	len = strlen(filespec)+1;
	if (filespec[0] == '~' && filespec[1] == '/') {
		home_env = getenv("HOME");
#ifdef HAVE_PWD_H
		if (home_env == NULL) {
		    uid_t uid;
		    struct passwd *pw, pwx;
		    char pwbuf[BUFSIZ];

		    uid = getuid();
		    if (!k5_getpwuid_r(uid, &pwx, pwbuf, sizeof(pwbuf), &pw)
			&& pw != NULL && pw->pw_dir[0] != 0)
			home_env = pw->pw_dir;
		}
#endif
		if (home_env)
			len += strlen(home_env);
	}
	expanded_filename = malloc(len);
	if (expanded_filename == 0)
	    return errno;
	if (home_env) {
	    strcpy(expanded_filename, home_env);
	    strcat(expanded_filename, filespec+1);
	} else
	    memcpy(expanded_filename, filespec, len);

	data = profile_make_prf_data(expanded_filename);
	if (data == NULL) {
	    free(prf);
	    free(expanded_filename);
	    return ENOMEM;
	}
	free(expanded_filename);
	prf->data = data;

	retval = profile_update_file(prf);
	if (retval) {
		profile_free_file(prf);
		return retval;
	}

	*ret_prof = prf;
	return 0;
}

errcode_t profile_update_file_data(prf_data_t data)
{
	errcode_t retval;
#ifdef HAVE_STAT
	struct stat st;
#ifdef STAT_ONCE_PER_SECOND
	time_t now;
#endif
#endif
	FILE *f;

#ifdef HAVE_STAT
#ifdef STAT_ONCE_PER_SECOND
	now = time(0);
	if (now == data->last_stat && data->root != NULL) {
	    return 0;
	}
#endif
	if (stat(data->filespec, &st)) {
	    retval = errno;
	    return retval;
	}
#ifdef STAT_ONCE_PER_SECOND
	data->last_stat = now;
#endif
	if (st.st_mtime == data->timestamp && data->root != NULL) {
	    return 0;
	}
	if (data->root) {
		profile_free_node(data->root);
		data->root = 0;
	}
	if (data->comment) {
		free(data->comment);
		data->comment = 0;
	}
#else
	/*
	 * If we don't have the stat() call, assume that our in-core
	 * memory image is correct.  That is, we won't reread the
	 * profile file if it changes.
	 */
	if (data->root) {
	    return 0;
	}
#endif
	errno = 0;
	f = fopen(data->filespec, "r");
	if (f == NULL) {
		retval = errno;
		if (retval == 0)
			retval = ENOENT;
		return retval;
	}
	data->upd_serial++;
	retval = profile_parse_file(f, &data->root);
	fclose(f);
	if (retval) {
	    return retval;
	}
#ifdef HAVE_STAT
	data->timestamp = st.st_mtime;
#endif
	return 0;
}

void profile_free_file(prf_file_t prf)
{
    prf_data_t data = prf->data;

    if (data->root)
	profile_free_node(data->root);
    if (data->comment)
	free(data->comment);
    data->magic = 0;
    free(data);
    free(prf);
}

/* End prof_file.c */

/* Begin prof_parse.c */

#define SECTION_SEP_CHAR '/'

#define STATE_INIT_COMMENT	1
#define STATE_STD_LINE		2
#define STATE_GET_OBRACE	3

struct parse_state {
	int	state;
	int	group_level;
	struct profile_node *root_section;
	struct profile_node *current_section;
};

static char *skip_over_blanks(char *cp)
{
	while (*cp && isspace((int) (*cp)))
		cp++;
	return cp;
}

static void strip_line(char *line)
{
	char *p = line + strlen(line);
	while (p > line && (p[-1] == '\n' || p[-1] == '\r'))
	    *p-- = 0;
}

static void parse_quoted_string(char *str)
{
	char *to, *from;

	to = from = str;

	for (to = from = str; *from && *from != '"'; to++, from++) {
		if (*from == '\\') {
			from++;
			switch (*from) {
			case 'n':
				*to = '\n';
				break;
			case 't':
				*to = '\t';
				break;
			case 'b':
				*to = '\b';
				break;
			default:
				*to = *from;
			}
			continue;
		}
		*to = *from;
	}
	*to = '\0';
}


static errcode_t parse_init_state(struct parse_state *state)
{
	state->state = STATE_INIT_COMMENT;
	state->group_level = 0;

	return profile_create_node("(root)", 0, &state->root_section);
}

static errcode_t parse_std_line(char *line, struct parse_state *state)
{
	char	*cp, ch, *tag, *value;
	char	*p;
	errcode_t retval;
	struct profile_node	*node;
	int do_subsection = 0;
	void *iter = 0;
	
	if (*line == 0)
		return 0;
	if (line[0] == ';' || line[0] == '#')
		return 0;
	strip_line(line);
	cp = skip_over_blanks(line);
	ch = *cp;
	if (ch == 0)
		return 0;
	if (ch == '[') {
		if (state->group_level > 0)
			return PROF_SECTION_NOTOP;
		cp++;
		p = strchr(cp, ']');
		if (p == NULL)
			return PROF_SECTION_SYNTAX;
		*p = '\0';
		retval = profile_find_node(state->root_section, cp, 0, 1, 
					   &iter, &state->current_section);
		if (retval == PROF_NO_SECTION) {
			retval = profile_add_node(state->root_section,
						  cp, 0,
						  &state->current_section);
			if (retval)
				return retval;
		} else if (retval)
			return retval;

		/*
		 * Finish off the rest of the line.
		 */
		cp = p+1;
		if (*cp == '*') {
			state->current_section->final = 1;
			cp++;
		}
		/*
		 * A space after ']' should not be fatal 
		 */
		cp = skip_over_blanks(cp);
		if (*cp)
			return PROF_SECTION_SYNTAX;
		return 0;
	}
	if (ch == '}') {
		if (state->group_level == 0)
			return PROF_EXTRA_CBRACE;
		if (*(cp+1) == '*')
			state->current_section->final = 1;
		state->current_section = state->current_section->parent;
		state->group_level--;
		return 0;
	}
	/*
	 * Parse the relations
	 */
	tag = cp;
	cp = strchr(cp, '=');
	if (!cp)
		return PROF_RELATION_SYNTAX;
	if (cp == tag)
	    return PROF_RELATION_SYNTAX;
	*cp = '\0';
	p = tag;
	/* Look for whitespace on left-hand side.  */
	while (p < cp && !isspace((int)*p))
	    p++;
	if (p < cp) {
	    /* Found some sort of whitespace.  */
	    *p++ = 0;
	    /* If we have more non-whitespace, it's an error.  */
	    while (p < cp) {
		if (!isspace((int)*p))
		    return PROF_RELATION_SYNTAX;
		p++;
	    }
	}
	cp = skip_over_blanks(cp+1);
	value = cp;
	if (value[0] == '"') {
		value++;
		parse_quoted_string(value);
	} else if (value[0] == 0) {
		do_subsection++;
		state->state = STATE_GET_OBRACE;
	} else if (value[0] == '{' && *(skip_over_blanks(value+1)) == 0)
		do_subsection++;
	else {
		cp = value + strlen(value) - 1;
		while ((cp > value) && isspace((int) (*cp)))
			*cp-- = 0;
	}
	if (do_subsection) {
		p = strchr(tag, '*');
		if (p)
			*p = '\0';
		retval = profile_add_node(state->current_section,
					  tag, 0, &state->current_section);
		if (retval)
			return retval;
		if (p)
			state->current_section->final = 1;
		state->group_level++;
		return 0;
	}
	p = strchr(tag, '*');
	if (p)
		*p = '\0';
	profile_add_node(state->current_section, tag, value, &node);
	if (p)
		node->final = 1;
	return 0;
}

static errcode_t parse_line(char *line, struct parse_state *state)
{
	char	*cp;
	
	switch (state->state) {
	case STATE_INIT_COMMENT:
		if (line[0] != '[')
			return 0;
		state->state = STATE_STD_LINE;
	case STATE_STD_LINE:
		return parse_std_line(line, state);
	case STATE_GET_OBRACE:
		cp = skip_over_blanks(line);
		if (*cp != '{')
			return PROF_MISSING_OBRACE;
		state->state = STATE_STD_LINE;
	}
	return 0;
}

errcode_t profile_parse_file(FILE *f, struct profile_node **root)
{
#define BUF_SIZE	2048
	char *bptr;
	errcode_t retval;
	struct parse_state state;

	bptr = malloc (BUF_SIZE);
	if (!bptr)
		return ENOMEM;

	retval = parse_init_state(&state);
	if (retval) {
		free (bptr);
		return retval;
	}
	while (!feof(f)) {
		if (fgets(bptr, BUF_SIZE, f) == NULL)
			break;
#ifndef PROFILE_SUPPORTS_FOREIGN_NEWLINES
		retval = parse_line(bptr, &state);
		if (retval) {
			free (bptr);
			return retval;
		}
#else
		{
		    char *p, *end;

		    if (strlen(bptr) >= BUF_SIZE - 1) {
			/* The string may have foreign newlines and
			   gotten chopped off on a non-newline
			   boundary.  Seek backwards to the last known
			   newline.  */
			long offset;
			char *c = bptr + strlen (bptr);
			for (offset = 0; offset > -BUF_SIZE; offset--) {
			    if (*c == '\r' || *c == '\n') {
				*c = '\0';
				fseek (f, offset, SEEK_CUR);
				break;
			    }
			    c--;
			}
		    }

		    /* First change all newlines to \n */
		    for (p = bptr; *p != '\0'; p++) {
			if (*p == '\r')
                            *p = '\n';
		    }
		    /* Then parse all lines */
		    p = bptr;
		    end = bptr + strlen (bptr);
		    while (p < end) {
			char* newline;
			char* newp;

			newline = strchr (p, '\n');
			if (newline != NULL)
			    *newline = '\0';

			/* parse_line modifies contents of p */
			newp = p + strlen (p) + 1;
			retval = parse_line (p, &state);
			if (retval) {
			    free (bptr);
			    return retval;
			}

			p = newp;
		    }
		}
#endif
	}
	*root = state.root_section;

	free (bptr);
	return 0;
}

#ifdef DEBUG_PROGRAM
/*
 * Return TRUE if the string begins or ends with whitespace
 */
static int need_double_quotes(char *str)
{
	if (!str || !*str)
		return 0;
	if (isspace((int) (*str)) ||isspace((int) (*(str + strlen(str) - 1))))
		return 1;
	if (strchr(str, '\n') || strchr(str, '\t') || strchr(str, '\b'))
		return 1;
	return 0;
}

/*
 * Output a string with double quotes, doing appropriate backquoting
 * of characters as necessary.
 */
static void output_quoted_string(char *str, void (*cb)(const char *,void *),
				 void *data)
{
	char	ch;
	char buf[2];

	cb("\"", data);
	if (!str) {
		cb("\"", data);
		return;
	}
	buf[1] = 0;
	while ((ch = *str++)) {
		switch (ch) {
		case '\\':
			cb("\\\\", data);
			break;
		case '\n':
			cb("\\n", data);
			break;
		case '\t':
			cb("\\t", data);
			break;
		case '\b':
			cb("\\b", data);
			break;
		default:
			/* This would be a lot faster if we scanned
			   forward for the next "interesting"
			   character.  */
			buf[0] = ch;
			cb(buf, data);
			break;
		}
	}
	cb("\"", data);
}

#ifndef EOL
#define EOL "\n"
#endif

/* Errors should be returned, not ignored!  */
static void dump_profile(struct profile_node *root, int level,
			 void (*cb)(const char *, void *), void *data)
{
	int i;
	struct profile_node *p;
	void *iter;
	long retval;
	
	iter = 0;
	do {
		retval = profile_find_node(root, 0, 0, 0, &iter, &p);
		if (retval)
			break;
		for (i=0; i < level; i++)
			cb("\t", data);
		if (need_double_quotes(p->value)) {
			cb(p->name, data);
			cb(" = ", data);
			output_quoted_string(p->value, cb, data);
			cb(EOL, data);
		} else {
			cb(p->name, data);
			cb(" = ", data);
			cb(p->value, data);
			cb(EOL, data);
		}
	} while (iter != 0);

	iter = 0;
	do {
		retval = profile_find_node(root, 0, 0, 1, &iter, &p);
		if (retval)
			break;
		if (level == 0)	{ /* [xxx] */
			cb("[", data);
			cb(p->name, data);
			cb("]", data);
			cb(p->final ? "*" : "", data);
			cb(EOL, data);
			dump_profile(p, level+1, cb, data);
			cb(EOL, data);
		} else { 	/* xxx = { ... } */
			for (i=0; i < level; i++)
				cb("\t", data);
			cb(p->name, data);
			cb(" = {", data);
			cb(EOL, data);
			dump_profile(p, level+1, cb, data);
			for (i=0; i < level; i++)
				cb("\t", data);
			cb("}", data);
			cb(p->final ? "*" : "", data);
			cb(EOL, data);
		}
	} while (iter != 0);
}

static void dump_profile_to_file_cb(const char *str, void *data)
{
	fputs(str, data);
}

errcode_t profile_write_tree_file(struct profile_node *root, FILE *dstfile)
{
	dump_profile(root, 0, dump_profile_to_file_cb, dstfile);
	return 0;
}

struct prof_buf {
	char *base;
	size_t cur, max;
	int err;
};

static void add_data_to_buffer(struct prof_buf *b, const void *d, size_t len)
{
	if (b->err)
		return;
	if (b->max - b->cur < len) {
		size_t newsize;
		char *newptr;

		newsize = b->max + (b->max >> 1) + len + 1024;
		newptr = realloc(b->base, newsize);
		if (newptr == NULL) {
			b->err = 1;
			return;
		}
		b->base = newptr;
		b->max = newsize;
	}
	memcpy(b->base + b->cur, d, len);
	b->cur += len; 		/* ignore overflow */
}

static void dump_profile_to_buffer_cb(const char *str, void *data)
{
	add_data_to_buffer((struct prof_buf *)data, str, strlen(str));
}

errcode_t profile_write_tree_to_buffer(struct profile_node *root,
				       char **buf)
{
	struct prof_buf prof_buf = { 0, 0, 0, 0 };

	dump_profile(root, 0, dump_profile_to_buffer_cb, &prof_buf);
	if (prof_buf.err) {
		*buf = NULL;
		return ENOMEM;
	}
	add_data_to_buffer(&prof_buf, "", 1); /* append nul */
	if (prof_buf.max - prof_buf.cur > (prof_buf.max >> 3)) {
		char *newptr = realloc(prof_buf.base, prof_buf.cur);
		if (newptr)
			prof_buf.base = newptr;
	}
	*buf = prof_buf.base;
	return 0;
}
#endif

/* End prof_parse.c */

/* Begin prof_tree.c */

/*
 * prof_tree.c --- these routines maintain the parse tree of the
 * 	config file.
 * 
 * All of the details of how the tree is stored is abstracted away in
 * this file; all of the other profile routines build, access, and
 * modify the tree via the accessor functions found in this file.
 *
 * Each node may represent either a relation or a section header.
 * 
 * A section header must have its value field set to 0, and may a one
 * or more child nodes, pointed to by first_child.
 * 
 * A relation has as its value a pointer to allocated memory
 * containing a string.  Its first_child pointer must be null.
 *
 */

/*
 * Free a node, and any children
 */
void profile_free_node(struct profile_node *node)
{
	struct profile_node *child, *next;

	if (node->magic != PROF_MAGIC_NODE)
		return;
	
	if (node->name)
		free(node->name);
	if (node->value)
		free(node->value);

	for (child=node->first_child; child; child = next) {
		next = child->next;
		profile_free_node(child);
	}
	node->magic = 0;
	
	free(node);
}

#ifndef HAVE_STRDUP
#undef strdup
#define strdup MYstrdup
static char *MYstrdup (const char *s)
{
    size_t sz = strlen(s) + 1;
    char *p = malloc(sz);
    if (p != 0)
	memcpy(p, s, sz);
    return p;
}
#endif

/*
 * Create a node
 */
errcode_t profile_create_node(const char *name, const char *value,
			      struct profile_node **ret_node)
{
	struct profile_node *new;

	new = malloc(sizeof(struct profile_node));
	if (!new)
		return ENOMEM;
	memset(new, 0, sizeof(struct profile_node));
	new->name = strdup(name);
	if (new->name == 0) {
	    profile_free_node(new);
	    return ENOMEM;
	}
	if (value) {
		new->value = strdup(value);
		if (new->value == 0) {
		    profile_free_node(new);
		    return ENOMEM;
		}
	}
	new->magic = PROF_MAGIC_NODE;

	*ret_node = new;
	return 0;
}

/*
 * This function verifies that all of the representation invarients of
 * the profile are true.  If not, we have a programming bug somewhere,
 * probably in this file.
 */
#ifdef DEBUG_PROGRAM
errcode_t profile_verify_node(struct profile_node *node)
{
	struct profile_node *p, *last;
	errcode_t	retval;

	CHECK_MAGIC(node);

	if (node->value && node->first_child)
		return PROF_SECTION_WITH_VALUE;

	last = 0;
	for (p = node->first_child; p; last = p, p = p->next) {
		if (p->prev != last)
			return PROF_BAD_LINK_LIST;
		if (last && (last->next != p))
			return PROF_BAD_LINK_LIST;
		if (node->group_level+1 != p->group_level)
			return PROF_BAD_GROUP_LVL;
		if (p->parent != node)
			return PROF_BAD_PARENT_PTR;
		retval = profile_verify_node(p);
		if (retval)
			return retval;
	}
	return 0;
}
#endif

/*
 * Add a node to a particular section
 */
errcode_t profile_add_node(struct profile_node *section, const char *name,
			   const char *value, struct profile_node **ret_node)
{
	errcode_t retval;
	struct profile_node *p, *last, *new;

	CHECK_MAGIC(section);

	if (section->value)
		return PROF_ADD_NOT_SECTION;

	/*
	 * Find the place to insert the new node.  We look for the
	 * place *after* the last match of the node name, since 
	 * order matters.
	 */
	for (p=section->first_child, last = 0; p; last = p, p = p->next) {
		int cmp;
		cmp = strcmp(p->name, name);
		if (cmp > 0)
			break;
	}
	retval = profile_create_node(name, value, &new);
	if (retval)
		return retval;
	new->group_level = section->group_level+1;
	new->deleted = 0;
	new->parent = section;
	new->prev = last;
	new->next = p;
	if (p)
		p->prev = new;
	if (last)
		last->next = new;
	else
		section->first_child = new;
	if (ret_node)
		*ret_node = new;
	return 0;
}

/*
 * Iterate through the section, returning the nodes which match
 * the given name.  If name is NULL, then interate through all the
 * nodes in the section.  If section_flag is non-zero, only return the
 * section which matches the name; don't return relations.  If value
 * is non-NULL, then only return relations which match the requested
 * value.  (The value argument is ignored if section_flag is non-zero.)
 * 
 * The first time this routine is called, the state pointer must be
 * null.  When this profile_find_node_relation() returns, if the state
 * pointer is non-NULL, then this routine should be called again.
 * (This won't happen if section_flag is non-zero, obviously.)
 *
 */
errcode_t profile_find_node(struct profile_node *section, const char *name,
			    const char *value, int section_flag, void **state,
			    struct profile_node **node)
{
	struct profile_node *p;

	CHECK_MAGIC(section);
	p = *state;
	if (p) {
		CHECK_MAGIC(p);
	} else
		p = section->first_child;
	
	for (; p; p = p->next) {
		if (name && (strcmp(p->name, name)))
			continue;
		if (section_flag) {
			if (p->value)
				continue;
		} else {
			if (!p->value)
				continue;
			if (value && (strcmp(p->value, value)))
				continue;
		}
		if (p->deleted)
		    continue;
		/* A match! */
		if (node)
			*node = p;
		break;
	}
	if (p == 0) {
		*state = 0;
		return section_flag ? PROF_NO_SECTION : PROF_NO_RELATION;
	}
	/*
	 * OK, we've found one match; now let's try to find another
	 * one.  This way, if we return a non-zero state pointer,
	 * there's guaranteed to be another match that's returned.
	 */
	for (p = p->next; p; p = p->next) {
		if (name && (strcmp(p->name, name)))
			continue;
		if (section_flag) {
			if (p->value)
				continue;
		} else {
			if (!p->value)
				continue;
			if (value && (strcmp(p->value, value)))
				continue;
		}
		/* A match! */
		break;
	}
	*state = p;
	return 0;
}

/*
 * This is a general-purpose iterator for returning all nodes that
 * match the specified name array.  
 */
struct profile_iterator {
	prf_magic_t		magic;
	profile_t		profile;
	int			flags;
	const char 		*const *names;
	const char		*name;
	prf_file_t		file;
	int			file_serial;
	int			done_idx;
	struct profile_node 	*node;
	int			num;
};

errcode_t 
profile_iterator_create(profile_t profile, const char *const *names, int flags,
			void **ret_iter)
{
	struct profile_iterator *iter;
	int	done_idx = 0;

	if (profile == 0)
		return PROF_NO_PROFILE;
	if (profile->magic != PROF_MAGIC_PROFILE)
		return PROF_MAGIC_PROFILE;
	if (!names)
		return PROF_BAD_NAMESET;
	if (!(flags & PROFILE_ITER_LIST_SECTION)) {
		if (!names[0])
			return PROF_BAD_NAMESET;
		done_idx = 1;
	}

	if ((iter = malloc(sizeof(struct profile_iterator))) == NULL)
		return ENOMEM;

	iter->magic = PROF_MAGIC_ITERATOR;
	iter->profile = profile;
	iter->names = names;
	iter->flags = flags;
	iter->file = profile->first_file;
	iter->done_idx = done_idx;
	iter->node = 0;
	iter->num = 0;
	*ret_iter = iter;
	return 0;
}

void profile_iterator_free(void **iter_p)
{
	struct profile_iterator *iter;

	if (!iter_p)
		return;
	iter = *iter_p;
	if (!iter || iter->magic != PROF_MAGIC_ITERATOR)
		return;
	free(iter);
	*iter_p = 0;
}

/*
 * Note: the returned character strings in ret_name and ret_value
 * points to the stored character string in the parse string.  Before
 * this string value is returned to a calling application
 * (profile_node_iterator is not an exported interface), it should be
 * strdup()'ed.
 */
errcode_t profile_node_iterator(void **iter_p, struct profile_node **ret_node,
				char **ret_name, char **ret_value)
{
	struct profile_iterator 	*iter = *iter_p;
	struct profile_node 		*section, *p;
	const char			*const *cpp;
	errcode_t			retval;
	int				skip_num = 0;

	if (!iter || iter->magic != PROF_MAGIC_ITERATOR)
		return PROF_MAGIC_ITERATOR;
	if (iter->file && iter->file->magic != PROF_MAGIC_FILE)
	    return PROF_MAGIC_FILE;
	if (iter->file && iter->file->data->magic != PROF_MAGIC_FILE_DATA)
	    return PROF_MAGIC_FILE_DATA;
	/*
	 * If the file has changed, then the node pointer is invalid,
	 * so we'll have search the file again looking for it.
	 */
	if (iter->node && (iter->file->data->upd_serial != iter->file_serial)) {
		iter->flags &= ~PROFILE_ITER_FINAL_SEEN;
		skip_num = iter->num;
		iter->node = 0;
	}
	if (iter->node && iter->node->magic != PROF_MAGIC_NODE) {
	    return PROF_MAGIC_NODE;
	}
get_new_file:
	if (iter->node == 0) {
		if (iter->file == 0 ||
		    (iter->flags & PROFILE_ITER_FINAL_SEEN)) {
			profile_iterator_free(iter_p);
			if (ret_node)
				*ret_node = 0;
			if (ret_name)
				*ret_name = 0;
			if (ret_value)
				*ret_value =0;
			return 0;
		}
		if ((retval = profile_update_file(iter->file))) {
		    if (retval == ENOENT || retval == EACCES) {
			/* XXX memory leak? */
			iter->file = iter->file->next;
			skip_num = 0;
			retval = 0;
			goto get_new_file;
		    } else {
			profile_iterator_free(iter_p);
			return retval;
		    }
		}
		iter->file_serial = iter->file->data->upd_serial;
		/*
		 * Find the section to list if we are a LIST_SECTION,
		 * or find the containing section if not.
		 */
		section = iter->file->data->root;
		for (cpp = iter->names; cpp[iter->done_idx]; cpp++) {
			for (p=section->first_child; p; p = p->next) {
				if (!strcmp(p->name, *cpp) && !p->value)
					break;
			}
			if (!p) {
				section = 0;
				break;
			}
			section = p;
			if (p->final)
				iter->flags |= PROFILE_ITER_FINAL_SEEN;
		}
		if (!section) {
			iter->file = iter->file->next;
			skip_num = 0;
			goto get_new_file;
		}
		iter->name = *cpp;
		iter->node = section->first_child;
	}
	/*
	 * OK, now we know iter->node is set up correctly.  Let's do
	 * the search.
	 */
	for (p = iter->node; p; p = p->next) {
		if (iter->name && strcmp(p->name, iter->name))
			continue;
		if ((iter->flags & PROFILE_ITER_SECTIONS_ONLY) &&
		    p->value)
			continue;
		if ((iter->flags & PROFILE_ITER_RELATIONS_ONLY) &&
		    !p->value)
			continue;
		if (skip_num > 0) {
			skip_num--;
			continue;
		}
		if (p->deleted)
			continue;
		break;
	}
	iter->num++;
	if (!p) {
		iter->file = iter->file->next;
		if (iter->file) {
		}
		iter->node = 0;
		skip_num = 0;
		goto get_new_file;
	}
	if ((iter->node = p->next) == NULL)
		iter->file = iter->file->next;
	if (ret_node)
		*ret_node = p;
	if (ret_name)
		*ret_name = p->name;
	if (ret_value)
		*ret_value = p->value;
	return 0;
}



/* End prof_tree.c */


/* Begin prof_get.c */
/*
 * prof_get.c --- routines that expose the public interfaces for
 * 	querying items from the profile.
 *
 */

/*
 * This function only gets the first value from the file; it is a
 * helper function for profile_get_string, profile_get_integer, etc.
 */
errcode_t profile_get_value(profile_t profile, const char *name,
			    const char *subname, const char *subsubname,
			    const char **ret_value)
{
	errcode_t		retval;
	void			*state;
	char			*value;
	const char		*names[4];

	names[0] = name;
	names[1] = subname;
	names[2] = subsubname;
	names[3] = 0;

	if ((retval = profile_iterator_create(profile, names,
					      PROFILE_ITER_RELATIONS_ONLY,
					      &state)))
		return retval;

	if ((retval = profile_node_iterator(&state, 0, 0, &value)))
		goto cleanup;

	if (value)
		*ret_value = value;
	else
		retval = PROF_NO_RELATION;
	
cleanup:
	profile_iterator_free(&state);
	return retval;
}

errcode_t 
profile_get_string(profile_t profile, const char *name, const char *subname,
		   const char *subsubname, const char *def_val,
		   char **ret_string)
{
	const char	*value;
	errcode_t	retval;

	if (profile) {
		retval = profile_get_value(profile, name, subname, 
					   subsubname, &value);
		if (retval == PROF_NO_SECTION || retval == PROF_NO_RELATION)
			value = def_val;
		else if (retval)
			return retval;
	} else
		value = def_val;
    
	if (value) {
		*ret_string = malloc(strlen(value)+1);
		if (*ret_string == 0)
			return ENOMEM;
		strcpy(*ret_string, value);
	} else
		*ret_string = 0;
	return 0;
}

errcode_t 
profile_get_integer(profile_t profile, const char *name, const char *subname,
		    const char *subsubname, int def_val, int *ret_int)
{
	const char	*value;
	errcode_t	retval;
	char            *end_value;
	long		ret_long;

	*ret_int = def_val;
	if (profile == 0)
		return 0;

	retval = profile_get_value(profile, name, subname, subsubname, &value);
	if (retval == PROF_NO_SECTION || retval == PROF_NO_RELATION) {
		*ret_int = def_val;
		return 0;
	} else if (retval)
		return retval;

	if (value[0] == 0)
	    /* Empty string is no good.  */
	    return PROF_BAD_INTEGER;
	errno = 0;
	ret_long = strtol (value, &end_value, 10);

	/* Overflow or underflow.  */
	if ((ret_long == LONG_MIN || ret_long == LONG_MAX) && errno != 0)
	    return PROF_BAD_INTEGER;
	/* Value outside "int" range.  */
	if ((long) (int) ret_long != ret_long)
	    return PROF_BAD_INTEGER;
	/* Garbage in string.  */
	if (end_value != value + strlen (value))
	    return PROF_BAD_INTEGER;
	
   
	*ret_int = ret_long;
	return 0;
}

static const char *const conf_yes[] = {
    "y", "yes", "true", "t", "1", "on",
    0,
};

static const char *const conf_no[] = {
    "n", "no", "false", "nil", "0", "off",
    0,
};

static errcode_t
profile_parse_boolean(const char *s, int *ret_boolean)
{
    const char *const *p;
    
    if (ret_boolean == NULL)
    	return PROF_EINVAL;

    for(p=conf_yes; *p; p++) {
		if (!strcasecmp(*p,s)) {
			*ret_boolean = 1;
	    	return 0;
		}
    }

    for(p=conf_no; *p; p++) {
		if (!strcasecmp(*p,s)) {
			*ret_boolean = 0;
			return 0;
		}
    }
	
	return PROF_BAD_BOOLEAN;
}

errcode_t 
profile_get_boolean(profile_t profile, const char *name, const char *subname,
		    const char *subsubname, int def_val, int *ret_boolean)
{
	const char	*value;
	errcode_t	retval;

	if (profile == 0) {
		*ret_boolean = def_val;
		return 0;
	}

	retval = profile_get_value(profile, name, subname, subsubname, &value);
	if (retval == PROF_NO_SECTION || retval == PROF_NO_RELATION) {
		*ret_boolean = def_val;
		return 0;
	} else if (retval)
		return retval;
   
	return profile_parse_boolean (value, ret_boolean);
}

errcode_t 
profile_iterator(void **iter_p, char **ret_name, char **ret_value)
{
	char *name, *value;
	errcode_t	retval;
	
	retval = profile_node_iterator(iter_p, 0, &name, &value);
	if (retval)
		return retval;

	if (ret_name) {
		if (name) {
			*ret_name = malloc(strlen(name)+1);
			if (!*ret_name)
				return ENOMEM;
			strcpy(*ret_name, name);
		} else
			*ret_name = 0;
	}
	if (ret_value) {
		if (value) {
			*ret_value = malloc(strlen(value)+1);
			if (!*ret_value) {
				if (ret_name) {
					free(*ret_name);
					*ret_name = 0;
				}
				return ENOMEM;
			}
			strcpy(*ret_value, value);
		} else
			*ret_value = 0;
	}
	return 0;
}

/* End prof_get.c */

#ifdef DEBUG_PROGRAM

/*
 * test_profile.c --- testing program for the profile routine
 */

#include "argv_parse.h"
#include "profile_helpers.h"

const char *program_name = "test_profile";

#define PRINT_VALUE	1
#define PRINT_VALUES	2

static void do_cmd(profile_t profile, char **argv)
{
	errcode_t	retval;
	const char	**names, *value;
	char		**values, **cpp;
	char	*cmd;
	int		print_status;

	cmd = *(argv);
	names = (const char **) argv + 1;
	print_status = 0;
	retval = 0;
	if (cmd == 0)
		return;
	if (!strcmp(cmd, "query")) {
		retval = profile_get_values(profile, names, &values);
		print_status = PRINT_VALUES;
	} else if (!strcmp(cmd, "query1")) {
		const char *name = 0;
		const char *subname = 0;
		const char *subsubname = 0;

		name = names[0];
		if (name)
			subname = names[1];
		if (subname)
			subsubname = names[2];
		if (subsubname && names[3]) {
			fprintf(stderr, 
				"Only 3 levels are allowed with query1\n");
			retval = EINVAL;
		} else
			retval = profile_get_value(profile, name, subname, 
						   subsubname, &value);
		print_status = PRINT_VALUE;
	} else if (!strcmp(cmd, "list_sections")) {
		retval = profile_get_subsection_names(profile, names, 
						      &values);
		print_status = PRINT_VALUES;
	} else if (!strcmp(cmd, "list_relations")) {
		retval = profile_get_relation_names(profile, names, 
						    &values);
		print_status = PRINT_VALUES;
	} else if (!strcmp(cmd, "dump")) {
		retval = profile_write_tree_file
			(profile->first_file->data->root, stdout);
#if 0
	} else if (!strcmp(cmd, "clear")) {
		retval = profile_clear_relation(profile, names);
	} else if (!strcmp(cmd, "update")) {
		retval = profile_update_relation(profile, names+2,
						 *names, *(names+1));
#endif
	} else if (!strcmp(cmd, "verify")) {
		retval = profile_verify_node
			(profile->first_file->data->root);
#if 0
	} else if (!strcmp(cmd, "rename_section")) {
		retval = profile_rename_section(profile, names+1, *names);
	} else if (!strcmp(cmd, "add")) {
		value = *names;
		if (strcmp(value, "NULL") == 0)
			value = NULL;
		retval = profile_add_relation(profile, names+1, value);
	} else if (!strcmp(cmd, "flush")) {
		retval = profile_flush(profile);
#endif
	} else {
		printf("Invalid command.\n");
	}
	if (retval) {
		com_err(cmd, retval, "");
		print_status = 0;
	}
	switch (print_status) {
	case PRINT_VALUE:
		printf("%s\n", value);
		break;
	case PRINT_VALUES:
		for (cpp = values; *cpp; cpp++)
			printf("%s\n", *cpp);
		profile_free_list(values);
		break;
	}
}

static void do_batchmode(profile_t profile)
{
	int		argc, ret;
	char		**argv;
	char		buf[256];

	while (!feof(stdin)) {
		if (fgets(buf, sizeof(buf), stdin) == NULL)
			break;
		printf(">%s", buf);
		ret = argv_parse(buf, &argc, &argv);
		if (ret != 0) {
			printf("Argv_parse returned %d!\n", ret);
			continue;
		}
		do_cmd(profile, argv);
		printf("\n");
		argv_free(argv);
	}
	profile_release(profile);
	exit(0);
	
}


int main(int argc, char **argv)
{
    profile_t	profile;
    long	retval;
    char	*cmd;
    
    if (argc < 2) {
	    fprintf(stderr, "Usage: %s filename [cmd argset]\n", program_name);
	    exit(1);
    }

    initialize_prof_error_table();
    
    retval = profile_init_path(argv[1], &profile);
    if (retval) {
	com_err(program_name, retval, "while initializing profile");
	exit(1);
    }
    cmd = *(argv+2);
    if (!cmd || !strcmp(cmd, "batch"))
	    do_batchmode(profile);
    else
	    do_cmd(profile, argv+2);
    profile_release(profile);

    return 0;
}

#endif