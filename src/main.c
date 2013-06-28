#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

#include "main.h"
#include "grab.h"
#include "configfile.h"
#include "mod_handle.h"
#include "log.h"

/* $Id: main.c,v 1.14 2003/04/20 21:53:35 dfx Exp $ */

static void main_init(char *, int);
static int kill_camsource(struct stat *);

int
main(int argc, char **argv)
{
	if (argc >= 2 && *argv[1] == '-') {
		if (!strcmp(argv[1], "-c"))
			main_init(argv[2], 1);
		else if (!strcmp(argv[1], "-k") || !strcmp(argv[1], "-s")) {
			struct stat sb;
			
			if (!argv[2])
				kill_camsource(NULL);
			else {
				if (!stat(argv[2], &sb) && S_ISCHR(sb.st_mode))
					kill_camsource(&sb);
				else
					printf("%s is not a device file\n", argv[2]);
			}
			exit(0);
		}
		else if (!strcmp(argv[1], "-r")) {
			struct stat sb;
			int arg;
			int ret;
			
			if (argv[2] && !stat(argv[2], &sb) && S_ISCHR(sb.st_mode)) {
				ret = kill_camsource(&sb);
				arg = 3;
			}
			else {
				ret = kill_camsource(NULL);
				arg = 2;
			}
			
			if (ret >= 1) {
				printf("Sleeping before starting up...\n");
				sleep(2);
			}
			main_init(argv[arg], 0);
			/* drop thru */
		}
		else {
			printf("Camsource version " VERSION "\n");
			printf("Usage:\n");
			printf("  %s [configfile]\n", argv[0]);
			printf("       - Starts camsource, optionally with a certain config file.\n");
			printf("  %s {-k | -s} [device]\n", argv[0]);
			printf("       - Shuts down (kills) the camsource instance which has the given\n");
			printf("         video device opened. If no device is given, kills all camsource\n");
			printf("         instances.\n");
			printf("  %s -r [device] [configfile]\n", argv[0]);
			printf("       - Restarts camsource. This flag combines a 'camsource -k [device]'\n");
			printf("         call with a 'camsource [configfile]' call. Both arguments are\n");
			printf("         optional.\n");
			printf("  %s -c [configfile]\n", argv[0]);
			printf("       - Loads the specified (or default) config file, and dumps the\n");
			printf("         capabilities for each specified grabbing device, then exits.\n");
			printf("  %s -h\n", argv[0]);
			printf("       - Shows this text.\n");
			exit(0);
		}
	}
	else
		main_init(argv[1], 0);
	
	
	/* nothing to do, so exit */
	pthread_exit(NULL);
	
	return 0;
}

static
void
main_init(char *config, int dump)
{
	int ret;
	int logfd;
	
	signal(SIGPIPE, SIG_IGN);
	
	printf("Camsource " VERSION " starting up...\n");
	fflush(stdout);
	
	mod_init();
	
	ret = config_init(config);
	if (ret)
	{
		printf("No config file found, exit.\n");
		printf("If you've just installed or compiled, check out \"camsource.conf.example\",\n");
		printf("either located in " SYSCONFDIR " or in the source tree.\n");
		exit(1);
	}
	
	ret = config_load();
	if (ret)
	{
		printf("Failed to load config file %s, exit.\n", ourconfig);
		exit(1);
	}
	
	if (dump)
		logfd = -1;
	else
		logfd = log_open();
	
	mod_load_all();

	ret = grab_threads_init();
	if (!ret) {
		printf("No valid <camdev> sections found, exit\n");
		exit(1);
	}
	
	if (dump) {
		grab_dump_all();
		exit(0);
	}

	ret = grab_open_all();
	if (ret)
		exit(1);
	
	if (logfd >= 0)
		log_replace_bg(logfd);
	
	grab_start_all();
	mod_start_all();
}

static
int
kill_camsource(struct stat *sp)
{
	DIR *dp;
	struct dirent *de;
	char buf[1024];
	FILE *fp;
	int ret;
	DIR *fddp;
	struct dirent *fdde;
	int pid;
	int count;
	struct stat sb;
	int mypid;
	
	dp = opendir("/proc");
	if (!dp) {
		printf("Unable to open /proc (not mounted?): %s\n", strerror(errno));
		return -1;
	}
	
	mypid = getpid();
	
	count = 0;
	while ((de = readdir(dp))) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		pid = atoi(de->d_name);
		if (pid <= 0)
			continue;
		if (pid == mypid)
			continue;
		snprintf(buf, sizeof(buf) - 1, "/proc/%s/stat", de->d_name);
		fp = fopen(buf, "r");
		if (!fp)
			continue;
		ret = fscanf(fp, "%*i %32s", buf);
		fclose(fp);
		if (ret != 1)
			continue;
		if (strcmp(buf, "(camsource)"))
			continue;
		if (sp) {
			snprintf(buf, sizeof(buf) - 1, "/proc/%s/fd", de->d_name);
			fddp = opendir(buf);
			if (!fddp)
				continue;
			while ((fdde = readdir(fddp))) {
				if (!strcmp(fdde->d_name, ".") || !strcmp(fdde->d_name, ".."))
					continue;
				snprintf(buf, sizeof(buf) - 1, "/proc/%s/fd/%s", de->d_name, fdde->d_name);
				ret = stat(buf, &sb);
				if (ret < 0)
					continue;
				if (sb.st_dev == sp->st_dev
					&& (sb.st_mode & S_IFMT) == (sp->st_mode & S_IFMT)
					&& sb.st_rdev == sp->st_rdev)
					goto found;
			}
			closedir(fddp);
			continue;
found:
			closedir(fddp);
		}
		kill(pid, SIGTERM);
		kill(pid, SIGKILL);
		count++;
	}
	
	closedir(dp);
	
	printf("%i matching process(es)/thread(s) killed\n", count);
	return count;
}
