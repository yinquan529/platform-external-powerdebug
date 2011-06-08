/*******************************************************************************
 * Copyright (C) 2010, Linaro Limited.
 *
 * This file is part of PowerDebug.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors:
 *     Amit Arora <amit.arora@linaro.org> (IBM Corporation)
 *       - initial API and implementation
 *******************************************************************************/

#include <stdio.h>
#include <mntent.h>
#include <sys/stat.h>

#include "powerdebug.h"
#include "clocks.h"
#include "tree.h"

#define MAX_LINES 120

static char clk_dir_path[PATH_MAX];
static int  bold[MAX_LINES];
static char clock_lines[MAX_LINES][128];
static int clock_line_no;
static int old_clock_line_no;

struct clock_info {
	char name[NAME_MAX];
	int flags;
	int rate;
	int usecount;
	int num_children;
	int last_child;
	int expanded;
	int level;
	struct clock_info *parent;
	struct clock_info **children;
} *clocks_info;

static struct tree *clock_tree;

static int locate_debugfs(char *clk_path)
{
	const char *mtab = "/proc/mounts";
	struct mntent *mntent;
	int ret = -1;
	FILE *file = NULL;

	file = setmntent(mtab, "r");
	if (!file)
		return -1;

	while ((mntent = getmntent(file))) {

		if (strcmp(mntent->mnt_type, "debugfs"))
			continue;

		strcpy(clk_path, mntent->mnt_dir);
		ret = 0;
		break;
	}

	fclose(file);
	return ret;
}

int clock_init(void)
{
	if (locate_debugfs(clk_dir_path))
		return -1;

	sprintf(clk_dir_path, "%s/clock", clk_dir_path);

	clock_tree = tree_load(clk_dir_path, NULL);
	if (!clock_tree)
		return -1;

	return access(clk_dir_path, F_OK);
}

static int file_read_from_format(char *file, int *value, const char *format)
{
	FILE *f;
	int ret;

	f = fopen(file, "r");
	if (!f)
		return -1;
	ret = fscanf(f, format, value);
	fclose(f);

	return !ret ? -1 : 0;
}

static inline int file_read_int(char *file, int *value)
{
	return file_read_from_format(file, value, "%d");
}

static inline int file_read_hex(char *file, int *value)
{
	return file_read_from_format(file, value, "%x");
}

static void dump_parent(struct clock_info *clk, int line, bool dump)
{
	char *unit = "Hz";
	double drate;
	static char spaces[64];
	char str[256];
	static int maxline;

	if (maxline < line)
		maxline = line;

	if (clk && clk->parent)
		dump_parent(clk->parent, ++line, dump);

	drate = (double)clk->rate;
	if (drate > 1000 && drate < 1000000) {
		unit = "KHz";
		drate /= 1000;
	}
	if (drate > 1000000) {
		unit = "MHz";
		drate /= 1000000;
	}
	if (clk == clocks_info) {
		line++;
		strcpy(spaces, "");
		sprintf(str, "%s%s (flags:0x%x,usecount:%d,rate:%5.2f %s)\n",
			spaces, clk->name, clk->flags, clk->usecount, drate,
			unit);
	} else {
		if (!(clk->parent == clocks_info))
			strcat(spaces, "  ");
		sprintf(str, "%s`- %s (flags:0x%x,usecount:%d,rate:%5.2f %s)\n",
			spaces, clk->name, clk->flags, clk->usecount, drate,
			unit);
	}
	if (dump)
		//printf("line=%d:m%d:l%d %s", maxline - line + 2, maxline, line, str);
		printf("%s", str);
	else
		print_one_clock(maxline - line + 2, str, 1, 0);
}

static struct clock_info *find_clock(struct clock_info *clk, char *clkarg)
{
	int i;
	struct clock_info *ret = clk;

	if (!strcmp(clk->name, clkarg))
		return ret;

	if (clk->children) {
		for (i = 0; i < clk->num_children; i++) {
			if (!strcmp(clk->children[i]->name, clkarg))
				return clk->children[i];
		}
		for (i = 0; i < clk->num_children; i++) {
			ret = find_clock(clk->children[i], clkarg);
			if (ret)
				return ret;
		}
	}

	return NULL;
}

static void dump_all_parents(char *clkarg, bool dump)
{
	struct clock_info *clk;
	char spaces[1024];

	strcpy(spaces, "");

	clk = find_clock(clocks_info, clkarg);

	if (!clk)
		printf("Clock NOT found!\n");
	else {
		/* while(clk && clk != clocks_info) { */
		/* 	printf("%s\n", clk->name); */
		/* 	strcat(spaces, "  "); */
		/* 	clk = clk->parent; */
		/* 	printf("%s <-- ", spaces); */
		/* } */
		/* printf("  /\n"); */
		dump_parent(clk, 1, dump);
	}
}

