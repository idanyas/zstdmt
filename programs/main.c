
/**
 * Copyright © 2016 - 2017 Tino Reichardt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You can contact the author at:
 * - zstdmt source repository: https://github.com/mcmilk/zstdmt
 */

/**
 * gzip compatible wrapper for the compression types of this library
 */

#include <alloca.h>
#include <stdio.h>
#include <errno.h>

#include "platform.h"

/* exit codes */
#define E_OK      0
#define E_ERROR   1
#define E_WARNING 2
static int exit_code = E_OK;

#define MODE_COMPRESS    1	/* -z (default) */
#define MODE_DECOMPRESS  2	/* -d */
#define MODE_LIST        3	/* -l */
#define MODE_TEST        4	/* -t */

/* for the -i option */
#define MAX_ITERATIONS   1000

static int opt_mode = MODE_COMPRESS;
static int opt_stdout = 0;
static int opt_level = 3;
static int opt_force = 0;
static int opt_keep = 1;	// XXX
static int opt_threads;

/* 0 = quiet | 1 = normal | >1 = verbose */
static int opt_verbose = 1;
static int opt_iterations = 1;
static int opt_bufsize = 0;
static int opt_timings = 0;

static char *progname;
static char *opt_suffix = SUFFIX;
static const char *errmsg = 0;

/* pointer to current infile, outfile and /dev/null  */
static FILE *fin = NULL;
static FILE *fout = NULL;
static size_t bytes_read = 0;
static size_t bytes_written = 0;

/* when set, do not change fout */
static int global_fout = 0;

MT_CCtx *cctx = 0;
MT_DCtx *dctx = 0;

static void panic(const char *msg)
{
	if (opt_verbose)
		fprintf(stderr, "%s\n", msg);
	fflush(stdout);
	exit(1);
}

static void version(void)
{
	printf(PROGNAME " version " VERSION "\n");
	exit(0);
}

static void license(void)
{
	printf
	    ("Copyright (c) 2016 - 2017, Tino Reichardt, All rights reserved.\n");
	printf("License: BSD License\n");
	exit(0);
}

static void usage(void)
{
	printf("Usage: " PROGNAME " [options] INPUT > FILE\n");
	printf("or     " PROGNAME " [options] -o FILE INPUT\n");
	printf("or     cat INPUT | " PROGNAME " [options] -o FILE\n");
	printf("or     cat INPUT | " PROGNAME " [options] > FILE\n\n");

	printf("Gzip/Bzip2 Like Options:\n");
	printf(" -#       Set compression level to # (%d-%d, default:%d).\n",
	       LEVEL_MIN, LEVEL_MAX, LEVEL_DEF);
	printf(" -c       Force write to standard output.\n");
	printf(" -d       Use decompress mode.\n");
	printf(" -z       Use compress mode.\n");
	printf(" -f       Force overwriting files and/or compression.\n");
	printf(" -h       Display a help screen and quit.\n");
	printf
	    (" -k       Keep input files after compression or decompression.\n");
	printf
	    (" -l       List information for the specified compressed files.\n");
	printf(" -L       Display License and quit.\n");
	printf(" -q       Be quiet: suppress all messages.\n");
	printf
	    (" -S SUF   Use suffix `SUF` for compressed files. Default: \"%s\"\n",
	     SUFFIX);
	printf
	    (" -t       Test the integrity of each file leaving any files intact.\n");
	printf(" -v       Be more verbose.\n");
	printf(" -V       Show version information and quit.\n\n");

	printf("Additional Options:\n");
	printf
	    (" -T N     Set number of (de)compression threads (def: #cores).\n");
	printf(" -b N     Set input chunksize to N MiB (default: auto).\n");
	printf
	    (" -i N     Set number of iterations for testing (default: 1).\n");
	printf(" -H       Print headline for the timing values and quit.\n");
	printf(" -B       Print timings and memory usage to stderr.\n");

	exit(0);
}

static void headline(void)
{
	fprintf(stderr,
		"Level;Threads;InSize;OutSize;Frames;Real;User;Sys;MaxMem\n");
	exit(0);
}

static int ReadData(void *arg, MT_Buffer * in)
{
	FILE *fd = (FILE *) arg;
	size_t done = fread(in->buf, 1, in->size, fd);
	in->size = done;
	bytes_read += done;

	return 0;
}

static int WriteData(void *arg, MT_Buffer * out)
{
	FILE *fd = (FILE *) arg;
	ssize_t done = fwrite(out->buf, 1, out->size, fd);
	out->size = done;
	bytes_written += done;

	return 0;
}

