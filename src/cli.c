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
"                     table, csv, xml, or md, for a report written to\n"
"                     standard output (default table). With --output the\n"
"                     filename extension names the format instead, and this\n"
"                     option is accepted only where it agrees with it. In\n"
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
"  -o, --output FILE  write the report to FILE instead of standard output.\n"
"                     The extension of FILE names the format: .txt for the\n"
"                     aligned table, .md for Markdown, .csv for CSV, and\n"
"                     .xml for the complete record. Any other extension, or\n"
"                     none, is a usage error rather than a guess\n"
"  -v, --verbose      present the verbose report: every per-function,\n"
"                     per-object, per-edge, and per-match table, in addition\n"
"                     to the summary tiers a report presents by default.\n"
"                     Presentation only — no measurement, finding, or exit\n"
"                     status changes. The csv and xml formats are complete\n"
"                     whatever the verbosity, so this has no effect on them\n"
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
"",
	      stream);

	/* The second of three, for the reason given below: the summary has
	 * outgrown the guaranteed literal length twice now, and each option
	 * added moves it closer again. The breaks fall between groups of
	 * options rather than mid-entry. */
	fputs(
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
"      --dsm          also write the dependency structure matrix as CSV,\n"
"                     beside the report and named from it: an --output of\n"
"                     report.md yields report.dsm.csv. Rows are callers and\n"
"                     columns callees, so back-calls gather below the\n"
"                     diagonal. Requires --output, since there is otherwise\n"
"                     no name to derive. The matrix is over the declared\n"
"                     layers, or over the analysed directories where none\n"
"                     were declared\n"
"  -D, --define NAME[=VALUE]\n"
"                     define a conditional-compilation symbol, so that the\n"
"                     metrics describe the configuration in which it is\n"
"                     defined. Repeatable. A condition testing a symbol no\n"
"                     -D mentions is undecidable rather than false: both\n"
"                     branches are counted and the region is reported\n"
"                     undecided. With no -D at all nothing is pruned\n"
"      --elf FILE     restrict every measurement to the functions the linked\n"
"                     image FILE defines, so that the report describes what\n"
"                     the build kept rather than what the source holds. FILE\n"
"                     is an image, not a source tree, and is read for its\n"
"                     symbol table alone: no compiler, linker, or build system\n"
"                     is invoked and no debugging information is needed. The\n"
"                     source functions the image does not define are listed,\n"
"                     and the linkage names elc could not decode are counted.\n"
"                     With no --elf nothing is filtered\n"
"      --rules LANG:PATH\n"
"                     check the analysed source against the custom rule query\n"
"                     in PATH, compiled for language LANG. Repeatable. Rules\n"
"                     placed in the runtime location under\n"
"                     queries/LANG/rules/ are used without being named; no\n"
"                     rule file is ever discovered from the working\n"
"                     directory, the target, or a dotfile\n"
"      --no-dot       do not write the annotated Graphviz call tree. It is\n"
"                     written by default beside the report and named from\n"
"                     it, an --output of report.md yielding report.dot, and\n"
"                     is never written when the report goes to standard\n"
"                     output, since there is then no name to derive\n"
"  -h, --help         display this help and exit\n",
	      stream);

	/* Split, and not for style: one literal holding the whole summary
	 * exceeds the 4095 characters ISO C99 requires a compiler to
	 * support, and this build treats a warning as a defect. The break
	 * falls between the options and the prose so that adding an option
	 * does not move it. */
	fputs(
"\n"
"Output:\n"
"  By default the summary tiers: the project totals and callouts, the route\n"
"  each directory target was discovered by, the totals broken down by\n"
"  language, each file's own totals, the functions at or over the complexity\n"
"  threshold, any analysis omitted for want of a declaration, the findings\n"
"  ranked by severity, the configuration in force, any image filtered by,\n"
"  any partly parsed files, and the files skipped.\n"
"\n"
"  With --verbose, those and the detail tiers as well: one row per function,\n"
"  and what the dependence graph says — fan-out, recursion, call depth,\n"
"  coupling, dependency cycles, layering, global state, dead code,\n"
"  cross-scope access — each measurement beside the published threshold it\n"
"  was judged against, with any custom-rule matches.\n"
"\n"
"  Files are listed by canonical absolute path, in ascending byte order,\n"
"  each exactly once however many targets reach it.\n"
"\n"
"Exit status:\n"
"  0  every discovered file was processed, or skipped for want of a\n"
"     language module\n"
"  1  the run completed and produced a report, but at least one file\n"
"     failed to be read or parsed\n"
"  2  the run did not complete and no report was produced: a usage error,\n"
"     an invalid target, a fatal runtime-location failure, an unusable\n"
"     linked image, or a rejected saved record\n"
"\n"
"  No finding, at any severity, affects the exit status.\n"
"\n"
"Full documentation: elc(1), and doc/User_Manual.md in the distribution.\n",
	      stream);
}