void find_parents_for_clock(char *clkname, int complete)
{
	char name[256];

	name[0] = '\0';
	if (!complete) {
		char str[256];

		strcat(name, clkname);
		sprintf(str, "Enter Clock Name : %s\n", name);
		print_one_clock(2, str, 1, 0);
		return;
	}
	sprintf(name, "Parents for \"%s\" Clock : \n", clkname);
	print_one_clock(0, name, 1, 1);
	dump_all_parents(clkname, false);
}

static void destroy_clocks_info_recur(struct clock_info *clock)
{
	int i;

	if (clock && clock->num_children) {
		for (i = (clock->num_children - 1); i >= 0; i--) {
			fflush(stdin);
			destroy_clocks_info_recur(clock->children[i]);
			if (!i) {
				free(clock->children);
				clock->children = NULL;
				clock->num_children = 0;
			}
		}
	}
}

static void destroy_clocks_info(void)
{
	int i;

	if (!clocks_info)
		return;

	if (clocks_info->num_children) {
		for (i = (clocks_info->num_children - 1); i >= 0 ; i--) {
			destroy_clocks_info_recur(clocks_info->children[i]);
			if (!i) {
				free(clocks_info->children);
				clocks_info->children = NULL;
			}
		}
	}
	clocks_info->num_children = 0;
	free(clocks_info);
	clocks_info = NULL;
}


int read_and_print_clock_info(int verbose, int hrow, int selected)
{
	print_one_clock(0, "Reading Clock Tree ...", 1, 1);

	if (!old_clock_line_no || selected == REFRESH_WINDOW) {
		destroy_clocks_info();
		read_clock_info(clk_dir_path);
	}

	if (!clocks_info || !clocks_info->num_children) {
		fprintf(stderr, "powerdebug: No clocks found. Exiting..\n");
		exit(1);
	}

	if (selected == CLOCK_SELECTED)
		selected = 1;
	else
		selected = 0;

	print_clock_info(verbose, hrow, selected);
	hrow = (hrow < old_clock_line_no) ? hrow : old_clock_line_no - 1;

	return hrow;
}

static int calc_delta_screen_size(int hrow)
{
	if (hrow >= (maxy - 3))
		return hrow - (maxy - 4);

	return 0;
}

static void prepare_name_str(char *namestr, struct clock_info *clock)
{
	int i;

	strcpy(namestr, "");
	if (clock->level > 1)
		for (i = 0; i < (clock->level - 1); i++)
			strcat(namestr, "  ");
	strcat(namestr, clock->name);
}

static void collapse_all_subclocks(struct clock_info *clock)
{
	int i;

	clock->expanded = 0;
	if (clock->num_children)
		for (i = 0; i < clock->num_children; i++)
			collapse_all_subclocks(clock->children[i]);
}

static void add_clock_details_recur(struct clock_info *clock,
				    int hrow, int selected)
{
	int i;
	char *unit = " Hz";
	char rate_str[64];
	char name_str[256];
	double drate = (double)clock->rate;

	if (drate > 1000 && drate < 1000000) {
		unit = "KHz";
		drate /= 1000;
	}
	if (drate > 1000000) {
		unit = "MHz";
		drate /= 1000000;
	}
	if (clock->usecount)
		bold[clock_line_no] = 1;
	else
		bold[clock_line_no] = 0;

	sprintf(rate_str, "%.2f %s", drate, unit);
	prepare_name_str(name_str, clock);
	sprintf(clock_lines[clock_line_no++], "%-55s 0x%-4x  %-12s %-12d %-12d",
		name_str, clock->flags, rate_str, clock->usecount,
		clock->num_children);

	if (selected && (hrow == (clock_line_no - 1))) {
		if (clock->expanded)
			collapse_all_subclocks(clock);
		else
			clock->expanded = 1;
		selected = 0;
	}

	if (clock->expanded && clock->num_children)
		for (i = 0; i < clock->num_children; i++)
			add_clock_details_recur(clock->children[i],
						hrow, selected);
	strcpy(clock_lines[clock_line_no], "");
}

void print_clock_info(int verbose, int hrow, int selected)
{
	int i, count = 0, delta;

	(void)verbose;

	print_clock_header();

	for (i = 0; i < clocks_info->num_children; i++)
		add_clock_details_recur(clocks_info->children[i],
					hrow, selected);

	delta = calc_delta_screen_size(hrow);

	while (clock_lines[count + delta] &&
		strcmp(clock_lines[count + delta], "")) {
		if (count < delta) {
			count++;
			continue;
		}
		print_one_clock(count - delta, clock_lines[count + delta],
				bold[count + delta], (hrow == (count + delta)));
		count++;
	}

	old_clock_line_no = clock_line_no;
	clock_line_no = 0;
}

static void insert_children(struct clock_info **parent, struct clock_info *clk)
{
	if (!(*parent)->num_children || (*parent)->children == NULL) {
		(*parent)->children = (struct clock_info **)
			malloc(sizeof(struct clock_info *)*2);
		(*parent)->num_children = 0;
	} else
		(*parent)->children = (struct clock_info **)
			realloc((*parent)->children,
				sizeof(struct clock_info *) *
				((*parent)->num_children + 2));
	if ((*parent)->num_children > 0)
		(*parent)->children[(*parent)->num_children - 1]->last_child
			= 0;
	clk->last_child = 1;
	(*parent)->children[(*parent)->num_children] = clk;
	(*parent)->children[(*parent)->num_children + 1] = NULL;
	(*parent)->num_children++;
}

