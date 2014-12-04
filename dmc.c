/* dmc - dynamic mail client
 * See LICENSE file for copyright and license details.
 */
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include "config.h"

static char *editor;
static char dmcdir[128];
static char **dmcaccounts();
static char prompt[64];
static char **acc, *defacc = NULL;
static int dmc_in[2], dmc_out[2], dmc_err[2];
static int dmc_pid = -1;
static fd_set rfds, wfds;

typedef struct {
	char *out;
	char *err;
	int errlen;
} DmcQuery;

static DmcQuery reply;

static void dmcstop();
static int dmcsend(const char *file);
static int dmcstore(const char *body);
static int fexist(const char *path);

#if 0
static char *dmcnamegen(const char *file) {
	// return filename from mail
	// Name is YYYYMMDDhhmm## -- name sort by date
	// if no Date..use file timestamp
	// well. we just need at the end.. a unique incremental ID
	// interesting mails are the last ones in mail server
	return NULL;
}

static int dmcstore() {
	// TODO
	return 0;
}
#endif

static const char* wd(const char *fmt, ...) {
	va_list ap;
	int len;
	static char ret[512];
	va_start (ap, fmt);
	len = strlen (dmcdir);
	memcpy (ret, dmcdir, len);
	vsnprintf (ret+len, sizeof (ret)-len, fmt, ap);
	va_end (ap);
	return ret;
}

static const char *abspath(const char *file) {
	static char filepath[512];
	char cwd[128];
	if (*file=='/')
		return file;
	getcwd (cwd, sizeof (cwd));
	snprintf (filepath, sizeof (filepath)-strlen (file)-3, "%s", cwd);
	strcat (filepath, "/");
	strcat (filepath, file);
	return filepath;
}

static const char *dmcalias(const char *filter) {
	static char line[64];
	FILE *fd = fopen (wd ("addrbook"), "r");
	if (fd) {
		for (;;) {
			fgets (line, sizeof (line), fd);
			if (feof (fd))
				break;
			if (strstr (line, filter)) {
				line[strlen(line)-1] = 0;
				return line;
			}
		}
		fclose (fd);
	}
	return filter;
}

static const char *dmcmailpath(const char *name) {
	static char path[256];
	/* check ./%s */
	if (fexist (name))
		return name;
	// TODO: simplify in loop and use wd()
	/* check DMCDIR/box/$acc/in/%s.eml */
	snprintf (path, sizeof (path), "%s/box/%s/in/%s.eml", dmcdir, defacc, name);
	if (fexist (path))
		return path;
	/* check DMCDIR/box/$acc/in/%s */
	snprintf (path, sizeof (path), "%s/box/%s/in/%s", dmcdir, defacc, name);
	if (fexist (path))
		return path;
	/* check DMCDIR/box/$acc/%s */
	snprintf (path, sizeof (path), "%s/box/%s/%s", dmcdir, defacc, name);
	if (fexist (path))
		return path;
	/* check DMCDIR/box/%s */
	snprintf (path, sizeof (path), "%s/box/%s", dmcdir, name);
	if (fexist (path))
		return path;
	/* not found */
	return NULL;
}

static int dmccat(const char *file) {
	char line[128];
	const char *f = dmcmailpath (file); // XXX dup?
	if (f && fexist (f)) {
		snprintf (line, sizeof (line), "cat '%s'", f);
		system (line); // implement native cat
		return 1;
	} else fprintf (stderr, "Cannot find '%s'\n", file);
	return 0;
}

static char *gethdr(const char *path, const char *file, const char *hdr) {
	char line[1024], *ret = NULL;
	FILE *fd;
	int i;
	snprintf (line, sizeof (line), "%s/%s", path, file);
	fd = fopen (line, "r");
	if (fd) {
		int len = strlen (hdr);
		while (!feof (fd)) {
			fgets (line, sizeof (line), fd);
			if (!memcmp (line, hdr, len)) {
				i = strlen (line)-1;
				if (i>len) {
					line[i--] = '\0';
					if (line[i]=='\r'||line[i]=='\n')
						line[i] = '\0';
					ret = strdup (line+len);
				} else ret = strdup ("");
				break;
			}
		}
		fclose (fd);
	}
	return ret;
}

