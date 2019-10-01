/*
 * UNG's Not GNU
 *
 * Copyright (c) 2011-2019, Jakob Kaivo <jkk@ung.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <ftw.h>
#include <time.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <limits.h>
#include <libgen.h>

#ifndef ARG_MAX
#define ARG_MAX 512
#endif

#ifndef FTW_CONTINUE
#define FTW_CONTINUE 1
#define FTW_SKIP_SUBTREE 2
#endif

const char *ls_desc = "list directory contents";
const char *ls_inv = "ls [-ACFRSacdgfiklmnoprstux1] [-H|-L] [file...]";

#define NONE 0
#define ALL 1
#define ALMOST 2

#define ALPHA 0
#define SIZE 1
#define DIRECTORY 2
#define CTIME 3
#define MTIME 4
#define ATIME 5

#define DCOLUMNS 1
#define DLONG 2
#define COMMA 3
#define ROWS 4
#define SINGLE 5

#define CHARACTER 1 << 0
#define INODES 1 << 1
#define DIRS 1 << 2
#define QUOTE 1 << 3
#define BLOCKS 1 << 4

#define FOLLOW 1
#define LLONG 2

#define DEFAULT_BLOCK_SIZE 512

static int all = NONE;
static int display = DCOLUMNS;
static int format = NONE;
static int links = NONE;
static int sort = ALPHA;
static int recurse = 0;
static int listdirs = 0;
static int noowner = 0;
static int blocksize = DEFAULT_BLOCK_SIZE;
static int numeric = 0;
static int nogroup = 0;
static int reverse = 0;
static int totaldirs = 0;
static int columns = 80;

struct ls_entry {
	char name[PATH_MAX];
	off_t size;
	ino_t inode;
	time_t time;
	uid_t uid;
	gid_t gid;
	char owner[PATH_MAX];
	char group[PATH_MAX];
	nlink_t links;
	blkcnt_t blocks;
	mode_t mode;
	char link[PATH_MAX];
	char type;
	int isexec;
	struct ls_entry *left;
	struct ls_entry *right;
	struct ls_entry *up;
};

static struct ls_entry *root = NULL;
static int biggest = 1;
static int maxlinks = 1;
static int longestowner = 1;
static int longestgroup = 1;
static int longestname = 1;
static int sizelen = 1;
static int linklen = 1;

struct directory {
	char display[PATH_MAX];
	char path[PATH_MAX];
};

static struct directory dirlist[ARG_MAX];

int nftw_ls(const char *p, const struct stat *sb, int flag, struct FTW *f)
{
	if (flag == FTW_D) {
		strcat(dirlist[totaldirs].path, p);
		// FIXME: this is showing the wrong names
		if (f->level > 0)
			f->base--;
		while (f->level >= 0) {
			f->base--;
			if (p[f->base] == '/')
				f->level--;
		}
		strcat(dirlist[totaldirs].display, &p[f->base]);
		totaldirs++;
	}
	return 0;
}

static int ls_stat(const char *p, struct stat *st)
{
	// FIXME fracking -L and -H
	if (links == NONE)
		return lstat(p, st);
	return stat(p, st);
}

void ls_reset(void)
{
	root = NULL;
	biggest = 1;
	maxlinks = 1;
	longestowner = 1;
	longestgroup = 1;
	longestname = 1;
	sizelen = 1;
	linklen = 1;
}

static int ls_other(struct ls_entry *current, int cpos)
{
	char umode[4], gmode[4], omode[4];
	char time[BUFSIZ];

	if (current == NULL)
		return 0;

	if (current->left != NULL) {
		cpos = ls_other(current->left, cpos);
		free(current->left);
		current->left = NULL;
	}

	if (format & INODES) {
		cpos += printf("%u ", (unsigned int)current->inode);
	}

	if (display == DLONG) {
		while (biggest /= 10)
			sizelen++;
		while (maxlinks /= 10)
			linklen++;

		umode[0] = (S_IRUSR & current->mode) ? 'r' : '-';
		umode[1] = (S_IWUSR & current->mode) ? 'w' : '-';
		if (S_ISUID & current->mode)
			umode[2] = (S_IXUSR & current->mode) ? 's' : 'S';
		else
			umode[2] = (S_IXUSR & current->mode) ? 'x' : '-';
		umode[3] = '\0';
		gmode[0] = (S_IRGRP & current->mode) ? 'r' : '-';
		gmode[1] = (S_IWGRP & current->mode) ? 'w' : '-';
		if (S_ISGID & current->mode)
			gmode[2] = (S_IXGRP & current->mode) ? 's' : 'S';
		else
			gmode[2] = (S_IXGRP & current->mode) ? 'x' : '-';
		gmode[3] = '\0';
		omode[0] = (S_IROTH & current->mode) ? 'r' : '-';
		omode[1] = (S_IWOTH & current->mode) ? 'w' : '-';
		if (S_ISVTX & current->mode)
			omode[2] = (S_IXOTH & current->mode) ? 't' : 'T';
		else
			omode[2] = (S_IXOTH & current->mode) ? 'x' : '-';
		omode[3] = '\0';
		strftime(time, BUFSIZ, "%b %e %H:%M",
			 localtime(&(current->time)));
		// over six months user "%b %e %Y"

		printf("%c%s%s%s ", current->type, umode, gmode, omode);
		printf("%*u ", linklen, (unsigned int)current->links);

		if (!noowner) {
			if (numeric)
				printf("%*u ", longestowner, current->uid);
			else
				printf("%-*s ", longestowner, current->owner);
		}

		if (!nogroup) {
			if (numeric)
				printf("%*u ", longestowner, current->gid);
			else
				printf("%-*s ", longestgroup, current->group);
		}

		printf("%*u ", sizelen, (unsigned int)current->size);
		printf("%s %s", time, current->name);

		if (links != LLONG && current->type == 'l')
			printf(" -> %s", current->link);

		putchar('\n');
	} else if (display == COMMA) {
		if (cpos + strlen(current->name) + 2 > columns) {
			putchar('\n');
			cpos = 0;
		}
		cpos += printf("%s, ", current->name);
	} else if (display == SINGLE) {
		printf("%s\n", current->name);
	} else {		// if (display == ROWS) {
		cpos += printf("%-*s ", longestname, current->name);
		if (cpos + longestname + 1 > columns) {
			putchar('\n');
			cpos = 0;
		}
	}

	if (current->right != NULL) {
		cpos = ls_other(current->right, cpos);
		free(current->right);
		current->right = NULL;
	}

	return cpos;
}

static int ls_columns(struct ls_entry *current, int cpos)
{
	int cols = columns / (longestname + 1);
	int row = 0;
	// FIXME: this isn't right at all

	return ls_other(current, cpos);
}

static int ls_compare(struct ls_entry *e1, struct ls_entry *e2)
{
	if (sort == SIZE && e1->size != e2->size) {
		return (reverse ? -1 : 1) * (e2->size - e1->size);
	} else if ((sort == CTIME || sort == MTIME || sort == ATIME)
		   && e1->time != e2->time) {
		return (reverse ? -1 : 1) * (e2->time - e1->time);
	} else if (sort == DIRECTORY) {
		return (reverse ? -1 : 1);
	}
	return (reverse ? -1 : 1) * (strcmp(e1->name, e2->name));
}

static int ls_add(const char *path, int do_stat)
{
	struct stat st;
	char dname[PATH_MAX];
	char lname[PATH_MAX];
	struct ls_entry *working = malloc(sizeof(struct ls_entry));
	struct ls_entry *current;
	working->left = NULL;
	working->right = NULL;
	working->blocks = 0;

	strcpy(dname, basename((char *)path));
	memset(working->name, 0, PATH_MAX);
	memset(working->link, 0, PATH_MAX);
	memset(working->owner, 0, PATH_MAX);
	memset(working->group, 0, PATH_MAX);

	if (do_stat == 1 || sort != NONE || display == DLONG || recurse ||
	    (format & CHARACTER || format & INODES || format & DIRS
	     || format & BLOCKS)) {
		ls_stat(path, &st);

		if (S_ISDIR(st.st_mode))
			working->type = 'd';
		else if (S_ISLNK(st.st_mode))
			working->type = 'l';
		else if (S_ISBLK(st.st_mode))
			working->type = 'b';
		else if (S_ISCHR(st.st_mode))
			working->type = 'c';
		else if (S_ISFIFO(st.st_mode))
			working->type = 'p';
		else
			working->type = '-';

		working->size = st.st_size;
		working->inode = st.st_ino;
		working->links = st.st_nlink;
		working->uid = st.st_uid;
		working->gid = st.st_gid;
		working->blocks = st.st_blocks;
		working->mode = st.st_mode;

		if (sort == CTIME)
			working->time = st.st_ctime;
		else if (sort == ATIME)
			working->time = st.st_atime;
		else
			working->time = st.st_mtime;

		if (working->size > biggest)
			biggest = working->size;
		if (working->links > maxlinks)
			maxlinks = working->links;

		if (!numeric) {
			struct passwd pw = *getpwuid(working->uid);
			struct group gr = *getgrgid(working->gid);
			strcpy(working->owner, pw.pw_name);
			strcpy(working->group, gr.gr_name);
			if (strlen(working->owner) > longestowner)
				longestowner = strlen(working->owner);
			if (strlen(working->group) > longestgroup)
				longestgroup = strlen(working->group);
		} else {
			int l, n = 0;
			for (l = working->uid; l > 0; l /= 10)
				n++;
			if (n > longestowner)
				longestowner = n;
			for (l = working->gid; l > 0; l /= 10)
				n++;
			if (n > longestgroup)
				longestgroup = n;
		}

		if (working->type == '-')
			working->isexec = (access(path, X_OK) == 0 ? 1 : 0);

		if (working->type == 'd'
		    && (format & DIRS || format & CHARACTER))
			strcat(dname, "/");
		else if (format & CHARACTER) {
			if (working->type == 'l')
				strcat(dname, "@");
			else if (working->type == 'p')
				strcat(dname, "|");
			else if (working->isexec)
				strcat(dname, "*");
		}

		if (working->type == 'l') {
			// FIXME: maybe do a chdir() here so relative symlinks resolve proerly
			int n = 0;
			memset(lname, 0, PATH_MAX);
			n = readlink(path, lname, PATH_MAX);
			stat(lname, &st);
			if (S_ISDIR(st.st_mode)
			    && (format & DIRS || format & CHARACTER)) {
				lname[n] = '/';
			} else if (format & CHARACTER) {
				if (S_ISLNK(st.st_mode))
					lname[n] = '@';
				else if (S_ISFIFO(st.st_mode))
					lname[n] = '|';
				else if (access(lname, X_OK) == 0)
					lname[n] = '*';
			}
			strcpy(working->link, lname);
		}
	}

	if (recurse && working->type == 'd') {
		char fullpath[PATH_MAX];
		getcwd(fullpath, PATH_MAX);
		strcat(fullpath, "/");
		strcat(fullpath, path);
		nftw(fullpath, nftw_ls, 0, (links != NONE ? 0 : FTW_PHYS));
	}

	strcpy(working->name, dname);

	if (strlen(dname) > longestname) {
		longestname = strlen(dname);
	}

	if (root == NULL) {
		root = working;
	} else {
		current = root;
 INSERT:
		if (ls_compare(current, working) > 0) {
			if (current->left == NULL) {
				working->up = current;
				current->left = working;
			} else {
				current = current->left;
				goto INSERT;
			}
		} else {
			if (current->right == NULL) {
				working->up = current;
				current->right = working;
			} else {
				current = current->right;
				goto INSERT;
			}
		}
	}

	return working->blocks;
}

static int ls_dir(char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *de;
	int blocks = 0;
	char filename[PATH_MAX];

	while (de = readdir(d)) {
		if (de->d_name[0] != '.' || (de->d_name[0] == '.' && all == ALL)
		    || (strcmp(".", de->d_name) && strcmp("..", de->d_name)
			&& all == ALMOST)) {
			strcpy(filename, dir);
			strcat(filename, "/");
			strcat(filename, de->d_name);
			blocks += ls_add(filename, 1);
		}
	}
	closedir(d);
	return blocks * DEFAULT_BLOCK_SIZE / blocksize;
}

static void ls_print(struct ls_entry *c)
{
	if (display == DCOLUMNS) {
		if (ls_columns(c, 0) != 0)
			putchar('\n');
	} else if (ls_other(c, 0) != 0 && display != DLONG && display != SINGLE) {
		putchar('\n');
	}
}

int main(int argc, char **argv)
{
	struct stat st;
	char *files[ARG_MAX];
	int c;
	int i = 0, f = 0;
	int lastfollow = 0;
	char *cols = getenv("COLUMNS");
	if (cols != NULL)
		columns = atoi(cols);

	while ((c = getopt(argc, argv, ":ACFRSacdgfiklmnoprstux1HL")) != -1) {
		switch (c) {
		case 'A':
			all = ALMOST;
			break;
		case 'C':
			display = DCOLUMNS;
			break;
		case 'F':
			format |= CHARACTER;
			break;
		case 'H':
			if (links != NONE)
				return 1;
			links = FOLLOW;
			break;
		case 'L':
			if (links != NONE)
				return 1;
			links = LLONG;
			break;
		case 'R':
			recurse = 1;
			break;
		case 'S':
			sort = SIZE;
			break;
		case 'a':
			all = ALL;
			break;
		case 'c':
			sort = CTIME;
			break;
		case 'd':
			listdirs = 1;
			break;
		case 'f':
			sort = DIRECTORY;
			all = ALL;
			display = ROWS;
			format ^= BLOCKS;
			recurse = 0;
			break;
		case 'g':
			display = DLONG;
			noowner = 1;
			break;
		case 'i':
			format |= INODES;
			break;
		case 'k':
			blocksize = 1024;
			break;
		case 'l':
			display = DLONG;
			break;
		case 'm':
			display = COMMA;
			break;
		case 'n':
			display = DLONG;
			numeric = 1;
			break;
		case 'o':
			display = DLONG;
			nogroup = 1;
			break;
		case 'p':
			format |= DIRS;
			break;
		case 'q':
			format |= QUOTE;	// FIXME
			break;
		case 'r':
			reverse = 1;
			break;
		case 's':
			format |= BLOCKS;	// FIXME
			break;
		case 't':
			sort = MTIME;
			break;
		case 'u':
			sort = ATIME;
			break;
		case 'x':
			display = ROWS;
			break;
		case '1':
			display = SINGLE;
			break;
		default:
			return 1;
		}
	}

	if (optind >= argc) {
		strcpy(dirlist[0].path, ".");
		strcpy(dirlist[0].display, ".");
		totaldirs = 1;
	}

	while (optind < argc) {
		if (ls_stat(argv[optind], &st) != 0) {
			perror(argv[optind]);
		} else if (!listdirs && S_ISDIR(st.st_mode)) {
			strcpy(dirlist[totaldirs].display, argv[optind]);
			strcpy(dirlist[totaldirs].path, argv[optind]);
			totaldirs++;
		} else {
			files[f] = argv[optind];
			f++;
		}
		optind++;
	}

	if (links == FOLLOW)
		lastfollow = totaldirs;

	for (i = 0; i < f; i++)
		ls_add(files[i], 0);

	ls_print(root);
	ls_reset();

	if (f > 0 && totaldirs > 0)
		putchar('\n');

	for (i = 0; i < totaldirs; i++) {
		int total = 0;

		if (i >= lastfollow && links == FOLLOW)
			links = NONE;

		total = ls_dir(dirlist[i].path);

		if (f > 0 || totaldirs > 1)
			printf("%s:\n", dirlist[i].display);
		if (display == DLONG || format & BLOCKS)
			printf("total %u\n", total);

		ls_print(root);

		if (totaldirs > i + 1)
			putchar('\n');

		ls_reset();
	}

	return 0;
}
