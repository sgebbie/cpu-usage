/*
 * Stewart Gebbie - 2021-2022
 *
 * Calculate the CPU usage and display it.
 */
#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>

#define USE_MICRO_SECONDS 1
#define LOG_DEBUG 0

/*
U+2581 LOWER ONE EIGHTH BLOCK
UTF-8: e2 96 81 UTF-16BE: 2581 Decimal: &#9601; Octal: \022601
▁

...

U+2587 LOWER SEVEN EIGHTHS BLOCK
UTF-8: e2 96 87 UTF-16BE: 2587 Decimal: &#9607; Octal: \022607
▇

U+2588 FULL BLOCK
UTF-8: e2 96 88 UTF-16BE: 2588 Decimal: &#9608; Octal: \022610
█
*/

// For an alternative and more compact display we could swith to using
// unicode Braille symbols. However, the while you could encode two
// measurements per character, the vertical resolution is half that
// of the blocks.
//
// See: U+28FF BRAILLE PATTERN DOTS-12345678

// Future work:
//   - extend to support network via: cat /proc/net/dev|grep '^wlp2s0'

#define U_PER_S 1000000
#define MAX_PATH 4096

int count_cpus(int proc_stat);
int daemonise(bool change_to_root);

int main(int argc, char** argv) {
	const char* const parsing_failed = "parsing failed";
	char usage_file_path[MAX_PATH];
	char spot_file_path[MAX_PATH];
	unsigned int usage_length = 20;

	bool use_background = true;
	bool show_cpu_count = false;
	bool show_clock_tck = false;

	// collect clock tick length
	const long clk_tck = sysconf(_SC_CLK_TCK);
#if USE_MICRO_SECONDS
	const long long clk_tck_u = U_PER_S / clk_tck; // number of µs per tick
#endif

	int ret = -1;
	int argp = 1;

	// configure parameters
	char * e = NULL;
	// .. pause
	unsigned int mpause = 1050;
	if (argc > argp) {
		mpause = strtoul(argv[argp], &e, 10); if (e <= argv[argp]) { perror(parsing_failed); return -1; }
		argp++;
	}
	const unsigned int pause = mpause / 1000; // seconds
	const useconds_t upause = mpause * 1000; // microseconds

	// .. graph length
	if (argc > argp) {
		usage_length = strtoul(argv[argp], &e, 10); if (e <= argv[argp]) { perror(parsing_failed); return -1; }
		argp++;
	}

	while (argp < argc) {
		// .. background usage
		switch (argv[argp][0]) {
			case 'f': use_background = false; break;
			case 'c': show_cpu_count = true; break;
			case 't': show_clock_tck = true; break;
		}
		argp++;
	}

	// test characters
#if LOG_DEBUG
	for (int x = 0x81; x <= 0x88; x++) {
		printf("%c%c%c", 0xe2, 0x96, x);
	}
	printf("\n");
#endif

	// declare enough space for 10 longs formatted as decimals, along with a prefix.
	// this will be used to read in from /proc/stat
	char buf[220];

	// create our graph and zero to spaces
	char spot[10];
	int ul = (usage_length < 1 ? 1 : usage_length);
	char graph[3 * ul];
	uint8_t graph_width[ul]; // introduce characater width tracking (to aid in eliding zero characters)
	for (int i = 0; i < ul; i++) {
		int j = i*3;
		graph[j] = graph[j+1] = 0x00;
		graph[j+2] = 0x20;
		graph_width[i] = 1;
	}

#if LOG_DEBUG
	printf("clk_tck=%ld\n", clk_tck);
#endif

	// open the input statistics
	const int proc_stat = open("/proc/stat", O_RDONLY);
	if (proc_stat < 0) {
		perror("stat open failed");
		return -1;
	}

	// set up the output path
	// -- graph file
	int gf = snprintf(usage_file_path, sizeof(usage_file_path), "%s/.cpu-usage", getenv("HOME"));
	if (gf >= sizeof(usage_file_path)) {
		perror("usage file name too long");
		return -1;
	}
	const int cpu_file = open(usage_file_path, O_WRONLY | O_CREAT, S_IWUSR | S_IRUSR);
	if (cpu_file < 0) {
		perror("output open failed");
		return -1;
	}
	int r = ftruncate(cpu_file, 0);
	if (r < 0) {
		perror("output truncate failed");
		return -1;
	}	
	// -- spot file
	gf = snprintf(spot_file_path, sizeof(spot_file_path), "%s/.cpu-usage.spot", getenv("HOME"));
	if (gf >= sizeof(spot_file_path)) {
		perror("spot file name too long");
		return -1;
	}
	const int spot_file = open(spot_file_path, O_WRONLY | O_CREAT, S_IWUSR | S_IRUSR);
	if (spot_file < 0) {
		perror("spot output open failed");
		return -1;
	}
	r = ftruncate(spot_file, 0);
	if (r < 0) {
		perror("spot output truncate failed");
		return -1;
	}	

	const int cpu_count = count_cpus(proc_stat);
#if LOG_DEBUG
	printf("cpu_count=%d\n", cpu_count);
#endif
	if (cpu_count <= 0) {
		perror("failed to count cpus");
		return -1;
	}

	struct timeval t_a = { .tv_sec = 0, .tv_usec = 0 };
	struct timeval t_b = { .tv_sec = 0, .tv_usec = 0 };

	long work_a = 0;
	long work_b = 0;

	// double buffer to flip
	struct timeval * t_p = &t_a;
	long * work_p = &work_a;

	// show diagnostics before going into the background
	if (show_cpu_count) printf("cpu_count=%d\n", cpu_count);
	if (show_clock_tck) printf("clk_tck=%ld\n", clk_tck);

	if (use_background) daemonise(false);

	int count = 0;
	while(1) {
		// flip the buffers
		if (t_p == &t_a) {
			t_p = &t_b;
			work_p = &work_b;
		} else {
			t_p = &t_a;
			work_p = &work_a;
		}

		// pause
#if USE_MICRO_SECONDS
		usleep(upause);
#else
		sleep(pause);
#endif

		// record time
		int t = gettimeofday(t_p, NULL);
		if (t != 0) { perror("get time"); ret = -2; goto fin; }

		// take measurements
		off_t o = lseek(proc_stat, 0, 0);
		if (o == (off_t) -1) {
			perror("seek failed");
			break;
		}
		ssize_t r = read(proc_stat, buf, sizeof(buf)-1);
		const char * p = buf;
		// skip over 'cpu...'
		p = strchr(buf, ' ');
		if (!p) break;
		char * e = NULL;
		long user = strtol(p, &e, 10); if (e <= p) { perror(parsing_failed); break; }
		long nice = strtol(e, &e, 10); if (e <= p) { perror(parsing_failed); break; }
		long system = strtol(e, &e, 10); if (e <= p) { perror(parsing_failed); break; }
		long idle = strtol(e, &e, 10); if (e <= p) { perror(parsing_failed); break; }
		long iowait = strtol(e, &e, 10); if (e <= p) { perror(parsing_failed); break; }
		long irq = strtol(e, &e, 10); if (e <= p) { perror(parsing_failed); break; }
		long softirq = strtol(e, &e, 10); if (e <= p) { perror(parsing_failed); break; }

		// calculate
		long work = user + nice + system + irq + softirq; // measured in clock ticks
#if LOG_DEBUG
		printf("cpu_count = %d user = %ld nice=%ld system=%ld irq=%ld softirq=%ld => work=%ld\n",
				cpu_count,
				user, nice, system, irq, softirq, work);
#endif

		// record work
		*work_p = work;

		// calculate diff
		// (this relies on the "double negatives" cancelling out in the division)
		long work_d = work_a - work_b;
		long cpu_perc = 0;
		int cpu_block = 0;

#if USE_MICRO_SECONDS
		long long t_a_micro = (long long)(t_a.tv_sec)*U_PER_S + (long long)t_a.tv_usec;
		long long t_b_micro = (long long)(t_b.tv_sec)*U_PER_S + (long long)t_b.tv_usec;
		long long t_d_micro = t_a_micro - t_b_micro;
		long long full_ticks_d_micro = (t_d_micro / clk_tck_u) * cpu_count;
#if LOG_DEBUG
		printf("work_d = %ld t_d_micro = %lld full_ticks_d_micro = %lld\n",
				work_d, t_d_micro, full_ticks_d_micro);

		printf("clk_tck_u = %lld cpu_count = %d\n",
				clk_tck_u, cpu_count);
#endif

		if (full_ticks_d_micro == 0) {
#if LOG_DEBUG
			printf("warn - no time measurement (micro sec)\n");
#endif
			continue;
		}
		cpu_perc = (work_d * 100) / (full_ticks_d_micro);
		cpu_block = (work_d * 9) / (full_ticks_d_micro);
#else
		time_t t_d = t_a.tv_sec - t_b.tv_sec; // TODO include microseconds
		long full_ticks_d = clk_tck * t_d * cpu_count;
		printf("work_d = %ld t_d = %ld full_ticks_d = %ld\n",
				work_d, t_d, full_ticks_d);

		if (full_ticks_d == 0) {
#if LOG_DEBUG
			printf("warn - no time measurement (sec)\n");
#endif
			continue;
		}
		cpu_perc = (work_d * 100) / (full_ticks_d);
		cpu_block = (work_d * 9) / (full_ticks_d);
#endif
		// unicode 2500..2590
		int cpu_char_a = 0xe2;
		int cpu_char_b = 0x96;
		int cpu_char_c = 0x20;
		if (cpu_block <= 0) {
			cpu_char_a = 0x00;
			cpu_char_b = 0x00;
			cpu_char_c = 0x5f;
		}
		if (cpu_block > 0) cpu_char_c = 0x80 + cpu_block;
		if (cpu_block >= 9) cpu_char_c = 0x93;

		count = usage_length ? (count + 1) % usage_length  : count + 1;
		if (!use_background) {
#if 0
			if (cpu_char_a == 0 && cpu_char_b == 0) printf("%c", cpu_char_c);
			else if (cpu_char_a == 0 && cpu_char_b != 0) printf("%c%c", cpu_char_b, cpu_char_c);
			else printf("%c%c%c", cpu_char_a, cpu_char_b, cpu_char_c);
#endif
			// note, we cheat and simply output NUL when we want and standard ASCII character
			// this keeps our buffer using 3 bytes per character.
			printf("%c%c%c", cpu_char_a, cpu_char_b, cpu_char_c);
			if(usage_length && !count) {
				printf("\n");
			}
			fflush(stdout);
		}

#if LOG_DEBUG
		if (!count) {
			printf("\n%ld %d %c%c%c\n", cpu_perc, cpu_block, cpu_char_a, cpu_char_b, cpu_char_c);
		}
#endif

		// update the graph
		memmove(graph, graph + 3, sizeof(graph) - 3);
		graph[sizeof(graph) - 1] = cpu_char_c;
		graph[sizeof(graph) - 2] = cpu_char_b;
		graph[sizeof(graph) - 3] = cpu_char_a;
		graph_width[sizeof(graph_width) - 1] = (cpu_char_a ? 1 : 0) + (cpu_char_b ? 1 : 0) + (cpu_char_c ? 1 : 0);

		if (use_background) {
			// output the graph
			off_t go = lseek(cpu_file, 0, 0);
			if (go == (off_t) -1) {
				perror("graph seek failed");
				break;
			}
			int gw = write(cpu_file, graph, sizeof(graph));
			if (gw != sizeof(graph)) {
				perror("graph write failed");
				break;
			}

			// ouput the spot
			int spot_len = snprintf(spot, sizeof(spot), "%ld", cpu_perc);
#if LOG_DEBUG
			printf("spot_len=%d\n", spot_len);
#endif
			int st = ftruncate(spot_file, spot_len);
			if (st < 0) {
				perror("spot truncate failed");
				break;
			}
			off_t so = lseek(spot_file, 0, 0);
			if (so == (off_t) -1) {
				perror("spot seek failed");
				break;
			}
			int sw = write(spot_file, spot, spot_len);
			if (sw != spot_len) {
				perror("spot write failed");
				break;
			}
		}

#if LOG_DEBUG
		{
			int spot_len = snprintf(spot, sizeof(spot), "%ld", cpu_perc);
			printf("spot_len=%d\n", spot_len);
			int sw = write(2, spot, spot_len);
		}
#endif
	}

	ret = 0;

fin:

	// finish up...
	close(proc_stat);

	return ret;
}

