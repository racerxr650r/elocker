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

/* Parse one `name:glob[,glob…]` execution-scope declaration (LLR-SCP-01).
 *
 * Every string is copied. The declaration is split on two separators, so
 * neither the name nor any pattern exists as a NUL-terminated substring of
 * argv — the entry points can be borrowed because a symbol is the whole
 * argument, and a scope cannot.
 *
 * Returns 0, or -1 after a diagnostic for a declaration that cannot be parsed:
 * a scope with no name, or a name with no components, is a usage error rather
 * than a silently empty scope that would match nothing and report nothing
 * (HLR-063, LLR-SCP-02).
 */
int parse_scope(const char *arg, ElcOptions *out)
{
	const char *colon = strchr(arg, ':');
	ScopeDecl   scope = { 0 };

	if (!colon || colon == arg || colon[1] == '\0') {
		fprintf(stderr,
		        "elc: '%s' is not an execution scope; expected "
		        "name:glob[,glob...]\n", arg);
		return -1;
	}

	scope.name = strndup(arg, (size_t)(colon - arg));
	if (!scope.name)
		goto oom;

	for (const char *p = colon + 1; *p; ) {
		const char *comma = strchr(p, ',');
		size_t      len   = comma ? (size_t)(comma - p) : strlen(p);

		if (len == 0) {
			fprintf(stderr,
			        "elc: '%s' has an empty component pattern\n",
			        arg);
			goto invalid;
		}

		char **grown = realloc(scope.patterns,
		                       (scope.pattern_count + 1) * sizeof *grown);

		if (!grown)
			goto oom;
		scope.patterns = grown;

		scope.patterns[scope.pattern_count] = strndup(p, len);
		if (!scope.patterns[scope.pattern_count])
			goto oom;
		scope.pattern_count++;

		p = comma ? comma + 1 : p + len;
	}

	if (out->scopes.count == out->scopes.capacity) {
		size_t     next  = out->scopes.capacity ? out->scopes.capacity * 2
		                                        : 4;
		ScopeDecl *grown = realloc(out->scopes.items,
		                           next * sizeof *grown);

		if (!grown)
			goto oom;
		out->scopes.items    = grown;
		out->scopes.capacity = next;
	}

	out->scopes.items[out->scopes.count++] = scope;
	return 0;

oom:
	fputs("elc: out of memory\n", stderr);
invalid:
	for (size_t i = 0; i < scope.pattern_count; i++)
		free(scope.patterns[i]);
	free(scope.patterns);
	free(scope.name);
	return -1;
}

/* Parse one `name:glob[,glob…]` architectural-stratum declaration
 * (LLR-STR-01, LLR-STR-02).
 *
 * Structurally identical to `parse_scope` and deliberately a separate
 * function: the two declarations mean different things, are validated against
 * different analyses, and a single parser taking a list to append to would
 * make a caller's mistake — a scope declared as a stratum — invisible.
 *
 * Repeating a name **adds patterns to the layer already declared** rather than
 * creating a second one, so naming `hal` twice with a pattern each time is one
 * layer of two patterns. A layer's ordinal is fixed when it is first named; the
 * declared order is the dependency direction unless `--stratum-order` states
 * it (LLR-STR-02).
 */
int parse_stratum(const char *arg, ElcOptions *out)
{
	const char *colon = strchr(arg, ':');
	StratumDecl *layer = NULL;
	char        *name  = NULL;

	if (!colon || colon == arg || colon[1] == '\0') {
		fprintf(stderr,
		        "elc: '%s' is not a stratum; expected "
		        "name:glob[,glob...]\n", arg);
		return -1;
	}

	name = strndup(arg, (size_t)(colon - arg));
	if (!name) {
		fputs("elc: out of memory\n", stderr);
		return -1;
	}

	for (size_t i = 0; i < out->strata.count; i++)
		if (strcmp(out->strata.items[i].name, name) == 0) {
			layer = &out->strata.items[i];
			break;
		}

	if (!layer) {
		if (out->strata.count == out->strata.capacity) {
			size_t       next  = out->strata.capacity
			                             ? out->strata.capacity * 2 : 4;
			StratumDecl *grown = realloc(out->strata.items,
			                             next * sizeof *grown);

			if (!grown) {
				fputs("elc: out of memory\n", stderr);
				free(name);
				return -1;
			}
			out->strata.items    = grown;
			out->strata.capacity = next;
		}

		layer = &out->strata.items[out->strata.count];
		memset(layer, 0, sizeof *layer);
		layer->name    = name;
		/* Declaration order is the direction until told otherwise: the
		 * first layer named is the top, permitted to depend downward. */
		layer->ordinal = out->strata.count;
		out->strata.count++;
		name = NULL;
	}
	free(name);

	for (const char *p = colon + 1; *p; ) {
		const char *comma = strchr(p, ',');
		size_t      len   = comma ? (size_t)(comma - p) : strlen(p);

		if (len == 0) {
			fprintf(stderr,
			        "elc: '%s' has an empty component pattern\n", arg);
			return -1;
		}

		char **grown = realloc(layer->patterns,
		                       (layer->pattern_count + 1) * sizeof *grown);

		if (!grown) {
			fputs("elc: out of memory\n", stderr);
			return -1;
		}
		layer->patterns = grown;

		layer->patterns[layer->pattern_count] = strndup(p, len);
		if (!layer->patterns[layer->pattern_count]) {
			fputs("elc: out of memory\n", stderr);
			return -1;
		}
		layer->pattern_count++;

		p = comma ? comma + 1 : p + len;
	}

	return 0;
}