/* A value above any printable character, so a long-only option cannot
 * collide with a short one. */
enum { OPT_FROM_XML = 1000, OPT_GRAPHML, OPT_NO_DOT, OPT_ENTRY,
       OPT_SCOPE, OPT_STRATUM, OPT_STRATUM_ORDER, OPT_RULES, OPT_ELF,
       OPT_DSM };

/* What reading one option needs: the options being built, and the record of
 * how the format came to be what it is. All three outlive the option that
 * touched them — `--format` is remembered until the end of the parse, because
 * whether the user chose Markdown or merely got it is a question only
 * regeneration asks (LLR-CLI-10) — so none belongs to any single handler.
 *
 * The two selection flags are kept apart because they answer different
 * questions. `format_given` says the *option* named a format, which is what
 * HLR-149's disagreement check compares against the filename. `format_from_
 * extension` says the *filename* named one, which is a selection just as
 * explicit — differently spelled — and is what stops regeneration silently
 * writing Markdown into a file called `report.txt` (LLR-CLI-10, LLR-CLI-28).
 */
typedef struct {
	ElcOptions *out;
	bool        format_given;
	bool        format_from_extension;
} CliParse;

/* The output-filename extensions elc recognises, and the format each names
 * (HLR-148).
 *
 * The single statement of the mapping: `format_extensions()` builds the
 * diagnostic from this table, so a format added here is named in the error
 * message a wrong extension produces without a second list to keep in step.
 */
static const struct { const char *extension; OutputFormat format; }
EXTENSIONS[] = {
	{ "txt", FORMAT_TABLE    },
	{ "md",  FORMAT_MARKDOWN },
	{ "csv", FORMAT_CSV      },
	{ "xml", FORMAT_XML      }
};

/* How the `--format` option spells each format, for a diagnostic that names
 * what the user would have had to write. Ordered by the enumerator so the
 * lookup is an index rather than a search. */
static const char *const FORMAT_NAMES[] = { "table", "csv", "xml", "md" };

/* Read a threshold: digits and nothing else.
 *
 * strtoul accepts leading whitespace, a sign, and a trailing tail; none of
 * those is a threshold. Both thresholds get the same test, which is why it is
 * written once — `what` names the one being read, and is the only thing that
 * differs between them.
 *
 * Returns 0, or -1 after a diagnostic.
 */
static int parse_threshold(const char *arg, const char *what, uint32_t *slot)
{
	char         *end = NULL;
	unsigned long value;

	errno = 0;
	value = strtoul(arg, &end, 10);

	if (arg[0] < '0' || arg[0] > '9' || !end || *end != '\0' ||
	    errno == ERANGE || value > UINT32_MAX) {
		fprintf(stderr, "elc: '%s' is not a %s threshold\n", arg, what);
		return -1;
	}
	*slot = (uint32_t)value;
	return 0;
}