int count_cpus(int proc_stat) {
	// count the occurences of 'cpu?'
	off_t o = lseek(proc_stat, 0, 0);
	if (o == (off_t) -1) {
		perror("seek failed");
		return -1;
	}
	char c;
	ssize_t r;
	int s = 0; // search state: 0 = expect 'c', 1 = expect 'p', 2 = expect 'u', 3 = expect [0-9]
	int count = 0; // incremented each time s == 3 and a digit is read.

   	for (r = read(proc_stat, &c, 1); r == 1; r = read(proc_stat, &c, 1)) {
		switch(s) {
			case 0: if (c == 'c') { s++; } else { s = 0; } break;
			case 1: if (c == 'p') { s++; } else { s = 0; } break;
			case 2: if (c == 'u') { s++; } else { s = 0; } break;
			case 3: if (c >= '0' && c <= '9') { count++; } s = 0; break;
			default:
				return -1;
		}
	}
	return count;
}

/**
    Create a normal process (Parent process)
    Create a child process from within the above parent process
    The process hierarchy at this stage looks like:
		TERMINAL -> PARENT PROCESS -> CHILD PROCESS
    Terminate the the parent process.
    The child process now becomes orphan and is taken over by the init process.
    Call setsid() function to run the process in new session and have a new group.
    After the above step we can say that now this process becomes a daemon process
		without having a controlling terminal.
    Change the working directory of the daemon process to root
		and close stdin, stdout and stderr file descriptors.
    Let the main logic of daemon process run.

	See: https://www.thegeekstuff.com/2012/02/c-daemon-process/
*/
int daemonise(bool change_to_root) {
	FILE *fp= NULL;
	pid_t process_id = 0;
	pid_t sid = 0;

	// create child process
	process_id = fork();
	// indication of fork() failure
	if (process_id < 0)
	{
		perror("fork failed!\n");
		exit(1);
	}

	// PARENT PROCESS. Need to kill it.
	if (process_id > 0)
	{
		printf("cpu-usage running now in the background: %d\n", process_id);
		// return success in exit status
		exit(0);
	}

	// -- set up new session etc.

	// unmask the file mode
	umask(0);
	// set new session
	sid = setsid();
	if(sid < 0)
	{
		perror("setsid failed");
		// return failure
		exit(1);
	}
	// change the current working directory to root.
	if (change_to_root) {
		chdir("/");
	}
	// close stdin. stdout and stderr
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	// we don't open new "stdout"...

	return 0;
}