/* Apply a `NAME>NAME[>NAME...]` declaration to the strata already parsed
 * (LLR-STR-02).
 *
 * Resolved once, after every option has been read, so that the order may be
 * given before or after the layers it orders — getopt hands options over one
 * at a time, and making the user remember a sequence would be a trap with no
 * purpose.
 *
 * **Every declared stratum must appear, and every name must be declared.** A
 * partial order cannot determine a direction, and a name for a layer that does
 * not exist is a typo whose silent acceptance would leave the layering
 * validated against something the user did not write.
 */
static int apply_stratum_order(ElcOptions *out)
{
	size_t assigned = 0;

	if (!out->stratum_order)
		return 0;

	if (out->strata.count == 0) {
		fputs("elc: --stratum-order was given but no --stratum was\n",
		      stderr);
		return -1;
	}

	for (const char *p = out->stratum_order; *p; ) {
		const char *sep = strchr(p, '>');
		size_t      len = sep ? (size_t)(sep - p) : strlen(p);
		bool        hit = false;

		if (len == 0) {
			fprintf(stderr, "elc: '%s' names an empty stratum\n",
			        out->stratum_order);
			return -1;
		}

		for (size_t i = 0; i < out->strata.count; i++) {
			if (strlen(out->strata.items[i].name) != len ||
			    strncmp(out->strata.items[i].name, p, len) != 0)
				continue;
			out->strata.items[i].ordinal = assigned++;
			hit                          = true;
			break;
		}

		if (!hit) {
			fprintf(stderr,
			        "elc: --stratum-order names '%.*s', which is not a "
			        "declared stratum\n", (int)len, p);
			return -1;
		}

		p = sep ? sep + 1 : p + len;
	}

	if (assigned != out->strata.count) {
		fprintf(stderr,
		        "elc: --stratum-order names %zu of %zu declared strata; a "
		        "partial order does not determine a direction\n",
		        assigned, out->strata.count);
		return -1;
	}

	return 0;
}

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
"                     and reachability are measured. Repeatable. Entry\n"
"                     points are never guessed at, so with none declared the\n"
"                     analyses that need them are omitted with a stated\n"
"                     reason, and nothing is reported unreachable\n"
"  -b, --bottleneck-threshold N\n"
"                     flag a component whose afferent and efferent couplings\n"
"                     are each N or greater as an architectural bottleneck\n"
"                     (default 5). This threshold is elc's own heuristic, not\n"
"                     a published standard, and is reported as such\n"
"      --stratum NAME:GLOB[,GLOB...]\n"
"                     declare an architectural layer named NAME containing the\n"
"                     files matching GLOB. Repeatable; repeating a name adds\n"
"                     patterns to that layer. The order layers are first\n"
"                     declared is the permitted direction of dependency,\n"
"                     topmost first, unless --stratum-order states it\n"
"      --stratum-order NAME>NAME[>NAME...]\n"
"                     state the permitted direction of dependency between the\n"
"                     declared layers, leftmost topmost. Every declared\n"
"                     stratum must appear, since a partial order determines no\n"
"                     direction\n"
"      --scope NAME:GLOB[,GLOB...]\n"
"                     declare an execution scope named NAME containing the\n"
"                     files matching GLOB. Repeatable. With two or more\n"
"                     declared, every call and every shared global by which\n"
"                     one reaches another is reported\n"
"      --graphml      also write the System Dependence Graph as GraphML,\n"
"                     beside the report and named from it: an --output of\n"
"                     report.md yields report.graphml. Requires --output,\n"
"                     since there is otherwise no name to derive\n"
"      --no-dot       do not write the annotated Graphviz call tree. It is\n"
"                     written by default beside the report and named from\n"
"                     it, an --output of report.md yielding report.dot, and\n"
"                     is never written when the report goes to standard\n"
"                     output, since there is then no name to derive\n"
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
	enum { OPT_FROM_XML = 1000, OPT_GRAPHML, OPT_NO_DOT, OPT_ENTRY,
	       OPT_SCOPE, OPT_STRATUM, OPT_STRATUM_ORDER };

	static const struct option longopts[] = {
		{ "format",               required_argument, NULL, 'f' },
		{ "from-xml",             required_argument, NULL, OPT_FROM_XML },
		{ "complexity-threshold", required_argument, NULL, 'c' },
		{ "output",               required_argument, NULL, 'o' },
		{ "graphml",              no_argument,       NULL, OPT_GRAPHML },
		{ "no-dot",               no_argument,       NULL, OPT_NO_DOT },
		{ "entry",                required_argument, NULL, OPT_ENTRY },
		{ "scope",                required_argument, NULL, OPT_SCOPE },
		{ "bottleneck-threshold", required_argument, NULL, 'b' },
		{ "stratum",              required_argument, NULL, OPT_STRATUM },
		{ "stratum-order",        required_argument, NULL, OPT_STRATUM_ORDER },
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
	out->bottleneck_threshold = ELC_DEFAULT_BOTTLENECK_THRESHOLD;

	/* Report errors ourselves so every diagnostic reaches stderr in one
	 * voice (HLR-038); getopt's own messages would bypass cli_usage(). */
	opterr = 0;
	optind = 1;

	int c;
	while ((c = getopt_long(argc, argv, ":ho:c:f:b:", longopts, NULL)) != -1) {
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
		case 'b': {
			/* The same test the complexity threshold gets, and for
			 * the same reason: strtoul alone accepts a sign,
			 * leading whitespace, and a trailing tail, none of
			 * which is a threshold. */
			char         *end  = NULL;
			unsigned long value;

			errno = 0;
			value = strtoul(optarg, &end, 10);

			if (optarg[0] < '0' || optarg[0] > '9' || !end ||
			    *end != '\0' || errno == ERANGE ||
			    value > UINT32_MAX) {
				fprintf(stderr,
				        "elc: '%s' is not a bottleneck "
				        "threshold\n", optarg);
				return CLI_ERROR;
			}
			out->bottleneck_threshold = (uint32_t)value;
			break;
		}
		case OPT_STRATUM:
			if (parse_stratum(optarg, out) != 0)
				return CLI_ERROR;
			break;
		case OPT_STRATUM_ORDER:
			/* Borrowed from argv and resolved after the loop, so
			 * that it may be given before the layers it orders. */
			out->stratum_order = optarg;
			break;
		case OPT_SCOPE:
			/* Owned outright, unlike the entry points: the
			 * declaration is split on two separators, so neither
			 * the name nor any pattern is a substring of argv that
			 * could be borrowed. */
			if (parse_scope(optarg, out) != 0)
				return CLI_ERROR;
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
		case OPT_NO_DOT:
			/* A refusal, not a request, which is why it needs no
			 * validation against --output: the companion is on by
			 * default (HLR-103) and declining it can never be in
			 * conflict with anything (LLR-WAR-01). */
			out->no_dot = true;
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

	if (apply_stratum_order(out) != 0)
		return CLI_ERROR;

	if (out->mode == MODE_REGENERATE) {
		/* A record carries the findings of a run, not the topology of
		 * the graph, so no companion artefact can be reconstructed from
		 * one. The default-on `.dot` is therefore simply not written —
		 * declining to produce it silently is what HLR-122 asks for —
		 * but an *explicit* request is rejected, so that a user who
		 * asked for a file and got none is told why rather than left to
		 * discover the absence (LLR-CLI-15). */
		if (out->graphml) {
			fputs("elc: --graphml cannot be combined with "
			      "--from-xml: a saved record carries the findings "
			      "of a run, not the graph they came from\n",
			      stderr);
			return CLI_ERROR;
		}

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
	for (size_t i = 0; i < opts->scopes.count; i++) {
		for (size_t p = 0; p < opts->scopes.items[i].pattern_count; p++)
			free(opts->scopes.items[i].patterns[p]);
		free(opts->scopes.items[i].patterns);
		free(opts->scopes.items[i].name);
	}
	free(opts->scopes.items);
	for (size_t i = 0; i < opts->strata.count; i++) {
		for (size_t p = 0; p < opts->strata.items[i].pattern_count; p++)
			free(opts->strata.items[i].patterns[p]);
		free(opts->strata.items[i].patterns);
		free(opts->strata.items[i].name);
	}
	free(opts->strata.items);
	memset(opts, 0, sizeof(*opts));
}