/* Append one string borrowed from argv to a growable array.
 *
 * Three options collect their arguments this way — entry points, rule files,
 * and definitions — and all three borrow: the string is a whole argument, and
 * argv outlives the run, so only the array holding the pointers is owned.
 * getopt hands them over one at a time, which is why the array grows rather
 * than being sized up front.
 *
 * Returns 0, or -1 after a diagnostic when the array cannot grow.
 */
static int append_borrowed(const char ***items, size_t *count, size_t *capacity,
                           const char *value)
{
	if (*count == *capacity) {
		size_t       next  = *capacity ? *capacity * 2 : 8;
		const char **grown = realloc(*items, next * sizeof *grown);

		if (!grown) {
			fputs("elc: out of memory\n", stderr);
			return -1;
		}
		*items    = grown;
		*capacity = next;
	}
	(*items)[(*count)++] = value;
	return 0;
}

/* --- one handler per option ------------------------------------------------
 *
 * Each reads the option's argument — NULL for an option that takes none — and
 * returns CLI_OK, CLI_ERROR for a usage error, or ELC_EXIT_FATAL for an
 * allocation that failed. Every one of them has already written its own
 * diagnostic before returning anything but CLI_OK.
 */

static int opt_format(const char *arg, CliParse *p)
{
	static const struct { const char *name; OutputFormat format; } formats[] = {
		{ "table", FORMAT_TABLE },
		{ "csv",   FORMAT_CSV   },
		{ "xml",   FORMAT_XML   },
		{ "md",    FORMAT_MARKDOWN }
	};

	for (size_t i = 0; i < sizeof formats / sizeof *formats; i++) {
		if (strcmp(arg, formats[i].name) != 0)
			continue;
		p->out->format  = formats[i].format;
		p->format_given = true;
		return CLI_OK;
	}

	fprintf(stderr, "elc: '%s' is not a format; expected table, csv, xml, "
	        "or md\n", arg);
	return CLI_ERROR;
}

static int opt_from_xml(const char *arg, CliParse *p)
{
	p->out->mode       = MODE_REGENERATE;
	p->out->input_path = arg;
	return CLI_OK;
}

static int opt_complexity(const char *arg, CliParse *p)
{
	if (parse_threshold(arg, "complexity",
	                    &p->out->complexity_threshold) != 0)
		return CLI_ERROR;
	return CLI_OK;
}

static int opt_bottleneck(const char *arg, CliParse *p)
{
	if (parse_threshold(arg, "bottleneck",
	                    &p->out->bottleneck_threshold) != 0)
		return CLI_ERROR;
	return CLI_OK;
}

static int opt_output(const char *arg, CliParse *p)
{
	/* Borrowed from argv, which outlives the run. Recording standard
	 * output as the destination is the default: output_path stays NULL
	 * (LLR-CLI-03). */
	p->out->output_path = arg;
	return CLI_OK;
}

static int opt_entry(const char *arg, CliParse *p)
{
	if (append_borrowed(&p->out->entry_points, &p->out->entry_point_count,
	                    &p->out->entry_point_capacity, arg) != 0)
		return ELC_EXIT_FATAL;
	return CLI_OK;
}

static int opt_stratum(const char *arg, CliParse *p)
{
	if (parse_stratum(arg, p->out) != 0)
		return CLI_ERROR;
	return CLI_OK;
}

static int opt_stratum_order(const char *arg, CliParse *p)
{
	/* Borrowed from argv and resolved after the loop, so that it may be
	 * given before the layers it orders. */
	p->out->stratum_order = arg;
	return CLI_OK;
}

static int opt_scope(const char *arg, CliParse *p)
{
	/* Owned outright, unlike the entry points: the declaration is split on
	 * two separators, so neither the name nor any pattern is a substring of
	 * argv that could be borrowed. */
	if (parse_scope(arg, p->out) != 0)
		return CLI_ERROR;
	return CLI_OK;
}

