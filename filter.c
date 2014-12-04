/* dmc - dynamic mail client
 * See LICENSE file for copyright and license details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
	char b[1024], argv2[1024][1024], *ptr;
	int edit = argc, body = 0, filter = 1, value = 0, print = 0, i, j;

	if (argc > 1) {
		if (!strcmp (argv[1], "-h")) {
			printf ("Usage: %s [-bhv] [headers | :] [-e] [new headers] < mail\n", argv[0]);
			return 1;
		} else if (!strcmp (argv[1], "-v")) {
			value = 1;
			filter++;
		} else if (!strcmp (argv[1], "-b")) {
			body = 1;
			filter++;
		}
		for (i = filter; i < argc; i++)
			if (!strcmp (argv[i], "-e"))
				edit = i;
		for (i = 0; i < argc; i++) {
			strncpy (argv2[i], argv[i], sizeof (argv2[i]));
			argv2[i][1023] = '\0';
		}
	}
	*b = '\0';
	/* Headers */
	while (fgets (b, sizeof (b), stdin) && b[0] != '\n') {
		if (b[0] == '\r' || b[0] == '\n')
			break;
		if ((b[0] == ' ' || b[0] == '\t')) {
			if (print) fputs (b, stdout);
		} else for (i = filter; i < edit && argv[i]; i++)
			if (!strncmp (b, argv[i], strlen(argv[i])) || argv[i][0] == ':') {
				/* Edit/Remove Headers */
				print = 1;
				for (j = edit + 1; !value && j < argc && argv[j]; j++)
					if ((ptr = strchr (argv[j], ':')) &&
						!strncmp (b, argv[j], ptr - argv[j] + 1)) {
						if (ptr[1] != '\0' && argv2[j][0])
							puts (argv[j]);
						argv2[j][0] = '\0';
						print = 0;
						break;
					}
				if (print) {
					if (value && (ptr = strchr (b, ':')))
						ptr += 2;
					else ptr = b;
					fputs (ptr, stdout);
				}
				break;
			} else print = 0;
	}
	if (!value) {
		/* New Headers */
		for (i = edit + 1; i < argc; i++)
			if (argv2[i][0]) puts (argv2[i]);
		if (edit < argc) puts ("");
		/* Body */
		if (body)
		while ((body || argc<2 || edit<argc) && fgets (b, sizeof (b), stdin))
			fputs (b, stdout);
	}
	return 0;
}