static int dmcls(const char *path) {
	struct dirent *de;
	DIR *dir;
	if ((dir = opendir (path))) {
		while ((de = readdir (dir))) {
			if (*de->d_name!='.') {
				char *subj = gethdr (path, de->d_name, "Subject: ");
				char *from = gethdr (path, de->d_name, "From: ");
				printf ("%s:\t%s\n\t%s\n", de->d_name, subj, from);
				free (from);
				free (subj);
			}
		}
		closedir (dir);
		return 1;
	}
	return 0;
}

static void dmcinit() {
	char *tmp = getenv ("EDITOR");
	editor = tmp? tmp: EDITOR;
	if (!(tmp = getenv ("HOME"))) {
		fprintf (stderr, "Cannot find HOME\n");
		exit (1);
	}
	snprintf (dmcdir, sizeof (dmcdir), "/%s/"DMCDIR"/", tmp);
	if (!fexist (dmcdir))
	if (mkdir (dmcdir, DIRPERM) == -1) {
		fprintf (stderr, "Cannot create \"%s\"\n", dmcdir);
		exit (1);
	}
	mkdir (wd ("tmp"), DIRPERM);
	mkdir (wd ("box"), DIRPERM);
	mkdir (wd ("acc"), DIRPERM);
	acc = dmcaccounts ();
	defacc = acc[0];
	signal (SIGINT, dmcstop);
	atexit (dmcstop);
	reply.out = reply.err = NULL;
}

static char *cfgget(const char *key) {
	FILE *fd;
	char line[128], *ptr, *ret = NULL;
	if ((fd = fopen (wd ("acc/%s", defacc), "r"))) {
		int len = strlen (key);
		while (!feof (fd)) {
			fgets (line, sizeof (line), fd);
			if (!memcmp (line, key, len)) {
				ptr = strchr (line, '#');
				if (ptr) {
					for (*ptr=0, ptr--; ptr != line && (*ptr==' ' || *ptr=='\t'); ptr--)
						*ptr = 0;
				} else line [strlen (line)-1] = 0;
				ret = strdup (line+len);
				break;
			}
		}
		fclose (fd);
	}
	return ret;
}

/* server */
static int dmcstart(const char *name) {
	char a0[512], a1[32];
	char *host, *port, *prot, *ssl;
	pipe (dmc_in);
	pipe (dmc_out);
	pipe (dmc_err);
	dmc_pid = fork ();
	signal (SIGPIPE, SIG_IGN);
	switch (dmc_pid) {
	case -1:
		fprintf (stderr, "Cannot fork\n");
		exit (1);
	case 0:
		dup2 (dmc_in[0], 0);
		close (dmc_in[0]);
		close (dmc_in[1]);

		dup2 (dmc_out[1], 1);
		close (dmc_out[0]);
		close (dmc_out[1]);

		dup2 (dmc_err[1], 2);
		close (dmc_err[0]);
		close (dmc_err[1]);

		host = cfgget ("HOST=");
		port = cfgget ("PORT=");
		prot = cfgget ("PROTOCOL=");
		ssl = cfgget ("SSL=");
		ssl = ssl? atoi (ssl)? "1": NULL: NULL;
		snprintf (a0, sizeof (a0), PREFIX"/bin/dmc-%s", prot);
		snprintf (a1, sizeof (a1), "dmc-%s", prot);
		printf ("(%s)\n", a0);
		execl (a0, a1, host, port, ssl, NULL);
		free (host);
		free (port);
		exit (1);
	default:
		close (dmc_out[1]);
		close (dmc_err[1]);
		close (dmc_in[0]);
		break;
	}
	return 0;
}

