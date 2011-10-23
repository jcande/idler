/*
 * Copyright (c) 2011, Jared Candelaria (jcande @ github)
 * 
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <X11/Xlib.h>
#include <X11/extensions/scrnsaver.h> 

#include <err.h>
#include <errno.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define PROG	"idler"
/* Low enough to not overflow and
 * large enough for anybody. Quote me. */
#define LIM	4000000

void
usage(void)
{
	errx(1, "usage: %s [-h | -t timeout_seconds] command\n", PROG);
}

void *
xmalloc(size_t s)
{
	void *p = malloc(s);

	if (!p)
		err(1, NULL);

	return p;
}

void
get_absolute(char *relative, char *loc)
{
	unsigned len;
	char wd[PATH_MAX];

	if (!getcwd(wd, PATH_MAX))
		err(1, NULL);

	len = snprintf(loc, PATH_MAX, "%s/%s", wd, relative);
	if (len <= 0 || len >= PATH_MAX)
		errx(1, "%s: file name too long", loc);
}

/* XXX Doesn't check if it's executable. */
int
proper_file(const char *file)
{
	struct stat f;

	if (stat(file, &f))
		return 0;

	return (S_ISREG(f.st_mode));
}

/* Taken from OpenBSD's sudo(8). */
/* Thanks Todd C. Miller! */
int
valid_command(const char *file, int *relative)
{
	char *p, *save, *path, command[PATH_MAX];
	int len, ret;

	/* We assume it's an absolute location. */
	*relative = 0;

	/* Does this require searching through PATH? */
	if (strchr(file, '/')) {
		if (*file != '/')
			*relative = 1;
		return proper_file(file);
	}

	if ((p = getenv("PATH")) == NULL)
		return 0;

	len = (int)strlen(p);
	save = path = xmalloc((unsigned)len + 1);
	strncpy(path, p, (unsigned)len);
	path[len] = '\0';

	ret = 0;
	for (p = path; p; path = p + 1) {
		if ((p = strchr(path, ':')))
			*p = '\0';

		len = snprintf(command, PATH_MAX, "%s/%s", path, file);
		if (len <= 0 || len >= (int)sizeof(command))
			errx(1, "%s: file name too long", file);

		if (proper_file(command)) {
			ret = 1;
			break;
		}
	}
	free(save);

	return ret;
}

void
launch(char *file, char **command)
{
	switch (fork()) {
	case -1:
		err(1, NULL);
	case 0:
		execvp(file, command);
		/* We shouldn't ever reach here. */
		err(1, NULL);
	default:
		wait(NULL);
	}
}

int
main(int argc, char **argv)
{
	XScreenSaverInfo *info;
	Display *d;
	Window w;

	unsigned long timeout, saved_timeout;
	char c, file[PATH_MAX];
	int relative;

	info = XScreenSaverAllocInfo();
	d = XOpenDisplay(NULL);
	w = DefaultRootWindow(d);

	errno = 0;
	relative = 0;
	/* Default timeout of 10 minutes. */
	timeout = 600000;

	while ((c = getopt(argc, argv, "+ht:")) != -1) {
		switch(c) {
		case 't':
			timeout = (int)strtol(optarg, NULL, 10);
			if (errno || timeout > LIM)
				errx(1, "%s: invalid timeout", optarg);
			/* Convert timeout into milliseconds. */
			timeout *= 1000;
			break;
		case 'h':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0)
		errx(1, "must specify a command");

	if (!valid_command(argv[0], &relative))
		errx(1, "%s: command not found", argv[0]);

	/* Convert our relative path to an absolute one. */
	if (relative)
		get_absolute(argv[0], file);
	else
		snprintf(file, PATH_MAX, "%s", argv[0]);

	daemon(0, 0);

	saved_timeout = 0;
	for (;;) {
		XScreenSaverQueryInfo(d, w, info);

		if (info->idle > timeout && saved_timeout == 0) {
			saved_timeout = info->idle;
			launch(file, argv);
		} else
			/* Sleep for .1 second. Maybe use sleep(3) instead? */
			usleep(100000);

		/* Make sure we only run once per idle period. */
		if (info->idle < saved_timeout)
			saved_timeout = 0;
	}
	/* NOT REACHED */

	XCloseDisplay(d);
	XFree(info);

	return 0;
}
