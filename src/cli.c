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
#include <stdbool.h>
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
"  -f, --format FORMAT\n"
"                     table, csv, xml, or md (default table). In\n"
"                     regeneration mode the default is md, and no other\n"
"                     format is accepted\n"
"      --from-xml FILE\n"
"                     rebuild a report from a record FILE previously\n"
"                     written with --format xml, without reading any\n"
"                     source file. TARGET is not given in this mode\n"
"  -c, --complexity-threshold N\n"
"                     list a file's functions whose cyclomatic complexity\n"
"                     is N or greater (default 15). Listing only: no\n"
"                     threshold affects the exit status\n"
"  -o, --output FILE  write the report to FILE instead of standard output\n"
"      --entry SYMBOL declare SYMBOL an entry point, from which call depth\n"
"                     is measured. Repeatable. Entry points are never\n"
"                     guessed at, so with none declared the analyses that\n"
"                     need them are omitted with a stated reason\n"
"      --graphml      also write the System Dependence Graph as GraphML,\n"
"                     beside the report and named from it: an --output of\n"
"                     report.md yields report.graphml. Requires --output,\n"
"                     since there is otherwise no name to derive\n"
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
	/* A value above any printable character, so a long-only option cannot
	 * collide with a short one. */
	enum { OPT_FROM_XML = 1000, OPT_GRAPHML, OPT_ENTRY };

	static const struct option longopts[] = {
		{ "format",               required_argument, NULL, 'f' },
		{ "from-xml",             required_argument, NULL, OPT_FROM_XML },
		{ "complexity-threshold", required_argument, NULL, 'c' },
		{ "output",               required_argument, NULL, 'o' },
		{ "graphml",              no_argument,       NULL, OPT_GRAPHML },
		{ "entry",                required_argument, NULL, OPT_ENTRY },
		{ "help",                 no_argument,       NULL, 'h' },
		{ NULL,                   0,                 NULL, 0   }
	};

	static const struct { const char *name; OutputFormat format; } formats[] = {
		{ "table", FORMAT_TABLE },
		{ "csv",   FORMAT_CSV   },
		{ "xml",   FORMAT_XML   },
		{ "md",    FORMAT_MARKDOWN }
	};

	bool format_given = false;

	memset(out, 0, sizeof(*out));
	out->mode                 = MODE_ANALYSE;
	out->format               = FORMAT_TABLE;
	out->complexity_threshold = ELC_DEFAULT_COMPLEXITY_THRESHOLD;

	/* Report errors ourselves so every diagnostic reaches stderr in one
	 * voice (HLR-038); getopt's own messages would bypass cli_usage(). */
	opterr = 0;
	optind = 1;

	int c;
	while ((c = getopt_long(argc, argv, ":ho:c:f:", longopts, NULL)) != -1) {
		switch (c) {
		case 'f': {
			size_t i;

			for (i = 0; i < sizeof formats / sizeof *formats; i++) {
				if (strcmp(optarg, formats[i].name) != 0)
					continue;
				out->format  = formats[i].format;
				format_given = true;
				break;
			}
			if (i == sizeof formats / sizeof *formats) {
				fprintf(stderr,
				        "elc: '%s' is not a format; expected "
				        "table, csv, xml, or md\n", optarg);
				return CLI_ERROR;
			}
			break;
		}
		case OPT_FROM_XML:
			out->mode       = MODE_REGENERATE;
			out->input_path = optarg;
			break;
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
		case OPT_ENTRY:
			if (out->entry_point_count == out->entry_point_capacity) {
				size_t       next = out->entry_point_capacity
				                            ? out->entry_point_capacity * 2
				                            : 8;
				const char **grown = realloc(out->entry_points,
				                             next * sizeof *grown);

				if (!grown) {
					fputs("elc: out of memory\n", stderr);
					return ELC_EXIT_FATAL;
				}
				out->entry_points         = grown;
				out->entry_point_capacity = next;
			}
			/* Borrowed from argv, which outlives the run; only the
			 * array holding them is owned. */
			out->entry_points[out->entry_point_count++] = optarg;
			break;
		case OPT_GRAPHML:
			/* Recorded, not validated against --output here. A
			 * request for GraphML with the report on stdout is not
			 * a usage error — it produces no companion, which is
			 * what HLR-106 says happens, and rejecting it would
			 * make `elc --graphml src/` fail where the requirement
			 * says it should simply not write a file. */
			out->graphml = true;
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

	if (out->mode == MODE_REGENERATE) {
		/* A saved record is the input, so a target would name a second
		 * source of truth for the same report. */
		if (optind < argc) {
			fprintf(stderr, "elc: --from-xml takes no target, but "
			        "'%s' was given\n", argv[optind]);
			return CLI_ERROR;
		}

		/* Markdown is the mode's output (HLR-055), and is therefore its
		 * default rather than a format the user must remember. Only an
		 * *explicit* choice of something else is an error — defaulting
		 * to table and then rejecting it would make the mode unusable
		 * without a redundant option (LLR-CLI-10). */
		if (!format_given) {
			out->format = FORMAT_MARKDOWN;
		} else if (out->format != FORMAT_MARKDOWN) {
			fputs("elc: --from-xml produces Markdown; no other "
			      "format can be regenerated from a saved record\n",
			      stderr);
			return CLI_ERROR;
		}

		return CLI_OK;
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
	/* The entry-point *array* is owned; the symbols in it are borrowed
	 * from argv, as the targets are (HLR-125). */
	free((void *)opts->entry_points);
	memset(opts, 0, sizeof(*opts));
}