static void dmckill(int sig) {
	fprintf (stderr, "Killed.\n");
	kill (dmc_pid, SIGKILL);
}

static void dmcstop() {
	if (dmc_pid != -1) {
		const char *cmd = "exit\n";
		write (dmc_in[1], cmd, strlen (cmd));
		signal (SIGALRM, dmckill);
		alarm (2);
		waitpid (dmc_pid, NULL, 0);
		signal (SIGALRM, NULL);
		alarm (0);
		dmc_pid = -1;

		// duppy
		close (dmc_in[0]);
		close (dmc_in[1]);
		close (dmc_out[0]);
		close (dmc_out[1]);
		close (dmc_err[0]);
		close (dmc_err[1]);
	}
}

static int dmccmd(const char *cmd) {
	static char buf[128];
	int ret, nfd;

	if (dmc_pid == -1) {
		printf ("Use 'on' or '?'\n");
		return 0;
	}
	free (reply.out);
	reply.out = NULL;
	free (reply.err);
	reply.err = NULL;
	reply.errlen = 0;
	for (;;) {
		FD_ZERO (&rfds);
		FD_ZERO (&wfds);
		FD_SET (dmc_out[0], &rfds);
		FD_SET (dmc_err[0], &rfds);
		FD_SET (dmc_in[1], &wfds);
		nfd = select (dmc_err[0] + 1, &rfds, &wfds, NULL, NULL);
		if (nfd < 0)
			break;
		if (FD_ISSET (dmc_out[0], &rfds)) {
			ret = read (dmc_out[0], buf, sizeof (buf)-1);
			if (ret>0) {
				buf[ret-1] = 0;
				reply.out = strdup (buf); // XXX
				//printf ("-(out)-> (%s)\n", buf);
				break;
			}
		} else
		if (FD_ISSET (dmc_err[0], &rfds)) {
			ret = read (dmc_err[0], buf, sizeof (buf)-1);
			if (ret>0) {
				buf[ret-1] = 0;
				reply.err = realloc (reply.err, reply.errlen+ret+1);
				memcpy (reply.err+reply.errlen, buf, ret);
				reply.errlen += ret-1;
				//printf ("-(err)-> (%s)\n", buf);
			}
		} else
		if (FD_ISSET (dmc_in[1], &wfds)) {
			if (cmd && *cmd && *cmd != '\n') {
				snprintf (buf, sizeof (buf), "%s\n", cmd);
				write (dmc_in[1], buf, strlen (buf));
				if (DEBUG) printf ("-(wri)-> (%s)\n", cmd);
				cmd = NULL;
			}
		}
	}
	return 1;
}

static void dmcpush(const char *name) {
	struct dirent *de;
	char path[256], file[512];
	DIR *dir;
	int i;

	for (i=0; acc[i]; i++) {
		snprintf (path, sizeof (path), "%s/box/%s/out", dmcdir, acc[i]);
		dir = opendir (path);
		while ((de = readdir (dir))) {
			char *n = de->d_name;
			if (*n != '.' && !strstr (n, ".d")) {
				snprintf (file, sizeof (file), "%s/%s", path, n);
				printf ("%s: ", file);
				fflush (stdout);
				if (dmcsend (file)) {
					snprintf (path, sizeof (path), "mv %s* '%s'",
						file, wd ("box/%s/sent", defacc));
					system (path); // do not use system()
				}
			}
		}
		closedir (dir);
	}
}

static int fexist(const char *path) {
	struct stat st;
	int ret = path? stat (path, &st): -1;
	return (ret != -1);
}

static int fsize(const char *path) {
	FILE *fd = fopen (path, "r");
	int len = 0;
	if (fd) {
		fseek (fd, 0, SEEK_END);
		len = ftell (fd);
		fclose (fd);
	}
	return len;
}