/**
 * compress() - compress data from fin to fout
 *
 * return: 0 for ok, or errmsg on error
 */
static const char *do_compress(FILE * in, FILE * out)
{
	static int first = 1;
	MT_RdWr_t rdwr;
	size_t ret;

	/* input or output not okay */
	if (errmsg)
		return errmsg;

	/* 1) setup read/write functions */
	rdwr.fn_read = ReadData;
	rdwr.fn_write = WriteData;
	rdwr.arg_read = (void *)in;
	rdwr.arg_write = (void *)out;

	/* 2) create compression context */
	if (!cctx)
		cctx = MT_createCCtx(opt_threads, opt_level, opt_bufsize);
	if (!cctx)
		return "Allocating compression context failed!";

	/* 3) compress */
	ret = MT_compressCCtx(cctx, &rdwr);
	if (MT_isError(ret))
		return MT_getErrorString(ret);

	/* 4) get statistic - xxx */
	if (first && opt_timings) {
		fprintf(stderr, "%d;%d;%lu;%lu;%lu",
			opt_level, opt_threads,
			(unsigned long)MT_GetInsizeCCtx(cctx),
			(unsigned long)MT_GetOutsizeCCtx(cctx),
			(unsigned long)MT_GetFramesCCtx(cctx));
		first = 0;
	}

	return 0;
}

/**
 * decompress() - decompress data from fin to fout
 *
 * return: 0 for ok, or errmsg on error
 */
static const char *do_decompress(FILE * in, FILE * out)
{
	static int first = 1;
	MT_RdWr_t rdwr;
	size_t ret;

	/* input or output not okay */
	if (errmsg)
		return errmsg;

	/* 1) setup read/write functions */
	rdwr.fn_read = ReadData;
	rdwr.fn_write = WriteData;
	rdwr.arg_read = (void *)in;
	rdwr.arg_write = (void *)out;

	/* 2) create compression context */
	if (!dctx)
		dctx = MT_createDCtx(opt_threads, opt_bufsize);
	if (!dctx)
		return "Allocating decompression context failed!";

	/* 3) compress */
	ret = MT_decompressDCtx(dctx, &rdwr);
	if (MT_isError(ret))
		return MT_getErrorString(ret);

	/* 4) get statistic - xxx */
	if (first && opt_timings) {
		fprintf(stderr, "%d;%d;%lu;%lu;%lu",
			0, opt_threads,
			(unsigned long)MT_GetInsizeDCtx(dctx),
			(unsigned long)MT_GetOutsizeDCtx(dctx),
			(unsigned long)MT_GetFramesDCtx(dctx));
		first = 0;
	}

	return 0;
}

/* free resources, used for compression/decompression */
static void compress_cleanup(void)
{
	if (cctx) {
		MT_freeCCtx(cctx);
		cctx = 0;
	}

	if (dctx) {
		MT_freeDCtx(dctx);
		dctx = 0;
	}
}

static int has_suffix(const char *filename, const char *suffix)
{
	int flen = strlen(filename);
	int xlen = strlen(suffix);

	if (flen < xlen)
		return 0;

	if (strcmp(filename + flen - xlen, suffix) == 0)
		return 1;

	return 0;
}

static void add_suffix(const char *filename, char *newname)
{
	newname[0] = 0;
	strcat(newname, filename);
	strcat(newname, opt_suffix);

	return;
}

static void remove_suffix(const char *filename, char *newname)
{
	int flen = strlen(filename);
	int xlen = strlen(opt_suffix);

	if (has_suffix(filename, opt_suffix)) {
		/* just remove the suffix */
		strcpy(newname, filename);
		newname[flen - xlen] = 0;
	} else {
		/* append .out to filename */
		newname[0] = 0;
		strcat(newname, opt_suffix);
		strcat(newname, ".out");
	}

	return;
}

/**
 * Maybe TODO, -l -v should also these:
 * method crc date time
 */
static void print_listmode(int headline, const char *filename)
{
	if (headline)
		printf("%20s %20s %7s %s\n",
		       "compressed", "uncompressed", "ratio",
		       "uncompressed_name");

	if (errmsg) {
		printf("%20s %20s %7s %s\n", "-", "-", "-", filename);
	} else {
		printf("%20lu %20lu %6.2f%% %s\n",
		       (unsigned long)bytes_read,
		       (unsigned long)bytes_written,
		       100 - (double)bytes_read * 100 / bytes_written,
		       filename);
	}
}

static void print_testmode(const char *filename)
{
	printf(PROGNAME ": %s: %s\n", filename, errmsg ? errmsg : "OK");
}

