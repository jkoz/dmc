/* dmc - dynamic mail client
 * See LICENSE file for copyright and license details.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define SZ 1024

FILE *fd;
static char word[SZ];

static void mbox_ls() {
	char b[SZ], from[SZ], subject[SZ], date[SZ], *ptr;
	int m = 0, headers = 1;

	b[SZ-1] = '\0';
	fseek (fd, 0, SEEK_SET);
	while (fgets (b, SZ-1, fd)) {
		if (b[0]=='\n') {
			if (headers) {
				printf ("%i  %s  %s  %s\n", m++, from, date, subject);
				headers = 0;
			} else {
				fgets (b, SZ-1, fd);
				if (!memcmp (b, "From ", 5))
					headers = 1;
			}
		}
		if (headers) {
			if (!memcmp (b, "From: ", 6)) {
				strncpy (from, b+6, SZ-1);
				from[SZ-1] = '\0';
				if ((ptr = strchr (from, '\n')))
					ptr[0] = '\0';
			} else
			if (!memcmp (b, "Subject: ", 9)) {
				strncpy (subject, b+9, SZ-1);
				subject[SZ-1] = '\0';
				if ((ptr = strchr (subject, '\n')))
					ptr[0] = '\0';
			} else
			if (!memcmp (b, "Date: ", 6)) {
				strncpy (date, b+6, SZ-1);
				date[SZ-1] = '\0';
				if ((ptr = strchr (date, '\n')))
					ptr[0] = '\0';
			}
		}
	}
}

static void mbox_cat(int idx, int body) {
	char b[SZ];
	int m = 0, headers = 1;

	b[SZ-1] = '\0';
	fseek (fd, 0, SEEK_SET);
	while (fgets (b, SZ-1, fd)) {
		if (b[0]=='\n') {
			if (!headers) {
				fgets (b, SZ-1, fd);
				if (!memcmp (b, "From ", 5)) {
					headers = 1;
					m++;
				}
			} else headers = 0;
		}
		if (m == idx && (headers || body))
			fputs (b, stdout);
	}
}

static void mbox_rm (int idx) {
	char b[SZ], *buf;
	int m = 0, i = 0, headers = 1, size;

	fseek(fd, 0, SEEK_END);
	size = ftell (fd);
	if (!(buf = malloc (size)))
		return;
	b[SZ-1] = '\0';
	fseek (fd, 0, SEEK_SET);
	while (fgets (b, SZ-1, fd)) {
		if (b[0]=='\n') {
			if (!headers) {
				fgets (b, SZ-1, fd);
				if (!memcmp (b, "From ", 5)) {
					headers = 1;
					if (++m == idx) {
						strcpy (buf + i, "\n");
						i += 1;
					}
				}
			} else headers = 0;
		}
		if (m != idx) {
			strcpy (buf + i, b);
			i += strlen (b);
		}
	}
	fseek (fd, 0, SEEK_SET);
	fwrite (buf, 1, i, fd);
	fflush (fd);
	ftruncate (fileno (fd), i);
	free (buf);
}

static char *getword () {
	fscanf (stdin, "%255s", word);
	if (feof (stdin))
		*word = '\0';
	return word;
}

static int doword (char *word) {
	int ret = 1;
	if (*word == '\0') {
		/* Do nothing */
	} else if (!strcmp (word, "ls")) {
		mbox_ls ();
	} else if (!strcmp (word, "cat")) {
		mbox_cat (atoi (getword ()), 1);
	} else if (!strcmp (word, "rm")) {
		mbox_rm (atoi (getword ()));
	} else if (!strcmp (word, "head")) {
		mbox_cat (atoi (getword ()), 0);
	} else if (!strcmp (word, "login")) {
		getword (); // ignore login
		getword (); // ignore password
	} else if (!strcmp (word, "exit"))
		ret = 0;
	return ret;
}

int main (int argc, char **argv) {
	int ret = 1;
	if (argc>1) {
		fd = fopen (argv[1], "r+");
		if (fd != NULL) {
			while (doword (getword ()));
			ret = fclose (fd);
		} else printf ("Cannot open %s\n", argv[1]);
	} else printf ("Usage: dmc-mbox [mbox-file] 2> body > fifo < input\n");
	return ret;
}