static char *parsedate(const char *str) {
	static char buf[256];
	char *ch;
	strncpy (buf, str, sizeof (buf));
 	if ((ch = strchr (buf, ','))) {
		int year, month, day = atoi (ch+1);
		if (!day)
			return 0;
		ch = strchr (ch+2, ' ');
		while (*ch==' ')ch++;
		if (ch) {
			char *q, *p = strchr (ch+3, ' ');
			if (p) {
				*p = 0;
				printf ("MONTH (%c%c%c)\n", ch[0], ch[1], ch[2]);
				month = 0; // TODO
				year = atoi (p+1);
				if (year) {
					q = strchr (p+1, ' ');
					if (q) {
						int h,m,s;
						*q = 0;
						sscanf (p+1, "%d:%d:%d", &h, &m, &s);
						snprintf (buf, sizeof (buf),
							"%s/box/%s/in/%04d%02d%02d-%02d%02d%02d",
							dmcdir, defacc, year, month, day, h, m, s);
						/* return 0: FUCK YEAH!!1 */
					} else return 0;
				} else return 0;
			} else return 0;
		} else return 0;
	} else return 0;
	return buf;
}

// TODO: support to store only the header, not full mail
static int dmcstore(const char *body) {
	char dmctag[512], *file, *end, *ptr = strstr (body, "Date: "); // TODO: use gethdr..on buffer, not file
	if (ptr) {
		ptr = ptr + 6;
		end = strchr (ptr, '\n');
		// generate filename from date
		file = parsedate (ptr);
		if (file) {
printf ("FILE IS =%s\n", file);
			if (!fexist (file)) {
				FILE *fd = fopen (file, "w");
				if (fd) {
					fputs (body, fd);
					fclose (fd);
					// TODO extract attachments
					// TODO save only body
					snprintf (dmctag, sizeof (dmctag),
						"dmc-tag '%s' new `dmc-tag %s`", file, file);
					system (dmctag);
					return 1;
				} else fprintf (stderr, "Cannot write file '%s'\n", file);
			} else fprintf (stderr, "Already downloaded.\n");
		} else fprintf (stderr, "Cannot generate file name for (%s)\n", body);
	} else fprintf (stderr, "Cannot find date header.\n");
	return 0;
}

static int dmcpull(int num, int lim, int dir) {
	int ret, count = 0;
	char cmd[64];
	char *slimit = cfgget ("LIMIT=");
	int limit = slimit? atoi (slimit): LIMIT;
	for (;num!=lim && (!limit||count<limit);num+=dir) {
		snprintf (cmd, sizeof (cmd), "cat %d\n", num);
		ret = dmccmd (cmd);
		printf ("RETURN IS = %d\n", ret);
		ret = dmcstore (reply.err); // TODO: add support for folders
		printf ("dmcstore ret = %d\n", ret);
		if (!ret)
			break;
		count ++;
	}
	printf ("%d new messages\n", count);
	return count;
}

static char **dmcaccounts() {
	struct dirent *de;
	static char *accounts[MAXACC];
	char buf[256], *defacc = NULL;
	DIR *dir;
	int acc = 0;
	memset (buf, 0, sizeof (buf));
	if (readlink (wd ("acc.default"), buf, sizeof (buf))!=-1) {
		defacc = buf + strlen (buf)-1;
		while (*defacc != '/')
			defacc--;
		accounts[acc++] = strdup (++defacc);
	} else fprintf (stderr, "No default account defined.\n");
	dir = opendir (wd ("acc"));
	while ((de = readdir (dir)) && acc<MAXACC-2)
		if (*de->d_name != '.' && (!defacc || strcmp (de->d_name, defacc)))
			accounts[acc++] = strdup (de->d_name);
	closedir (dir);
	accounts[acc] = NULL;
	return accounts;
}