static int opt_graphml(const char *arg, CliParse *p)
{
	/* Recorded, not validated against --output here. A request for GraphML
	 * with the report on stdout is not a usage error — it produces no
	 * companion, which is what HLR-106 says happens, and rejecting it would
	 * make `elc --graphml src/` fail where the requirement says it should
	 * simply not write a file. */
	(void)arg;
	p->out->graphml = true;
	return CLI_OK;
}

static int opt_dsm(const char *arg, CliParse *p)
{
	/* Recorded, not validated against --output, by the rule --graphml
	 * follows: a request for the matrix with the report on stdout produces
	 * no companion rather than a usage error, since there is no name to
	 * derive one from (HLR-104, HLR-180). */
	(void)arg;
	p->out->dsm = true;
	return CLI_OK;
}

static int opt_no_dot(const char *arg, CliParse *p)
{
	/* A refusal, not a request, which is why it needs no validation against
	 * --output: the companion is on by default (HLR-103) and declining it
	 * can never be in conflict with anything (LLR-WAR-01). */
	(void)arg;
	p->out->no_dot = true;
	return CLI_OK;
}

static int opt_verbose(const char *arg, CliParse *p)
{
	/* Recorded and validated against nothing. Asking a complete-record
	 * format for more detail is not a contradiction — there is no
	 * presentation to vary and the request simply has no effect — so it is
	 * accepted rather than rejected, unlike every other pairing this parser
	 * decides (HLR-152, LLR-CLI-30). */
	(void)arg;
	p->out->verbose = true;
	return CLI_OK;
}

static int opt_rules(const char *arg, CliParse *p)
{
	/* Recorded unsplit and unvalidated. Whether the named language exists
	 * is the registry's question — it is the module that knows — and
	 * answering it here would put a second copy of that knowledge in the
	 * parser (HLR-107, LLR-RLR-02). */
	if (append_borrowed(&p->out->rules, &p->out->rule_count,
	                    &p->out->rule_capacity, arg) != 0)
		return ELC_EXIT_FATAL;
	return CLI_OK;
}

static int opt_define(const char *arg, CliParse *p)
{
	/* Recorded as given, `NAME` and `NAME=VALUE` alike. What a definition
	 * *means* is a question for the conditional evaluation, which is the
	 * only place that knows what a language's conditions can test. */
	if (arg[0] == '\0') {
		fputs("elc: -D requires a symbol name\n", stderr);
		return CLI_ERROR;
	}
	if (append_borrowed(&p->out->defines, &p->out->define_count,
	                    &p->out->define_capacity, arg) != 0)
		return ELC_EXIT_FATAL;
	return CLI_OK;
}

static int opt_elf(const char *arg, CliParse *p)
{
	/* Recorded unvalidated and unopened. Whether the file is an image elc
	 * can read is `elfsyms.c`'s question — it is the module that knows —
	 * and answering it here would put a second copy of that knowledge in
	 * the parser, which is the module that reads argv and not one that
	 * reads files (HLR-140, LLR-CLI-22). */
	if (arg[0] == '\0') {
		fputs("elc: --elf requires the path of a linked image\n",
		      stderr);
		return CLI_ERROR;
	}
	p->out->image_path = arg;
	return CLI_OK;
}

typedef int (*OptionFn)(const char *arg, CliParse *p);

/* Which handler reads which option. A table rather than a switch: the loop is
 * then one shape whatever the option is, and each option's meaning sits in a
 * named function beside the requirement it serves rather than in a case label
 * three hundred lines from the next one.
 */
static const struct { int code; OptionFn handle; } OPTION_HANDLERS[] = {
	{ 'f',               opt_format        },
	{ OPT_FROM_XML,      opt_from_xml      },
	{ 'c',               opt_complexity    },
	{ 'o',               opt_output        },
	{ OPT_ENTRY,         opt_entry         },
	{ 'b',               opt_bottleneck    },
	{ OPT_STRATUM,       opt_stratum       },
	{ OPT_STRATUM_ORDER, opt_stratum_order },
	{ OPT_SCOPE,         opt_scope         },
	{ OPT_GRAPHML,       opt_graphml       },
	{ OPT_DSM,           opt_dsm           },
	{ OPT_NO_DOT,        opt_no_dot        },
	{ 'v',               opt_verbose       },
	{ OPT_RULES,         opt_rules         },
	{ 'D',               opt_define        },
	{ OPT_ELF,           opt_elf           }
};