/**
 * check_overwrite() - check if file exists
 *
 * return:
 * 0 -> do not create new file
 * 1 -> can create new file
 */
static int check_overwrite(const char *filename)
{
	int c, yes = -1;
	FILE *f = fopen(filename, "r");

	/* no file there, we can create a new one */
	if (f == NULL && errno == ENOENT)
		return 1;

	/* file there, but can not open?! */
	if (f == NULL)
		return 0;

	fclose(f);

	/* when we are here, we ask the user what to do */
	for (;;) {
		printf(PROGNAME
		       ": `%s` already exists. Overwrite (y/N) ? ",
		       filename);
		c = getchar();

		if (c == 'y' || c == 'Y')
			yes = 1;
		if (c == 'n' || c == 'N')
			yes = 0;

		if (yes != -1)
			break;

		while (c != '\n' && c != EOF)
			c = getchar();
	}

	return yes;
}

static void treat_stdin()
{
	const char *filename = "(stdin)";

	/* setup fin and fout */
	fin = stdin;
	if (!global_fout)
		fout = stdout;

	/* do some work */
	if (opt_mode == MODE_COMPRESS)
		errmsg = do_compress(fin, fout);
	else
		errmsg = do_decompress(fin, fout);

	/* remember, that we had some error */
	if (errmsg)
		exit_code = E_ERROR;

	/* listing mode */
	if (opt_mode == MODE_LIST)
		print_listmode(1, filename);

	/* testing mode */
	if (opt_mode == MODE_TEST && opt_verbose > 1)
		print_testmode(filename);

	return;
}

static void treat_file(char *filename)
{
	FILE *local_fout = NULL;
	int fn2len = strlen(filename) + 10;
	char *fn2 = alloca(fn2len + 1);
	static int first = 1;

	if (!fn2)
		panic("Out of memory!");

	/* reset counter */
	if (opt_mode == MODE_LIST)
		bytes_written = bytes_read = 0;

	/* reset errmsg */
	errmsg = 0;

	fin = fopen(filename, "rb");
	if (global_fout) {
		local_fout = fout;
	} else {
		/* setup input / output */
		switch (opt_mode) {
		case MODE_COMPRESS:
			add_suffix(filename, fn2);
			if (check_overwrite(fn2) == 0) {
				fprintf(stderr, "Skipping %s...\n", fn2);
				exit_code = E_WARNING;
				return;
			}
			local_fout = fopen(fn2, "wb");
			break;
		case MODE_DECOMPRESS:
			remove_suffix(filename, fn2);
			if (check_overwrite(fn2) == 0) {
				fprintf(stderr, "Skipping %s...\n", fn2);
				exit_code = E_WARNING;
				return;
			}
			local_fout = fopen(fn2, "wb");
			break;
		}
	}

	if (fin == NULL)
		errmsg = "Opening infile failed.";

	if (local_fout == NULL)
		errmsg = "Opening outfile failed.";

	/* do some work */
	if (opt_mode == MODE_COMPRESS)
		errmsg = do_compress(fin, local_fout);
	else
		errmsg = do_decompress(fin, local_fout);

	/* remember, that we had some error */
	if (errmsg)
		exit_code = E_ERROR;

	/* close instream */
	if (fin && fin != stdin)
		if (fclose(fin) != 0 && opt_verbose)
			fprintf(stderr, "Closing infile failed.");

	/* close outstream */
	if (!global_fout)
		if (local_fout && fclose(local_fout) != 0 && opt_verbose)
			fprintf(stderr, "Closing outfile failed.");

	/* listing mode */
	if (opt_mode == MODE_LIST)
		print_listmode(first, filename);

	/* testing mode */
	if (opt_mode == MODE_TEST && opt_verbose > 1)
		print_testmode(filename);

	/* remove input file */
	if (!errmsg && !opt_keep)
		remove(filename);

	first = 0;

	return;
}