static void charrfree(char **ptr) {
	char **p = ptr;
	while (*p) {
		free(*p);
		p++;
	}
	*ptr = NULL;
}

static int dmcsend(const char *file) {
	char line[512];
	char *user = cfgget ("USER=");
	char *mail = cfgget ("MAIL=");
	char *to = gethdr ((*file=='/')?"/":"./", file, "To: ");
	/* TODO: Implement non-msmtp method */
	snprintf (line, sizeof (line),
		"dmc-pack `ls %s.d/* 2>/dev/null` < %s "
		"| msmtp --user=\"%s\" --from=\"%s\" \"%s\"",
		file, file, user, mail, to);
	if (system (line) != 0) {
		fprintf (stderr, "Error ocurred while sending %s\n", file);
		return 0;
	} else fprintf (stderr, "Sent.\n");
	return 1;
}

static int dmcline(const char *line) {
	int ret = 1;
	char cmd[128];
	if (!strcmp (line, "?")) {
		printf ("Usage: on off push pull exit ls lsd cat ..\n");
	} else
	if (!strcmp (line, "login")) {
		char *user = cfgget ("USER=");
		char *pass = cfgget ("PASS=");
		snprintf (cmd, sizeof (cmd), "login %s %s\n", user, pass);
		if (!dmccmd (cmd))
			printf ("Error.\n");
		free (user);
		free (pass);
	} else
	if (!strcmp (line, "on")) {
		dmcstart (acc[0]);
		if (!dmccmd (NULL))
			printf ("Error.\n");
	} else
	if (!memcmp (line, "get ", 4)) {
		snprintf (cmd, sizeof (cmd), "cat %s", line+4);
		if (dmc_pid != -1) {
			dmccmd (cmd);
			printf ("CHECK (%s)\n", reply.out);
			// TODO. if (check)..
			dmcstore (reply.err);
		} else fprintf (stderr, "Not connected.\n");
	} else
	if (!strcmp (line, "off")) {
		dmcstop ();
	} else
	if (!strcmp (line, "ls")) {
		if (dmc_pid != -1)
			dmccmd ("ls\n");
		else dmcls (wd ("box/%s/in", defacc));
	} else
	if (!memcmp (line, "cat ", 4)) {
		if (dmc_pid != -1)
			dmccmd (line); // bypass
		else dmccat (line+4);
	} else
	if (!strcmp (line, "push")) {
		dmcpush (defacc);
	} else
	if (!strcmp (line, "pull")) {
		dmcpull (5, 9, 1); // pop3 test
	} else
	if (!strcmp (line, "exit")) {
		return 0;
	} else {
		/* bypass to child */
		if (!dmccmd (line)) {
			fprintf (stderr, "## No reply\n");
			ret = 0;
		}
	}
	if (ret) {
		printf ("-(out)-> %s\n", reply.out);
		//printf ("-(err)-> %s\n", reply.err);
	}
	return 1;
}

static int usage(const char *argv0, int lon) {
	fprintf (stderr, "Usage: %s [-hv] [-c [cmd] [-s file] [-e acc] [-A file ..]\n"
		"\t[-a addr] [-m [a [s [..]] [-f m a] [-r m a] [-l [box]]\n", argv0);
	if (lon) fprintf (stderr,
		" -m [a [s]..] create mail [addr [subj [file1 file2 ..]]]\n"
		" -c [cmd]     command shell\n"
		" -e [acc]     list/edit/set default accounts\n"
		" -a [addr]    grep in addressbook\n"
		" -A [file .]  attach files to ~/.dmc/mail.last done by 'dmc -m'\n"
		" -l [box]     list mails in specified box of def account\n"
		" -s [file]    send mail\n"
		" -f m [addr]  forward mail to addr\n"
		" -r m [addr]  reply mail to addr\n"
		" -v           show version\n"
		"");
	return 0;
}