static OptionFn option_handler(int code)
{
	for (size_t i = 0; i < sizeof OPTION_HANDLERS / sizeof *OPTION_HANDLERS;
	     i++) {
		if (OPTION_HANDLERS[i].code == code)
			return OPTION_HANDLERS[i].handle;
	}
	return NULL;
}

/* Report an option no handler can be found for: one missing its argument, and
 * one that is not an option elc has. Always CLI_ERROR — the two are told apart
 * for the reader's sake, not the caller's.
 */
static int report_bad_option(int code, char *argv[])
{
	if (code == ':')
		fprintf(stderr, "elc: option '%s' requires an argument\n",
		        argv[optind - 1]);
	else if (optopt)
		fprintf(stderr, "elc: unrecognised option '-%c'\n", optopt);
	else
		fprintf(stderr, "elc: unrecognised option '%s'\n",
		        argv[optind - 1]);
	return CLI_ERROR;
}

/* The extension of a path, or NULL where it has none.
 *
 * The last dot of the *basename*, so a directory component carrying one —
 * `build.d/report` — does not lend its extension to a file that has none. A
 * leading dot names a hidden file rather than an extension, and a trailing one
 * names nothing at all; both are "no extension" and are reported as such
 * (LLR-CLI-26).
 */
static const char *path_extension(const char *path)
{
	const char *base = strrchr(path, '/');
	const char *dot;

	base = base ? base + 1 : path;
	dot  = strrchr(base, '.');

	if (!dot || dot == base || dot[1] == '\0')
		return NULL;
	return dot + 1;
}

/* Write the recognised extensions into `buffer` as `.txt, .md, .csv, or .xml`.
 *
 * Built from EXTENSIONS rather than written out, so that the diagnostic cannot
 * come to list a set of formats elc no longer has (HLR-148).
 */
static const char *format_extensions(char *buffer, size_t size)
{
	size_t count = sizeof EXTENSIONS / sizeof *EXTENSIONS;
	size_t at    = 0;

	buffer[0] = '\0';
	for (size_t i = 0; i < count; i++) {
		int n = snprintf(buffer + at, size - at, "%s%s.%s",
		                 i ? ", " : "",
		                 i + 1 == count ? "or " : "",
		                 EXTENSIONS[i].extension);

		if (n < 0 || (size_t)n >= size - at)
			break;
		at += (size_t)n;
	}
	return buffer;
}

/* Settle the report format, which two options can each state (HLR-148,
 * HLR-149).
 *
 * Standard output has no filename and so no extension: there the option alone
 * decides, and its default stands. Where a file is named, its extension states
 * the format and no option is needed to repeat it — but an option that repeats
 * it is accepted, since nothing is ambiguous about saying a thing twice, and
 * one that contradicts it is a usage error naming both rather than a silent
 * preference for either.
 *
 * Run after the option loop rather than inside it, so that `-f` and `-o` may
 * be given in either order (LLR-CLI-26, LLR-CLI-27).
 *
 * Returns CLI_OK, or CLI_ERROR after a diagnostic.
 */