int main(int argc, char **argv)
{
	/* default options: */
	struct rusage ru;
	struct timeval tms, tme, tm;
	int opt;		/* for getopt */
	int files;		/* number of files in cmdline */
	int levelnumbers = 0;

	/* get programm name */
	progname = strrchr(argv[0], '/');
	if (progname)
		++progname;
	else
		progname = argv[0];

	/* change defaults, if needed */
	if (strcmp(progname, UNZIP) == 0) {
		opt_mode = MODE_DECOMPRESS;
	} else if (strcmp(progname, ZCAT) == 0) {
		opt_mode = MODE_DECOMPRESS;
		opt_stdout = 1;
		opt_force = 1;
	}

	/* default: thread count = # cpu's */
	opt_threads = getcpucount();

	/* same order as in help option -h */
	while ((opt =
		getopt(argc, argv,
		       "1234567890cdzfhklLqrS:tvVT:b:i:HB")) != -1) {
		switch (opt) {

			/* 1) Gzip Like Options: */
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			if (levelnumbers == 0)
				opt_level = 0;
			else
				opt_level *= 10;
			opt_level += ((int)opt - 48);
			levelnumbers++;
			break;

		case 'c':	/* force to stdout */
			opt_stdout = 1;
			break;

		case 'd':	/* mode = decompress */
			opt_mode = MODE_DECOMPRESS;
			break;

		case 'z':	/* mode = compress */
			opt_mode = MODE_COMPRESS;
			break;

		case 'f':	/* force overwriting */
			opt_force = 1;
			break;

		case 'h':	/* show help */
			usage();
			/* not reached */

		case 'k':	/* keep old files */
			opt_keep = 1;
			break;

		case 'l':	/* listing */
			opt_mode = MODE_LIST;
			opt_keep = 1;
			break;

		case 'L':	/* show License */
			license();
			/* not reached */

		case 'q':	/* be quiet */
			opt_verbose = 0;
			break;

		case 'S':	/* use specified suffix */
			opt_suffix = optarg;
			break;

		case 't':	/* testing */
			opt_mode = MODE_TEST;
			opt_keep = 1;
			break;

		case 'v':	/* be more verbose */
			opt_verbose++;
			break;

		case 'V':	/* version */
			version();
			/* not reached */

			/* 2) additional options */
		case 'T':	/* threads */
			opt_threads = atoi(optarg);
			break;

		case 'b':	/* input buffer in MB */
			opt_bufsize = atoi(optarg);
			break;

		case 'i':	/* iterations */
			opt_iterations = atoi(optarg);
			break;

		case 'H':	/* headline */
			headline();
			/* not reached */

		case 'B':	/* print timings */
			opt_timings = 1;
			break;

		default:
			usage();
			/* not reached */
		}
	}

	/**
	 * generic check of parameters
	 */

	/* make opt_level valid */
	if (opt_level < LEVEL_MIN)
		opt_level = LEVEL_MIN;
	else if (opt_level > LEVEL_MAX)
		opt_level = LEVEL_MAX;

	/* opt_threads = 1..THREAD_MAX */
	if (opt_threads < 1)
		opt_threads = 1;
	else if (opt_threads > THREAD_MAX)
		opt_threads = THREAD_MAX;

	/* opt_iterations = 1..MAX_ITERATIONS */
	if (opt_iterations < 1)
		opt_iterations = 1;
	else if (opt_iterations > MAX_ITERATIONS)
		opt_iterations = MAX_ITERATIONS;

	/* opt_bufsize is in MiB */
	if (opt_bufsize > 0)
		opt_bufsize *= 1024 * 1024;

	/* number of args, which are not options */
	files = argc - optind;

	/* -c was used */
	if (opt_stdout) {
		if (IS_CONSOLE(stdout) && !opt_force)
			usage();
		fout = stdout;
		global_fout = 1;
	}

	/* -l or -t given, then we write to /dev/null */
	if (opt_mode == MODE_LIST || opt_mode == MODE_TEST) {
		fout = fopen("/dev/null", "wb");
		if (!fout)
			panic("Opening output file failed!");
		global_fout = 1;
	}

	/* begin timing */
	if (opt_timings)
		gettimeofday(&tms, NULL);

	/* main work */
	if (files == 0) {
		if (opt_iterations != 1)
			panic
			    ("You can not use stdin together with the -i option.");

		/* use stdin */
		treat_stdin();
	} else {
		/* use input files */
		for (;;) {
			files = optind;
			while (files < argc) {
				treat_file(argv[files++]);
			}
			opt_iterations--;
			if (opt_iterations == 0)
				break;
		}
	}

	/* show timings */
	if (opt_timings) {
		gettimeofday(&tme, NULL);
		timersub(&tme, &tms, &tm);
		getrusage(RUSAGE_SELF, &ru);
		fprintf(stderr, ";%ld.%ld;%ld.%ld;%ld.%ld;%ld\n",
			tm.tv_sec, tm.tv_usec / 1000,
			ru.ru_utime.tv_sec, ru.ru_utime.tv_usec / 1000,
			ru.ru_stime.tv_sec, ru.ru_stime.tv_usec / 1000,
			(long unsigned)ru.ru_maxrss);
	}

	/* free ressources */
	compress_cleanup();

	/* exit should flush stdout */
	exit(exit_code);
}