static struct clock_info *read_clock_info_recur(char *clkpath, int level,
						struct clock_info *parent)
{
	int ret = 0;
	DIR *dir;
	char filename[PATH_MAX];
	struct dirent *item;
	struct clock_info *cur = NULL;
	struct stat buf;

	dir = opendir(clkpath);
	if (!dir)
		return NULL;

	while ((item = readdir(dir))) {
		struct clock_info *child;
		/* skip hidden dirs except ".." */
		if (item->d_name[0] == '.' )
			continue;

		sprintf(filename, "%s/%s", clkpath, item->d_name);

		ret = stat(filename, &buf);

		if (ret < 0) {
			printf("Error doing a stat on %s\n", filename);
			exit(1);
		}

		if (S_ISREG(buf.st_mode)) {
			if (!strcmp(item->d_name, "flags"))
				file_read_hex(filename, &parent->flags);
			if (!strcmp(item->d_name, "rate"))
				file_read_int(filename, &parent->rate);
			if (!strcmp(item->d_name, "usecount"))
				file_read_int(filename, &parent->usecount);
			continue;
		}

		if (!S_ISDIR(buf.st_mode))
			continue;

		cur = (struct clock_info *)malloc(sizeof(struct clock_info));
		memset(cur, 0, sizeof(cur));
		strcpy(cur->name, item->d_name);
		cur->children = NULL;
		cur->parent = NULL;
		cur->num_children = 0;
		cur->expanded = 0;
		cur->level = level;
		child = read_clock_info_recur(filename, level + 1, cur);
		insert_children(&parent, cur);
		cur->parent = parent;
	}
	closedir(dir);

	return cur;
}

static struct clock_info *clock_alloc(const char *name)
{
	struct clock_info *ci;

	ci = malloc(sizeof(*ci));
	if (ci) {
		memset(ci, 0, sizeof(*ci));
		strcpy(ci->name, name);
	}

	return ci;
}

int read_clock_info(char *clkpath)
{
	DIR *dir;
	struct dirent *item;
	char filename[NAME_MAX];
	struct clock_info *child;
	struct clock_info *cur;
	int ret = -1;

	dir = opendir(clkpath);
	if (!dir)
		return -1;

	clocks_info = clock_alloc("/");
	if (!clocks_info)
		return -1;

	while ((item = readdir(dir))) {

		/* skip hidden dirs except ".." */
		if (item->d_name[0] == '.')
			continue;

		sprintf(filename, "%s/%s", clkpath, item->d_name);

		cur = clock_alloc(item->d_name);
		if (!cur)
			goto out;

		cur->parent = clocks_info;
		cur->num_children = 0;
		cur->expanded = 0;
		cur->level = 1;
		insert_children(&clocks_info, cur);
		child = read_clock_info_recur(filename, 2, cur);
	}

	ret = 0;

out:
	closedir(dir);

	return ret;
}

void read_and_dump_clock_info_one(char *clk, bool dump)
{
	printf("\nParents for \"%s\" Clock :\n\n", clk);
	read_clock_info(clk_dir_path);
	dump_all_parents(clk, dump);
	printf("\n\n");
}

void dump_clock_info(struct clock_info *clk, int level, int bmp)
{
	int i, j;

	if (!clk)
		return;

	for (i = 1, j = 0; i < level; i++, j = (i - 1)) {
		if (i == (level - 1)) {
			if (clk->last_child)
				printf("`-- ");
			else
				printf("|-- ");
		} else {
			if ((1<<j) & bmp)
				printf("|   ");
			else
				printf("    ");
		}
	}

	if (clk == clocks_info)
		printf("%s\n", clk->name);
	else {
		char *unit = "Hz";
		double drate = (double)clk->rate;

		if (drate > 1000 && drate < 1000000) {
			unit = "KHz";
			drate /= 1000;
		}
		if (drate > 1000000) {
			unit = "MHz";
			drate /= 1000000;
		}
		printf("%s (flags:0x%x,usecount:%d,rate:%5.2f %s)\n",
			clk->name, clk->flags, clk->usecount, drate, unit);
	}
	if (clk->children) {
		int tbmp = bmp;
		int xbmp = -1;

		if (clk->last_child) {
			xbmp ^= 1 << (level - 2);

			xbmp = tbmp & xbmp;
		} else
			xbmp = bmp;
		for (i = 0; i < clk->num_children; i++) {
			tbmp = xbmp | (1 << level);
			dump_clock_info(clk->children[i], level + 1, tbmp);
		}
	}
}

void read_and_dump_clock_info(int verbose)
{
	(void)verbose;
	printf("\nClock Tree :\n");
	printf("**********\n");
	read_clock_info(clk_dir_path);
	dump_clock_info(clocks_info, 1, 1);
	printf("\n\n");
}