static int resolve_format(CliParse *p)
{
	const char *path = p->out->output_path;
	const char *extension;
	char        recognised[64];

	if (!path)
		return CLI_OK;

	extension = path_extension(path);
	if (!extension) {
		fprintf(stderr,
		        "elc: output file '%s' has no extension to name a "
		        "report format; expected %s\n",
		        path, format_extensions(recognised, sizeof recognised));
		return CLI_ERROR;
	}

	for (size_t i = 0; i < sizeof EXTENSIONS / sizeof *EXTENSIONS; i++) {
		if (strcmp(extension, EXTENSIONS[i].extension) != 0)
			continue;

		/* Two statements of one fact. Where they agree the invocation
		 * stands; where they disagree neither is preferred, because
		 * honouring one would leave the user's own command line
		 * disagreeing with the file it produced (HLR-149). */
		if (p->format_given && p->out->format != EXTENSIONS[i].format) {
			fprintf(stderr,
			        "elc: --format %s and an output file named "
			        "'%s' disagree; the extension already names "
			        "the format, so name it once or name it the "
			        "same twice\n",
			        FORMAT_NAMES[p->out->format], path);
			return CLI_ERROR;
		}

		p->out->format            = EXTENSIONS[i].format;
		p->format_from_extension  = true;
		return CLI_OK;
	}

	/* Guessing here would write one format under a name promising another,
	 * and defaulting to the table would produce a `report.json` holding no
	 * JSON (HLR-148). */
	fprintf(stderr,
	        "elc: '.%s' is not a report format extension; expected %s\n",
	        extension, format_extensions(recognised, sizeof recognised));
	return CLI_ERROR;
}

/* The checks that apply only when a saved record is the input, and the default
 * that goes with it.
 *
 * Returns CLI_OK, or CLI_ERROR after a diagnostic.
 */
static int check_regenerate(int argc, char *argv[], CliParse *p)
{
	/* A record carries the findings of a run, not the topology of the
	 * graph, so no companion artefact can be reconstructed from one. The
	 * default-on `.dot` is therefore simply not written — declining to
	 * produce it silently is what HLR-122 asks for — but an *explicit*
	 * request is rejected, so that a user who asked for a file and got none
	 * is told why rather than left to discover the absence (LLR-CLI-15). */
	if (p->out->graphml) {
		fputs("elc: --graphml cannot be combined with --from-xml: a "
		      "saved record carries the findings of a run, not the "
		      "graph they came from\n", stderr);
		return CLI_ERROR;
	}

	/* Pruning happens when a file is measured, not when a report is
	 * rendered, so a record already describes one configuration and cannot
	 * be re-cut into another. Rejected rather than ignored, so that a user
	 * who named a configuration and got a different one is told (HLR-136,
	 * LLR-CLI-24). */
	if (p->out->define_count > 0) {
		fputs("elc: -D cannot be combined with --from-xml: a saved "
		      "record already describes one configuration, chosen when "
		      "it was written\n", stderr);
		return CLI_ERROR;
	}

	/* The same rule the definitions get, and for the same reason: the
	 * filter is applied when a file is measured, not when a report is
	 * rendered, so a record already describes the run that was filtered and
	 * cannot be re-cut against another image (HLR-147, LLR-CLI-23). */
	if (p->out->image_path) {
		fputs("elc: --elf cannot be combined with --from-xml: a saved "
		      "record already describes one filtered run, chosen when "
		      "it was written\n", stderr);
		return CLI_ERROR;
	}

	/* A saved record is the input, so a target would name a second source
	 * of truth for the same report. */
	if (optind < argc) {
		fprintf(stderr, "elc: --from-xml takes no target, but '%s' was "
		        "given\n", argv[optind]);
		return CLI_ERROR;
	}

	/* Markdown is the mode's output (HLR-055), and is therefore its default
	 * rather than a format the user must remember. Only an *explicit*
	 * choice of something else is an error — defaulting to table and then
	 * rejecting it would make the mode unusable without a redundant option
	 * (LLR-CLI-10).
	 *
	 * **An output filename's extension is such a choice.** HLR-148 makes
	 * the extension a statement of the format and HLR-149 makes it the same
	 * statement `--format` makes, so `--from-xml rec.xml -o out.txt` is the
	 * user asking for a table exactly as `-f table` is. Reading it as
	 * anything less would leave the mode writing Markdown into a file named
	 * `out.txt` — one format under a name promising another, which is the
	 * result HLR-148 exists to forbid. The two spellings are told apart in
	 * the diagnostic alone, so that the message names the thing the user
	 * actually wrote (LLR-CLI-10, LLR-CLI-28). */
	if (!p->format_given && !p->format_from_extension)
		p->out->format = FORMAT_MARKDOWN;
	else if (p->out->format != FORMAT_MARKDOWN) {
		if (p->format_from_extension)
			fprintf(stderr,
			        "elc: --from-xml produces Markdown, but the "
			        "output file '%s' names %s; a saved record can "
			        "be regenerated as Markdown alone\n",
			        p->out->output_path,
			        FORMAT_NAMES[p->out->format]);
		else
			fputs("elc: --from-xml produces Markdown; no other "
			      "format can be regenerated from a saved "
			      "record\n", stderr);
		return CLI_ERROR;
	}

	return CLI_OK;
}

