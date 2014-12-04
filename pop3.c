/* dmc - dynamic mail client
 * See LICENSE file for copyright and license details.
 */

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include "sock.c"

#define STRSZ 4095
static char cmd[STRSZ], word[STRSZ];

static char *getword() {
	fscanf (stdin, "%s", word);
	if (feof (stdin))
		*word = '\0';
	return word;
}

static int waitreply(int foo) {
	char *ch, *str, res[STRSZ];
	int reply = -1;

	ftruncate (2, 0);
	lseek (2, 0, SEEK_SET);
	*res = '\0';
	while (sock_ready () && sock_read (word, sizeof (word)) > 1) {
		str = word;
		if (reply == -1 && (reply = (word[0] == '+'))) {
			if ((ch = strchr (str, '\r')) || (ch = strchr (str, '\n'))) {
				*ch = '\0';
				snprintf (res, sizeof (res), "### %s %d \"%s\"\n", cmd, reply, str);
				str = ch + (ch[1] == '\n' ? 2 : 1);
			}
		}
		// TODO: Fix possible \r\n issues
		if ((ch = strstr (str, "\r\n.")))
			*ch = '\0';
		write (2, str, strlen (str));
	}
	write (2, "\n", 1);
	if (foo) {
		if (!*res)
			snprintf (res, sizeof (res), "### %s %d \"\"\n", cmd, reply);
		write (1, res, strlen (res));
	}
	return reply;
}

static int doword(char *word) {
	int ret = 1;
	strcpy (cmd, word);

	if (*word == '\0') {
		/* Do nothing */
	} else
	if (!strcmp (word, "exit")) {
		sock_printf ("QUIT\n");
		waitreply (1);
		ret = 0;
	} else
	if (!strcmp (word, "help") || !strcmp (word, "?")) {
		printf ("Use: ls cat head rm login exit\n");
	} else
	if (!strcmp (word, "ls")) {
		sock_printf ("LIST\n");
		waitreply (1);
	} else
	if (!strcmp (word, "cat")) {
		sock_printf ("RETR %d\n", atoi (getword ()));
		waitreply (1);
	} else
	if (!strcmp (word, "head")) {
		sock_printf ("TOP %d 50\n", atoi (getword ()));
		waitreply (1);
	} else
	if (!strcmp (word, "rm")) {
		sock_printf ("DELE %d\n", atoi (getword ()));
		waitreply (1);
	} else
	if (!strcmp (word, "login")) {
		sock_printf ("USER %s\n", getword ());
		waitreply (0); // TODO: if user fail, do not send pass
		sock_printf ("PASS %s\n", getword ());
		waitreply (1);
	} else sock_printf ("NOOP\n");
	return ret;
}

int main(int argc, char **argv) {
	int ssl = 0, ret = 1;
	if (argc > 2) {
		if (argc > 3)
			ssl = (*argv[3] == '1');
		if (sock_connect (argv[1], atoi (argv[2]), ssl) >= 0) {
			ret = atexit (sock_close);
			strcpy (cmd, ".");
			waitreply (1);
			while (doword (getword ()));
		} else {
			printf ("## on -1 Cannot connect to %s %d\n", argv[1], atoi (argv[2]));
			fprintf(stderr, "\n");
		}
	} else printf ("Usage: dmc-pop3 host port [ssl] 2> body > fifo < input\n");
	return 0;
}