static int dmcmail(const char *addr, const char *subj, const char *slurp, const char *slurptitle) {
	const char *from = cfgget("MAIL=");
	char file[128], line[128];
	int fd, ret = 0;
	snprintf (file, sizeof (file), "%s/box/%s/out/mail.XXXXXX", dmcdir, acc[0]);
	fd = mkstemp (file);
	if (fd != -1) {
		// TODO: fchmod or mkostemp
		snprintf (line, sizeof (line),
			"X-Mailer: dmc v"VERSION"\n"
			"From: %s\n"
			"To: %s\n"
			"Subject: %s\n\n\n", from, addr, subj);
		write (fd, line, strlen (line));
		if (slurp) {
			FILE *sfd = fopen (slurp, "r");
			if (sfd) {
				int body = 0;
				write (fd, "\n\n", 2);
				if (slurptitle)
					write (fd, slurptitle, strlen (slurptitle));
				for (;;) {
					fgets (line, sizeof (line), sfd);
					if (feof (sfd)) break;
					if (body) {
						write (fd, "> ", 2);
						write (fd, line, strlen (line));
					} else if (strlen (line)<4) body = 1;
				}
			}
		}
		close (fd);
		snprintf (line, sizeof (line), EDITOR" '%s'", file);
		system (line);
		if (fsize (file)<32) {
			fprintf (stderr, "Aborted\n");
			unlink (file);
		} else {
			snprintf (line, sizeof (line), "%s/mail.last", dmcdir);
			unlink (line);
			symlink (file, line);
			ret = 1;
		}
	} else fprintf (stderr, "Cannot create '%s'\n", file);
	return ret;
}

static void dmcattach(const char *file) {
	char path[256];
	const char *name = file + strlen (file)-1;
	while (*name!='/' && name>file)
		name--;
	name++;
	path[0] = 0;
	memset (path, 0, sizeof(path));
	if (readlink (wd ("mail.last"), path, sizeof (path)-strlen (name)-2)!=-1) {
		strcat (path, ".d/");
		mkdir (path, 0750);
		strcat (path, name);
		symlink (abspath (file), path);
	} else fprintf (stderr, "Cannot attach '%s'\n", file);
}

static char *evalstr(const char *mail, const char *str) {
	int outi = 0;
	char *out, *tmp;
	const char *ptr = str;
	out = malloc (1024); // XXX overflow
	while (*ptr) {
		if (*ptr == '{') {
			int dsti = 0;
			char dst[1024];
			ptr++;
			while (*ptr && *ptr != '}' && dsti<sizeof(dst)-1) {
				dst[dsti++] = *ptr;
				ptr++;
			}
			dst[dsti] = 0;
			tmp = gethdr ("/", mail, dst);
			if (tmp) {
				strcpy (out+outi, tmp);
				outi += strlen (tmp);
				free (tmp);
			}
			ptr++;
			continue;
		}
		out[outi++] = *ptr;
		ptr++;
	}
	out[outi] = 0;
	return out;
}

static void dmcfwd(const char *file, const char *addr, const char *msg, const char *sub) {
	if (file) {
		char *m, *subj, *msub = gethdr ("/", file, "Subject: ");
		if (msub) {
			char *s = evalstr (file, sub);
			subj = malloc (strlen (msub) + strlen (s) + 2);
			strcpy (subj, s);
			strcat (subj, msub);
			free (s);
		} else {
			subj = strdup (sub);
		}
		if (addr && *addr)
			addr = dmcalias (addr);
		m = evalstr (file, msg);
		dmcmail (addr, subj, file, m);
		free (m);
		free (subj);
	} else fprintf (stderr, "Mail not found.\n");
}