int cli_parse(int argc, char *argv[], ElcOptions *out)
{
	static const struct option longopts[] = {
		{ "format",               required_argument, NULL, 'f' },
		{ "from-xml",             required_argument, NULL, OPT_FROM_XML },
		{ "complexity-threshold", required_argument, NULL, 'c' },
		{ "output",               required_argument, NULL, 'o' },
		{ "graphml",              no_argument,       NULL, OPT_GRAPHML },
		{ "dsm",                  no_argument,       NULL, OPT_DSM },
		{ "no-dot",               no_argument,       NULL, OPT_NO_DOT },
		{ "verbose",              no_argument,       NULL, 'v' },
		{ "entry",                required_argument, NULL, OPT_ENTRY },
		{ "rules",                required_argument, NULL, OPT_RULES },
		{ "elf",                  required_argument, NULL, OPT_ELF },
		{ "define",               required_argument, NULL, 'D' },
		{ "scope",                required_argument, NULL, OPT_SCOPE },
		{ "bottleneck-threshold", required_argument, NULL, 'b' },
		{ "stratum",              required_argument, NULL, OPT_STRATUM },
		{ "stratum-order",        required_argument, NULL, OPT_STRATUM_ORDER },
		{ "help",                 no_argument,       NULL, 'h' },
		{ NULL,                   0,                 NULL, 0   }
	};

	CliParse p = { out, false, false };
	int      c;

	memset(out, 0, sizeof(*out));
	out->mode                 = MODE_ANALYSE;
	out->format               = FORMAT_TABLE;
	out->complexity_threshold = ELC_DEFAULT_COMPLEXITY_THRESHOLD;
	out->bottleneck_threshold = ELC_DEFAULT_BOTTLENECK_THRESHOLD;

	/* Report errors ourselves so every diagnostic reaches stderr in one
	 * voice (HLR-038); getopt's own messages would bypass cli_usage(). */
	opterr = 0;
	optind = 1;

	while ((c = getopt_long(argc, argv, ":hvo:c:f:b:D:", longopts, NULL)) != -1) {
		OptionFn handle;
		int      status;

		/* The one option that answers rather than records, and so ends
		 * the parse where it stands rather than adding to `out`. */
		if (c == 'h') {
			cli_usage(stdout);
			return CLI_HELP;
		}

		handle = option_handler(c);
		if (!handle)
			return report_bad_option(c, argv);

		status = handle(optarg, &p);
		if (status != CLI_OK)
			return status;
	}

	if (apply_stratum_order(out) != 0)
		return CLI_ERROR;

	/* After the loop, so that `-f` and `-o` may be given in either order
	 * and still be compared against one another (HLR-148, HLR-149). Before
	 * the regeneration checks, because those ask which format was chosen
	 * and the filename is one of the two ways of choosing it. */
	if (resolve_format(&p) != CLI_OK)
		return CLI_ERROR;

	if (out->mode == MODE_REGENERATE)
		return check_regenerate(argc, argv, &p);

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
	free((void *)opts->rules);
	free((void *)opts->defines);
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
