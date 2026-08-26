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

#include "diag.h"
#include "cli.h"
#include "elc.h"

/* The colon separating a declaration's name from its pattern list, or NULL
 * having diagnosed against `arg`.
 *
 * `--scope` and `--stratum` take the same grammar and refuse it in the same
 * words, so the refusal is written once (HLR-104, HLR-161).
 */
static const char *declaration_colon(const char *arg, const char *what)
{
	const char *colon = strchr(arg, ':');

	if (!colon || colon == arg || colon[1] == '\0') {
		diag_printf("elc: '%s' is not %s; expected "
		        "name:glob[,glob...]\n", arg, what);
		return NULL;
	}
	return colon;
}

/* Append every comma-separated pattern in `list` to the array `patterns`
 * holds, growing it one entry at a time. Returns 0, or -1 having diagnosed.
 *
 * Shared by the two declarations that take a pattern list, because they take
 * the same one: `--scope` and `--stratum` differ in what they do with the name
 * ahead of it, not in how they read the patterns after it.
 */
static int append_patterns(const char *arg, const char *list, char ***patterns,
                           size_t *count)
{
	for (const char *p = list; *p; ) {
		const char *comma = strchr(p, ',');
		size_t      len   = comma ? (size_t)(comma - p) : strlen(p);
		char      **grown;

		if (len == 0) {
			diag_printf("elc: '%s' has an empty component pattern\n",
			        arg);
			return -1;
		}

		grown = realloc(*patterns, (*count + 1) * sizeof *grown);
		if (!grown) {
			diag_printf("elc: out of memory\n");
			return -1;
		}
		*patterns = grown;

		(*patterns)[*count] = strndup(p, len);
		if (!(*patterns)[*count]) {
			diag_printf("elc: out of memory\n");
			return -1;
		}
		(*count)++;

		p = comma ? comma + 1 : p + len;
	}
	return 0;
}

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
	const char *colon = declaration_colon(arg, "an execution scope");
	ScopeDecl   scope = { 0 };

	if (!colon)
		return -1;

	scope.name = strndup(arg, (size_t)(colon - arg));
	if (!scope.name) {
		diag_printf("elc: out of memory\n");
		goto failed;
	}

	if (append_patterns(arg, colon + 1, &scope.patterns,
	                    &scope.pattern_count) != 0)
		goto failed;

	if (out->scopes.count == out->scopes.capacity) {
		size_t     next  = out->scopes.capacity
		                           ? out->scopes.capacity * 2 : 4;
		ScopeDecl *grown = realloc(out->scopes.items,
		                           next * sizeof *grown);

		if (!grown) {
			diag_printf("elc: out of memory\n");
			goto failed;
		}
		out->scopes.items    = grown;
		out->scopes.capacity = next;
	}

	out->scopes.items[out->scopes.count++] = scope;
	return 0;

failed:
	for (size_t i = 0; i < scope.pattern_count; i++)
		free(scope.patterns[i]);
	free(scope.patterns);
	free(scope.name);
	return -1;
}

/* The stratum called `name`, appending a new one where none is declared yet.
 *
 * Takes ownership of `name` only when it appends: two `--stratum` arguments
 * naming the same layer are one declaration with two pattern lists, which is
 * what lets a layer be built up across several arguments (HLR-161).
 */