int main(int argc, char **argv) {
	char file[128], line[128];
	int i;

	if (argc < 2)
		return usage (argv[0], 0);

	dmcinit ();
	if (strcmp (argv[1], "-e")) {
		if (!acc[0]) {
			fprintf (stderr, "Use 'dmc -e' to configure the account\n");
			return 1;
		}
	}

	if (argv[1][0] == '-')
	switch (argv[1][1]) {
	case 'l':
		if (argc>2) {
			if (dmccat (argv[2]))
				break;
			snprintf (line, sizeof (line), "%s/box/%s", dmcdir, argv[2]);
		} else snprintf (line, sizeof (line), "%s/box/%s/in", dmcdir, acc[0]);
		dmcls (line);
		break;
	case 'A':
		for (i=2; i<argc; i++)
			dmcattach (argv[i]);
		break;
	case 'v':
		printf ("dmc v"VERSION"\n");
		break;
	case 's':
		if (argc>2)
			dmcsend (argv[2]);
		else fprintf (stderr, "Usage: dmc-cmd [-s file]\n");
		break;
	case 'm':
		{
			const char *addr = "";
			const char *subj = "";
			if (argc>2)
				addr = argv[2];
			if (argc>3)
				subj = argv[3];
			if (addr && !strchr (addr, '@'))
				addr = dmcalias (addr);
			if (dmcmail (addr, subj, NULL, NULL))
				for (i=4; argv[i]; i++)
					dmcattach (argv[i]);
		}
		break;
	case 'e':
		if (argc<3) {
			for (i=0; acc[i]; i++)
				printf ("%s\n", acc[i]);
		} else {
			snprintf (file, sizeof (file), "%s/acc/%s", dmcdir, argv[2]);
			if (!fexist (file)) {
				/* create template */
				FILE *fd = fopen (file, "w");
				if (fd) {
					fprintf (fd,
						"LIMIT=50 # get only 50 mails for each folder\n"
						"PROTOCOL=pop3 # imap4\n"
						"HOST=serverhost.com\n"
						"PORT=110\n"
						"SSL=0\n"
						"#SEND=acc-name\n"
						"SEND=!msmtp\n"
						"MAIL=username@domain.com\n"
						"USER=username\n"
						"PASS=password\n");
					fclose (fd);
				} else {
					fprintf (stderr, "Cannot write in %s\n", file);
					return 1;
				}
			}
			snprintf (line, sizeof (line), EDITOR" %s", file);
			system (line);
			if (fsize(file)<32) {
				printf ("Abort\n");
				unlink (file);
			} else {
				printf ("Default account has changed.\n");
				unlink (wd ("acc.default"));
				symlink (file, wd ("acc.default"));
				mkdir (wd ("box/%s", argv[2]), DIRPERM);
				mkdir (wd ("box/%s/out", argv[2]), DIRPERM);
				mkdir (wd ("box/%s/in", argv[2]), DIRPERM);
				mkdir (wd ("box/%s/sent", argv[2]), DIRPERM);
			}
		}
		break;
	case 'a':
		if (argc<3) {
			snprintf (line, sizeof (line), "%s '%s'", editor, wd ("addrbook"));
			system (line);
		} else {
			const char *mail = dmcalias (argv[2]);
			if (mail)
				puts (mail);
		}
		break;
	case 'c':
		if (argc<3) {
			strcpy (prompt, "dmc> ");
			do {
				write (1, prompt, strlen (prompt));
				fgets(line, sizeof (line), stdin);
				if (feof (stdin))
					strcpy (line, "exit");
				else line[strlen (line)-1] = '\0';
			} while (dmcline (line));
		} else {
			strcpy (prompt, "");
			for (i=2; i<argc; i++)
				dmcline (argv[i]);
		}
		dmcstop ();
		break;
	case 'r':
		dmcfwd (dmcmailpath (argv[2]), argc>3?argv[3]:"", REPMSG, REPSUB);
		break;
	case 'f':
		dmcfwd (dmcmailpath (argv[2]), argc>3?argv[3]:"", FWDMSG, FWDSUB);
		break;
	default:
		return usage (argv[0], 1);
	}

	charrfree (acc);
	return 0;
}
