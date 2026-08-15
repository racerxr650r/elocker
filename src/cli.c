/* cli.c — command-line parsing and validation.
 *
 * The only reader of argv. Produces an immutable ElcOptions that every later
 * stage treats as read-only.
 *
 * Phase 0 implements the options that are honoured today: the help request
 * and the target list. Options that select behaviour no phase has built yet
 * are deliberately absent rather than parsed-and-ignored, so that `--help`,
 * the man page, and the user manual describe only what elc actually does
 * (HLR-129). Later phases add each option alongside the behaviour it selects.
 */

#include <errno.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "elc.h"

void cli_usage(FILE *stream)
{
	fputs(
"Usage: elc [OPTION]... TARGET...\n"
"\n"
"Report per-function code metrics and whole-project architecture analysis\n"
"for the given targets. A TARGET is a source file or a directory.\n"
"\n"
"Options:\n"
"  -c, --complexity-threshold N\n"
"                     list a file's functions whose cyclomatic complexity\n"
"                     is N or greater (default 15). Listing only: no\n"
"                     threshold affects the exit status\n"
"  -o, --output FILE  write the report to FILE instead of standard output\n"
"  -h, --help         display this help and exit\n"
"\n"
"Output:\n"
"  An aligned table of the discovered files and their physical line counts,\n"
"  preceded by the project totals. Files are listed by canonical absolute\n"
"  path, in ascending byte order, each exactly once however many targets\n"
"  reach it.\n"
"\n"
"Exit status:\n"
"  0  every discovered file was processed, or skipped for want of a\n"
"     language module\n"
"  1  the run completed and produced a report, but at least one file\n"
"     failed to be read or parsed\n"
"  2  the run did not complete: a usage error, an invalid target, or a\n"
"     fatal runtime-location failure\n"
"\n"
"Full documentation: elc(1), and doc/User_Manual.md in the distribution.\n",
	      stream);
}

int cli_parse(int argc, char *argv[], ElcOptions *out)
{
	static const struct option longopts[] = {
		{ "complexity-threshold", required_argument, NULL, 'c' },
		{ "output",               required_argument, NULL, 'o' },
		{ "help",                 no_argument,       NULL, 'h' },
		{ NULL,                   0,                 NULL, 0   }
	};

	memset(out, 0, sizeof(*out));
	out->mode                 = MODE_ANALYSE;
	out->complexity_threshold = ELC_DEFAULT_COMPLEXITY_THRESHOLD;

	/* Report errors ourselves so every diagnostic reaches stderr in one
	 * voice (HLR-038); getopt's own messages would bypass cli_usage(). */
	opterr = 0;
	optind = 1;

	int c;
	while ((c = getopt_long(argc, argv, ":ho:c:", longopts, NULL)) != -1) {
		switch (c) {
		case 'c': {
			/* strtoul accepts leading whitespace, a sign, and a
			 * trailing tail; none of those is a threshold. The
			 * argument must be digits and nothing else. */
			char         *end  = NULL;
			unsigned long value;

			errno = 0;
			value = strtoul(optarg, &end, 10);

			if (optarg[0] < '0' || optarg[0] > '9' || !end ||
			    *end != '\0' || errno == ERANGE || value > UINT32_MAX) {
				fprintf(stderr,
				        "elc: '%s' is not a complexity threshold\n",
				        optarg);
				return CLI_ERROR;
			}
			out->complexity_threshold = (uint32_t)value;
			break;
		}
		case 'o':
			/* Borrowed from argv, which outlives the run. Recording
			 * standard output as the destination is the default:
			 * output_path stays NULL (LLR-CLI-03). */
			out->output_path = optarg;
			break;
		case 'h':
			cli_usage(stdout);
			return CLI_HELP;
		case ':':
			fprintf(stderr, "elc: option '%s' requires an argument\n",
			        argv[optind - 1]);
			return CLI_ERROR;
		case '?':
		default:
			if (optopt)
				fprintf(stderr, "elc: unrecognised option '-%c'\n",
				        optopt);
			else
				fprintf(stderr, "elc: unrecognised option '%s'\n",
				        argv[optind - 1]);
			return CLI_ERROR;
		}
	}

	if (optind >= argc) {
		fputs("elc: no target given\n", stderr);
		return CLI_ERROR;
	}

	/* Targets are borrowed from argv, which outlives the run. */
	out->targets      = (const char **)&argv[optind];
	out->target_count = (size_t)(argc - optind);

	return CLI_OK;
}

void cli_options_free(ElcOptions *opts)
{
	if (!opts)
		return;
	/* Phase 0 owns no heap allocation: targets are borrowed from argv.
	 * The function exists so that main()'s teardown path is complete from
	 * the outset and later phases have somewhere to add releases
	 * (HLR-125). */
	memset(opts, 0, sizeof(*opts));
}