static StratumDecl *stratum_named(ElcOptions *out, char *name)
{
	StratumDecl *layer;

	for (size_t i = 0; i < out->strata.count; i++)
		if (strcmp(out->strata.items[i].name, name) == 0) {
			free(name);
			return &out->strata.items[i];
		}

	if (out->strata.count == out->strata.capacity) {
		size_t       next  = out->strata.capacity
		                             ? out->strata.capacity * 2 : 4;
		StratumDecl *grown = realloc(out->strata.items,
		                             next * sizeof *grown);

		if (!grown) {
			diag_printf("elc: out of memory\n");
			free(name);
			return NULL;
		}
		out->strata.items    = grown;
		out->strata.capacity = next;
	}

	layer = &out->strata.items[out->strata.count];
	memset(layer, 0, sizeof *layer);
	layer->name = name;
	/* Declaration order is the direction until told otherwise: the first
	 * layer named is the top, permitted to depend downward. */
	layer->ordinal = out->strata.count;
	out->strata.count++;

	return layer;
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
	const char  *colon = declaration_colon(arg, "a stratum");
	StratumDecl *layer;
	char        *name;

	if (!colon)
		return -1;

	name = strndup(arg, (size_t)(colon - arg));
	if (!name) {
		diag_printf("elc: out of memory\n");
		return -1;
	}

	layer = stratum_named(out, name);
	if (!layer)
		return -1;

	return append_patterns(arg, colon + 1, &layer->patterns,
	                       &layer->pattern_count);
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
		diag_printf("elc: --stratum-order was given but no --stratum was\n");
		return -1;
	}

	for (const char *p = out->stratum_order; *p; ) {
		const char *sep = strchr(p, '>');
		size_t      len = sep ? (size_t)(sep - p) : strlen(p);
		bool        hit = false;

		if (len == 0) {
			diag_printf("elc: '%s' names an empty stratum\n",
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
			diag_printf("elc: --stratum-order names '%.*s', which is not a "
			        "declared stratum\n", (int)len, p);
			return -1;
		}

		p = sep ? sep + 1 : p + len;
	}

	if (assigned != out->strata.count) {
		diag_printf("elc: --stratum-order names %zu of %zu declared strata; a "
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

	/* The second of five, for the reason given below: the summary has
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
"",
	      stream);

	/* The third of five. Same reason as the break above: the summary has
	 * outgrown the guaranteed literal length again, and the break falls
	 * between groups rather than mid-entry (LLR-USG-08). */
	fputs(
"      --sink-authority PCT\n"
"      --sink-hub PCT\n"
"      --god-betweenness PCT\n"
"      --god-hub PCT\n"
"      --core-depth N\n"
"                     adjust the purification of the architecture-recovery\n"
"                     view. A function is a utility sink where its authority\n"
"                     outranks PCT per cent of the others and its hub score\n"
"                     outranks no more than PCT of them (defaults 90 and 10),\n"
"                     and a god object where its betweenness and its hub score\n"
"                     each outrank PCT per cent (default 90 for both); a\n"
"                     function below core depth N is peripheral and is left\n"
"                     out of the view entirely (default 2). These five are\n"
"                     elc's own heuristics, not published standards, and are\n"
"                     reported as such. They govern the recovery view alone:\n"
"                     no measurement or finding elsewhere is taken over it\n"
"      --scope NAME:GLOB[,GLOB...]\n"
"                     declare an execution scope named NAME containing the\n"
"                     files matching GLOB. Repeatable. With two or more\n"
"                     declared, every call and every shared global by which\n"
"                     one reaches another is reported\n"
"      --graphml      also write the System Dependence Graph as GraphML,\n"
"                     beside the report and named from it: an --output of\n"
"                     report.md yields report.graphml. Requires --output,\n"
"                     since there is otherwise no name to derive\n"
"      --dbg          also write a debug log beside the report and named\n"
"                     from it: an --output of report.md yields report.dbg.\n"
"                     It carries the invocation, every diagnostic the run\n"
"                     wrote, and the source of every region the grammar\n"
"                     could not parse -- what is needed to debug a tree\n"
"                     nobody else can reproduce. Requires --output\n"
"",
	      stream);

	/* The fourth of five. Same reason as the breaks above: purification's
	 * three companion artefacts pushed the third block past the guaranteed
	 * literal length, and the break falls between groups of options rather
	 * than mid-entry (LLR-USG-08). */
	fputs(
"      --manifest FILE\n"
"                     read the purification manifest FILE, whose statements\n"
"                     overrule what elc concluded about the functions it\n"
"                     names. A manifest is read only when it is named here:\n"
"                     never from the working directory, the target, an\n"
"                     ancestor of either, or a dotfile. A manifest elc cannot\n"
"                     read, or that is not a manifest, ends the run; one that\n"
"                     names a function no analysed file defines is reported\n"
"                     and ignored\n"
"      --write-manifest\n"
"                     also write the purification manifest, beside the report\n"
"                     and named from it: an --output of report.md yields\n"
"                     report.manifest.json. Edit a classification you\n"
"                     disagree with and hand it back with --manifest.\n"
"                     Requires --output, since there is otherwise no name to\n"
"                     derive\n"
"      --purify-dot   also write the call graph as built and the purified\n"
"                     recovery view, as two Graphviz files beside the report\n"
"                     and named from it: an --output of report.md yields\n"
"                     report.raw.dot and report.purified.dot. The masked and\n"
"                     excluded nodes are drawn greyed and detached rather\n"
"                     than deleted, so the pair shows what purification did.\n"
"                     Requires --output, since there is otherwise no name to\n"
"                     derive\n"
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
"      --no-expand    measure the source as written, without expanding its\n",
	      stream);

	/* Split again, for the reason the break below records: the option list
	 * has outgrown what one literal may portably hold. */
	fputs(
"                     macros. Expansion runs the language's preprocessor\n"
"                     over each file and keeps only that file's own lines,\n"
"                     at their own line numbers; declining it returns the\n"
"                     figures a build with no toolchain would produce\n"
"      --cc PROGRAM   preprocessor to expand with, in place of gcc for C\n"
"                     and g++ for C++. A program that cannot be run falls\n"
"                     back per file, exactly as a missing one does\n"
"      --cc-flag ARG  pass ARG to the preprocessor, repeatable. Use it for\n"
"                     the include paths and defines the build needs, as in\n"
"                     --cc-flag -Iinclude; elc never invents one, so a\n"
"                     project whose headers are not beside its sources\n"
"                     expands only when told where they are\n"
"  -h, --help         display this help and exit\n",
	      stream);

	/* The fifth and last. Split, and not for style: one literal holding
	 * the whole summary exceeds the 4095 characters ISO C99 requires a
	 * compiler to support, and this build treats a warning as a defect. The
	 * break falls between the options and the prose so that adding an
	 * option does not move it. */
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
"  was judged against, with any custom-rule matches, what graph purification\n"
"  set aside, and the layering recovered from what remained. A recovered\n"
"  layering is a proposal and never a baseline: nothing is measured against\n"
"  it, and it takes effect only if you pass it back as --stratum arguments.\n"
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
       OPT_DSM, OPT_SINK_AUTHORITY, OPT_SINK_HUB, OPT_GOD_BETWEENNESS,
       OPT_GOD_HUB, OPT_CORE_DEPTH, OPT_MANIFEST, OPT_WRITE_MANIFEST,
       OPT_PURIFY_DOT, OPT_DBG, OPT_NO_EXPAND, OPT_CC, OPT_CC_FLAG };

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
		diag_printf("elc: '%s' is not a %s threshold\n", arg, what);
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
			diag_printf("elc: out of memory\n");
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

	diag_printf("elc: '%s' is not a format; expected table, csv, xml, "
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

/* The five purification thresholds (HLR-171).
 *
 * Every one of them is `elc`'s own heuristic rather than a published standard,
 * and each is adjustable for that reason: these are the values a user is most
 * likely to disagree with, because unlike a published threshold they rest on
 * nothing but this project's judgement.
 *
 * The four centrality thresholds are **rank positions** and are therefore
 * bounded at 100: a percentage above that names a position no node can occupy,
 * which is a usage error rather than a threshold that silently classifies
 * nothing. The core depth is a coreness and has no such ceiling.
 */
static int opt_percentile(const char *arg, const char *what, uint32_t *out)
{
	if (parse_threshold(arg, what, out) != 0)
		return CLI_ERROR;
	if (*out > 100) {
		diag_printf("elc: the %s threshold is a percentage of the "
		        "other functions; %s is above 100\n", what, arg);
		return CLI_ERROR;
	}
	return CLI_OK;
}

static int opt_sink_authority(const char *arg, CliParse *p)
{
	return opt_percentile(arg, "sink authority",
	                      &p->out->purify.sink_authority);
}

static int opt_sink_hub(const char *arg, CliParse *p)
{
	return opt_percentile(arg, "sink hub", &p->out->purify.sink_hub);
}

static int opt_god_betweenness(const char *arg, CliParse *p)
{
	return opt_percentile(arg, "god-object betweenness",
	                      &p->out->purify.god_betweenness);
}

static int opt_god_hub(const char *arg, CliParse *p)
{
	return opt_percentile(arg, "god-object hub", &p->out->purify.god_hub);
}

static int opt_core_depth(const char *arg, CliParse *p)
{
	if (parse_threshold(arg, "core depth", &p->out->purify.core_depth) != 0)
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

static int opt_dbg(const char *arg, CliParse *p)
{
	/* Recorded, not validated against --output, by the rule --graphml
	 * follows: a request for the debug log with the report on standard
	 * output produces no companion rather than a usage error, since there
	 * is no name to derive one from (HLR-119, HLR-194). */
	(void)arg;
	p->out->debug_log = true;
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

static int opt_manifest(const char *arg, CliParse *p)
{
	/* The path, borrowed from argv, and nothing else. The file is read by
	 * `purify.c`, which owns the failure, so the parser stays the module
	 * that reads argv rather than becoming one that reads files
	 * (LLR-CLI-22).
	 *
	 * **This is the only way a manifest is reached** (HLR-176). Nothing
	 * searches the working directory, the analysis target, an ancestor of
	 * either, or a dotfile: a manifest is read because the user named it,
	 * exactly as a custom rule file is, and the zero-configuration
	 * guarantee is unchanged by the option existing (HLR-039). */
	p->out->manifest_path = arg;
	return CLI_OK;
}

static int opt_write_manifest(const char *arg, CliParse *p)
{
	/* Recorded, not validated against --output, by the rule --graphml
	 * follows: the manifest is a companion artefact named from the report's
	 * own path, so a request made with the report on standard output writes
	 * nothing rather than failing (HLR-104, HLR-119, HLR-175). */
	(void)arg;
	p->out->write_manifest = true;
	return CLI_OK;
}

static int opt_purify_dot(const char *arg, CliParse *p)
{
	/* One flag for the pair of drawings, and the same companion rule again.
	 * A single drawing of the recovery view cannot show what purification
	 * acted on, so producing one without the other would answer half the
	 * question the option exists to answer (HLR-178). */
	(void)arg;
	p->out->purify_dot = true;
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

static int opt_no_expand(const char *arg, CliParse *p)
{
	/* A refusal, like --no-dot. Expansion is on by default (HLR-202), and
	 * declining it returns the tool to measuring source exactly as
	 * written — which is what every fallback already does per file, and
	 * what a reader wanting one machine's answer to match another's
	 * should reach for. */
	(void)arg;
	p->out->no_expand = true;
	return CLI_OK;
}

static int opt_cc(const char *arg, CliParse *p)
{
	/* Borrowed from argv, and taken unvalidated: whether the named driver
	 * exists is answered by trying to run it, and a run that cannot start
	 * one falls back per file exactly as a missing compiler does
	 * (HLR-205). Naming it on the command line rather than reading an
	 * environment variable is what keeps HLR-039 intact — behaviour is
	 * determined by the arguments and nothing else. */
	p->out->cc = arg;
	return CLI_OK;
}

static int opt_cc_flag(const char *arg, CliParse *p)
{
	/* Passed to the preprocessor unexamined. elc does not know what the
	 * build needs and will not guess (LLR-PRE-02); what a user states, it
	 * forwards. Repeatable, because one `-I` is rarely enough. */
	if (append_borrowed(&p->out->cc_flags, &p->out->cc_flag_count,
	                    &p->out->cc_flag_capacity, arg) != 0)
		return ELC_EXIT_FATAL;
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
		diag_printf("elc: -D requires a symbol name\n");
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
		diag_printf("elc: --elf requires the path of a linked image\n");
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
	{ OPT_SINK_AUTHORITY,   opt_sink_authority   },
	{ OPT_SINK_HUB,         opt_sink_hub         },
	{ OPT_GOD_BETWEENNESS,  opt_god_betweenness  },
	{ OPT_GOD_HUB,          opt_god_hub          },
	{ OPT_CORE_DEPTH,       opt_core_depth       },
	{ OPT_STRATUM,       opt_stratum       },
	{ OPT_STRATUM_ORDER, opt_stratum_order },
	{ OPT_SCOPE,         opt_scope         },
	{ OPT_GRAPHML,       opt_graphml       },
	{ OPT_DSM,           opt_dsm           },
	{ OPT_DBG,           opt_dbg           },
	{ OPT_MANIFEST,      opt_manifest      },
	{ OPT_WRITE_MANIFEST, opt_write_manifest },
	{ OPT_PURIFY_DOT,    opt_purify_dot    },
	{ OPT_NO_DOT,        opt_no_dot        },
	{ OPT_NO_EXPAND,     opt_no_expand     },
	{ OPT_CC,            opt_cc            },
	{ OPT_CC_FLAG,       opt_cc_flag       },
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
		diag_printf("elc: option '%s' requires an argument\n",
		        argv[optind - 1]);
	else if (optopt)
		diag_printf("elc: unrecognised option '-%c'\n", optopt);
	else
		diag_printf("elc: unrecognised option '%s'\n",
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
		diag_printf("elc: output file '%s' has no extension to name a "
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
			diag_printf("elc: --format %s and an output file named "
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
	diag_printf("elc: '.%s' is not a report format extension; expected %s\n",
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
		diag_printf("elc: --graphml cannot be combined with --from-xml: a "
		      "saved record carries the findings of a run, not the "
		      "graph they came from\n");
		return CLI_ERROR;
	}

	/* The same rule, for the three artefacts purification and recovery add.
	 * A record carries what a run concluded and not the graph it concluded
	 * it from, so there is nothing here to classify, to mask, or to draw —
	 * and a user who asked for a file and got none is told why rather than
	 * left to discover the absence (HLR-122, LLR-CLI-15). */
	if (p->out->purify_dot) {
		diag_printf("elc: --purify-dot cannot be combined with --from-xml: a "
		      "saved record carries the findings of a run, not the "
		      "graph they came from\n");
		return CLI_ERROR;
	}

	if (p->out->write_manifest) {
		diag_printf("elc: --write-manifest cannot be combined with "
		      "--from-xml: a saved record carries the classifications a "
		      "run made, not the graph it made them over\n");
		return CLI_ERROR;
	}

	if (p->out->manifest_path) {
		diag_printf("elc: --manifest cannot be combined with --from-xml: a "
		      "saved record already carries the classifications it was "
		      "written with, and nothing here recomputes them\n");
		return CLI_ERROR;
	}

	/* Pruning happens when a file is measured, not when a report is
	 * rendered, so a record already describes one configuration and cannot
	 * be re-cut into another. Rejected rather than ignored, so that a user
	 * who named a configuration and got a different one is told (HLR-136,
	 * LLR-CLI-24). */
	if (p->out->define_count > 0) {
		diag_printf("elc: -D cannot be combined with --from-xml: a saved "
		      "record already describes one configuration, chosen when "
		      "it was written\n");
		return CLI_ERROR;
	}

	/* The same rule the definitions get, and for the same reason: the
	 * filter is applied when a file is measured, not when a report is
	 * rendered, so a record already describes the run that was filtered and
	 * cannot be re-cut against another image (HLR-147, LLR-CLI-23). */
	if (p->out->image_path) {
		diag_printf("elc: --elf cannot be combined with --from-xml: a saved "
		      "record already describes one filtered run, chosen when "
		      "it was written\n");
		return CLI_ERROR;
	}

	/* A saved record is the input, so a target would name a second source
	 * of truth for the same report. */
	if (optind < argc) {
		diag_printf("elc: --from-xml takes no target, but '%s' was "
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
			diag_printf("elc: --from-xml produces Markdown, but the "
			        "output file '%s' names %s; a saved record can "
			        "be regenerated as Markdown alone\n",
			        p->out->output_path,
			        FORMAT_NAMES[p->out->format]);
		else
			diag_printf("elc: --from-xml produces Markdown; no other "
			      "format can be regenerated from a saved "
			      "record\n");
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
		{ "dbg",                  no_argument,       NULL, OPT_DBG },
		{ "dsm",                  no_argument,       NULL, OPT_DSM },
		{ "manifest",             required_argument, NULL, OPT_MANIFEST },
		{ "write-manifest",       no_argument,       NULL, OPT_WRITE_MANIFEST },
		{ "purify-dot",           no_argument,       NULL, OPT_PURIFY_DOT },
		{ "no-dot",               no_argument,       NULL, OPT_NO_DOT },
		{ "no-expand",            no_argument,       NULL, OPT_NO_EXPAND },
		{ "cc",                   required_argument, NULL, OPT_CC },
		{ "cc-flag",              required_argument, NULL, OPT_CC_FLAG },
		{ "verbose",              no_argument,       NULL, 'v' },
		{ "entry",                required_argument, NULL, OPT_ENTRY },
		{ "rules",                required_argument, NULL, OPT_RULES },
		{ "elf",                  required_argument, NULL, OPT_ELF },
		{ "define",               required_argument, NULL, 'D' },
		{ "scope",                required_argument, NULL, OPT_SCOPE },
		{ "bottleneck-threshold", required_argument, NULL, 'b' },
		{ "sink-authority",       required_argument, NULL, OPT_SINK_AUTHORITY },
		{ "sink-hub",             required_argument, NULL, OPT_SINK_HUB },
		{ "god-betweenness",      required_argument, NULL, OPT_GOD_BETWEENNESS },
		{ "god-hub",              required_argument, NULL, OPT_GOD_HUB },
		{ "core-depth",           required_argument, NULL, OPT_CORE_DEPTH },
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
	/* The purification thresholds in force where the user states none. All
	 * five are `elc`'s own and are reported as such wherever a
	 * classification made against them is presented (HLR-171). */
	out->purify.sink_authority  = ELC_DEFAULT_SINK_AUTHORITY;
	out->purify.sink_hub        = ELC_DEFAULT_SINK_HUB;
	out->purify.god_betweenness = ELC_DEFAULT_GOD_BETWEENNESS;
	out->purify.god_hub         = ELC_DEFAULT_GOD_HUB;
	out->purify.core_depth      = ELC_DEFAULT_CORE_DEPTH;

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
		diag_printf("elc: no target given\n");
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
	free((void *)opts->cc_flags);
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
