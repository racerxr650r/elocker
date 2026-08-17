/* format_xml.c — the saved record.
 *
 * Writes the complete XML record of a run, and reads one back to drive the
 * regeneration mode (doc/SDD.md §16).
 *
 * **Writing is hand-rolled; reading is streamed through a parser.** The
 * asymmetry is deliberate. Emission needs only correct escaping, which
 * `write_escaped` provides in one place, so a writer library would add a
 * dependency for no benefit. Ingestion needs a hardened parser, because the
 * input is a file the user supplies and may not be one `elc` wrote.
 *
 * **The reader reconstructs inputs, not conclusions.** It rebuilds the
 * per-file and per-function facts and then hands them to `report_assemble` —
 * the same function a live run uses. Every derived thing (the totals, the
 * per-language breakdown, the callouts and their tie-break, the threshold
 * listing, the ordering) is therefore computed by the same code on both
 * paths. That is what makes HLR-056's byte-identical guarantee structural
 * rather than a property two pieces of code have to keep agreeing on.
 *
 * The totals are written anyway, for a consumer that reads the record with
 * something other than `elc` (LLR-XWR-02). They are ignored on read.
 */

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <expat.h>

#include "analyze.h"
#include "calltree.h"
#include "discover.h"
#include "elc.h"
#include "format_xml.h"
#include "report.h"
#include "thresholds.h"

#define XML_READ_CHUNK 8192

/* Expat's handlers are conventionally declared with its XMLCALL macro, which
 * names a calling convention on Windows and expands to nothing everywhere
 * else. It is omitted here for two reasons: `elc` is POSIX-only, so the
 * convention it selects never applies; and tree-sitter-c parses a macro
 * sitting between a return type and a function name as an error, which would
 * make this file unreadable by the tool it is part of (doc/notes.md §3). */

/* ------------------------------------------------------------- writing -- */

void write_escaped(const char *value, FILE *out)
{
	if (!value)
		return;

	for (const char *p = value; *p; p++) {
		switch (*p) {
		case '&':  fputs("&amp;",  out); break;
		case '<':  fputs("&lt;",   out); break;
		case '>':  fputs("&gt;",   out); break;
		case '"':  fputs("&quot;", out); break;
		case '\'': fputs("&apos;", out); break;
		default:   fputc(*p, out);       break;
		}
	}
}

/* An attribute whose value is escaped. Every attribute goes through this,
 * so a path or an identifier carrying a quote cannot end the attribute
 * early and leave the document unparseable (HLR-065, LLR-ESC-02). */
static void write_attribute(FILE *out, const char *name, const char *value)
{
	fprintf(out, " %s=\"", name);
	write_escaped(value, out);
	fputc('"', out);
}

int xml_write_report(const Report *report, FILE *out)
{
	fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", out);
	fprintf(out, "<elc-report format-version=\"%d\">\n",
	        ELC_XML_FORMAT_VERSION);

	/* Derived, and written for a consumer that is not elc. The reader
	 * recomputes all of it rather than trusting it. */
	fputs("  <summary", out);
	fprintf(out, " files=\"%zu\"", report->summary.file_count);
	fprintf(out, " physical-lines=\"%" PRIu64 "\"",
	        report->summary.physical_lines);
	fprintf(out, " eloc=\"%" PRIu64 "\"", report->summary.eloc);
	fprintf(out, " functions=\"%" PRIu64 "\"",
	        report->summary.function_count);
	fputs("/>\n", out);

	fputs("  <languages>\n", out);
	for (size_t i = 0; i < report->languages.count; i++) {
		const LanguageTotals *l = &report->languages.items[i];

		fputs("    <language", out);
		write_attribute(out, "name", l->language);
		fprintf(out, " files=\"%zu\" physical-lines=\"%" PRIu64
		        "\" eloc=\"%" PRIu64 "\"/>\n", l->file_count,
		        l->physical_lines, l->eloc);
	}
	fputs("  </languages>\n", out);

	fputs("  <files>\n", out);
	for (size_t i = 0; i < report->file_count; i++) {
		const FileMetrics *f = report->files[i];

		fputs("    <file", out);
		write_attribute(out, "path", f->path);
		write_attribute(out, "language", f->language ? f->language : "");
		fprintf(out, " physical-lines=\"%" PRIu32 "\" eloc=\"%" PRIu32
		        "\" unparsed-lines=\"%" PRIu32 "\">\n",
		        f->physical_lines, f->eloc, f->unparsed_lines);

		for (size_t j = 0; j < f->function_count; j++) {
			const FunctionMetric *fn = &f->functions[j];

			fputs("      <function", out);
			write_attribute(out, "name", fn->name);
			fprintf(out, " start-line=\"%" PRIu32 "\" end-line=\"%"
			        PRIu32 "\" eloc=\"%" PRIu32 "\" complexity=\"%"
			        PRIu32 "\"/>\n", fn->start_line, fn->end_line,
			        fn->eloc, fn->complexity);
		}

		fputs("    </file>\n", out);
	}
	fputs("  </files>\n", out);

	/* Carried because the report presents it. A record that omitted the
	 * routes would regenerate into a report with an empty Discovery
	 * section, which is a different report — and HLR-056 says it must not
	 * be (LLR-XWR-06). */
	/* A measurement of the run, so it lives in the record beside the
	 * others: it cannot be recomputed later, since regeneration has no
	 * graph and no source to build one from (HLR-054, HLR-056). */
	fprintf(out, "  <graph unresolved-calls=\"%zu\"/>\n",
	        report->unresolved_calls);

	/* The call-tree measurements. Every one is a fact about the run that
	 * regeneration cannot recompute — there is no graph and no source to
	 * build one from — so the record carries them exactly as it carries
	 * the metrics (HLR-054, HLR-056). */
	fprintf(out, "  <calltree depth-state=\"%d\" depth=\"%" PRIu32 "\">\n",
	        (int)report->depth_state, report->depth);
	for (size_t i = 0; i < report->fan_out_count; i++) {
		fputs("    <fanout", out);
		write_attribute(out, "function", report->fan_out[i].function);
		write_attribute(out, "file", report->fan_out[i].file);
		fprintf(out, " line=\"%" PRIu32 "\" value=\"%" PRIu32 "\"/>\n",
		        report->fan_out[i].line, report->fan_out[i].fan_out);
	}
	for (size_t i = 0; i < report->cycle_count; i++) {
		fputs("    <cycle>\n", out);
		for (size_t m = 0; m < report->cycles[i].count; m++) {
			fputs("      <member", out);
			write_attribute(out, "function",
			                report->cycles[i].members[m]);
			fputs("/>\n", out);
		}
		fputs("    </cycle>\n", out);
	}
	for (size_t i = 0; i < report->deepest_count; i++) {
		fputs("    <step", out);
		write_attribute(out, "function", report->deepest[i].function);
		write_attribute(out, "file", report->deepest[i].file);
		fprintf(out, " line=\"%" PRIu32 "\"/>\n",
		        report->deepest[i].line);
	}
	fputs("  </calltree>\n", out);

	/* The global-state and reachability measurements. Carried for the same
	 * reason the call-tree ones are: regeneration has no graph and no
	 * source to build one from, so a value not written here is a value the
	 * regenerated report cannot have (HLR-054, HLR-056).
	 *
	 * The attribution of a verdict is deliberately *not* written. It is
	 * derived from the verdict by one function both paths call, so a
	 * record cannot carry a citation that disagrees with a live run's
	 * (LLR-GLB-04). */
	fprintf(out, "  <state reach-state=\"%d\" scope-state=\"%d\">\n",
	        (int)report->reach_state, (int)report->scope_state);
	for (size_t i = 0; i < report->global_state_count; i++) {
		const GlobalStateRow *r = &report->global_state[i];

		fputs("    <global", out);
		write_attribute(out, "object", r->object);
		write_attribute(out, "writers", r->writers);
		write_attribute(out, "readers", r->readers);
		write_attribute(out, "participants", r->participants);
		fprintf(out, " verdict=\"%d\"/>\n", (int)r->verdict);
	}
	for (size_t i = 0; i < report->unreachable_count; i++) {
		fputs("    <unreachable-function", out);
		write_attribute(out, "function", report->unreachable[i].function);
		write_attribute(out, "file", report->unreachable[i].file);
		fprintf(out, " line=\"%" PRIu32 "\"/>\n",
		        report->unreachable[i].line);
	}
	for (size_t i = 0; i < report->unreachable_global_count; i++) {
		fputs("    <unreachable-global", out);
		write_attribute(out, "object", report->unreachable_globals[i]);
		fputs("/>\n", out);
	}
	for (size_t i = 0; i < report->cross_scope_count; i++) {
		const CrossScopeRow *r = &report->cross_scope[i];

		fputs("    <cross-scope", out);
		write_attribute(out, "from-scope", r->from_scope);
		write_attribute(out, "from", r->from_function);
		write_attribute(out, "to-scope", r->to_scope);
		write_attribute(out, "to", r->to_function);
		write_attribute(out, "object", r->object ? r->object : "");
		fputs("/>\n", out);
	}
	fputs("  </state>\n", out);

	/* The languages dead code was *not* looked for in are written beside
	 * the findings, because the two together are the claim: a record
	 * carrying the spans alone would regenerate into a report that reads
	 * as a clean bill of health for a language nobody analysed
	 * (HLR-139). */
	fputs("  <deadcode>\n", out);
	for (size_t i = 0; i < report->dead_unanalysed.count; i++) {
		fputs("    <unanalysed", out);
		write_attribute(out, "language", report->dead_unanalysed.paths[i]);
		fputs("/>\n", out);
	}
	for (size_t i = 0; i < report->dead_count; i++) {
		const DeadRow *r = &report->dead[i];

		fputs("    <span", out);
		write_attribute(out, "file", r->file);
		write_attribute(out, "function", r->function);
		fprintf(out, " start-line=\"%" PRIu32 "\" end-line=\"%" PRIu32
		        "\" cause=\"%d\"/>\n", r->start_line, r->end_line,
		        (int)r->cause);
	}
	fputs("  </deadcode>\n", out);

	/* The component-level measurements. Carried for the reason every other
	 * analysis result is: regeneration has no graph and no source tree to
	 * rebuild one from, so a value not written here is one the regenerated
	 * report cannot have (HLR-054, HLR-056).
	 *
	 * The attributions are absent by the same rule the global-state
	 * citation is: both are derived from one function each path calls, so
	 * a record cannot carry an attribution that disagrees with a live
	 * run's. */
	fprintf(out, "  <architecture strata-state=\"%d\" bottleneck-threshold=\"%"
	        PRIu32 "\">\n", (int)report->strata_state,
	        report->bottleneck_threshold);
	for (size_t i = 0; i < report->coupling_count; i++) {
		const CouplingRow *r = &report->coupling[i];

		fputs("    <coupling", out);
		write_attribute(out, "component", r->component);
		write_attribute(out, "instability", r->instability);
		fprintf(out, " ca=\"%" PRIu32 "\" ce=\"%" PRIu32
		        "\" bottleneck=\"%d\"/>\n", r->ca, r->ce,
		        r->bottleneck ? 1 : 0);
	}
	for (size_t i = 0; i < report->dep_cycle_count; i++) {
		fputs("    <dependency-cycle", out);
		write_attribute(out, "components",
		                report->dep_cycles[i].components);
		write_attribute(out, "path", report->dep_cycles[i].path);
		fputs("/>\n", out);
	}
	for (size_t i = 0; i < report->layering_count; i++) {
		const LayeringRow *r = &report->layering[i];

		fputs("    <layering", out);
		write_attribute(out, "from-stratum", r->from_stratum);
		write_attribute(out, "from", r->from_function);
		write_attribute(out, "from-file", r->from_file);
		write_attribute(out, "to-stratum", r->to_stratum);
		write_attribute(out, "to", r->to_function);
		write_attribute(out, "to-file", r->to_file);
		fprintf(out, " layers=\"%" PRIu32 "\" kind=\"%d\"/>\n",
		        r->layers_crossed, (int)r->kind);
	}
	fputs("  </architecture>\n", out);

	/* The findings. Carried because regeneration has no measurements to
	 * re-band and no catalogue call to make against them, exactly as the
	 * measurements themselves are (HLR-054, HLR-056).
	 *
	 * The source *is* written here, unlike the verdict citations elsewhere
	 * in this record. Those are derived from a verdict the record already
	 * carries; a finding's source is derived from its measurement kind,
	 * which the record would otherwise have to carry as a number whose
	 * meaning could shift between builds. A string that reads the same in
	 * both is the more durable of the two. */
	fputs("  <findings>\n", out);
	for (size_t i = 0; i < report->finding_count; i++) {
		const FindingRow *r = &report->findings[i];

		fputs("    <finding", out);
		write_attribute(out, "severity", r->severity);
		write_attribute(out, "measurement", r->measurement);
		write_attribute(out, "subject", r->subject);
		write_attribute(out, "where", r->where);
		write_attribute(out, "detail", r->detail);
		write_attribute(out, "source", r->source);
		fprintf(out, " line=\"%" PRIu32 "\"/>\n", r->line);
	}
	fputs("  </findings>\n", out);

	fputs("  <discovery>\n", out);
	for (size_t i = 0; i < report->routes.count; i++) {
		fputs("    <route", out);
		write_attribute(out, "target", report->routes.items[i].target);
		fprintf(out, " via=\"%s\"/>\n",
		        report->routes.items[i].route == ROUTE_REPOSITORY
		                ? "repository" : "filesystem");
	}
	fputs("  </discovery>\n", out);

	fputs("  <skipped>\n", out);
	for (size_t i = 0; i < report->skipped_files.count; i++) {
		fputs("    <file", out);
		write_attribute(out, "path", report->skipped_files.paths[i]);
		fputs("/>\n", out);
	}
	fputs("  </skipped>\n", out);

	/* The architectural findings, custom-rule matches, and omission
	 * notices are written here once the analyses that produce them exist.
	 * The elements are absent rather than empty, so that adding them is an
	 * addition a reader of an older record ignores — which is why the
	 * format version marks removals and meaning changes only. */

	fputs("</elc-report>\n", out);

	if (fflush(out) != 0 || ferror(out))
		return -1;

	return 0;
}

/* ------------------------------------------------------------- reading -- */

typedef struct {
	RouteList           routes;
	size_t              unresolved;
	/* Rebuilt into the report after assembly, exactly as a live run does
	 * it, so the two paths converge on one model (HLR-056). */
	DepthState          depth_state;
	uint32_t            depth;
	FanOutRow          *fan_out;
	size_t              fan_out_count;
	CycleRow           *cycles;
	size_t              cycle_count;
	ChainRow           *deepest;
	size_t              deepest_count;
	ReachState          reach_state;
	ScopeState          scope_state;
	GlobalStateRow     *global_state;
	size_t              global_state_count;
	UnreachableRow     *unreachable;
	size_t              unreachable_count;
	char              **unreachable_globals;
	size_t              unreachable_global_count;
	CrossScopeRow      *cross_scope;
	size_t              cross_scope_count;
	FindingRow         *findings;
	size_t              finding_count;
	CouplingRow        *coupling;
	size_t              coupling_count;
	uint32_t            bottleneck_threshold;
	CycleDependencyRow *dep_cycles;
	size_t              dep_cycle_count;
	StrataState         strata_state;
	LayeringRow        *layering;
	size_t              layering_count;
	DeadRow            *dead;
	size_t              dead_count;
	PathList            dead_unanalysed;
	MetricsAccumulator *acc;
	FileMetrics        *current;      /* the <file> being populated */
	size_t              capacity;     /* of current->functions      */
	bool                saw_root;
	bool                failed;
	const char         *path;         /* for diagnostics            */
	const char         *reason;       /* what was wrong, if failed  */
	char                detail[160];  /* when the reason needs one  */
} ReadState;

static void fail(ReadState *state, const char *reason)
{
	if (state->failed)
		return;
	state->failed = true;
	state->reason = reason;
}

static const char *attribute(const XML_Char **atts, const char *name)
{
	for (size_t i = 0; atts[i]; i += 2)
		if (strcmp(atts[i], name) == 0)
			return atts[i + 1];
	return NULL;
}

/* An unsigned attribute, defaulting to zero when absent.
 *
 * A value present but not a number is a malformed record rather than a zero:
 * accepting it would produce a report that renders cleanly and is wrong,
 * which is the outcome HLR-058 exists to prevent.
 */
static uint32_t uint_attribute(ReadState *state, const XML_Char **atts,
                               const char *name)
{
	const char   *text = attribute(atts, name);
	char         *end  = NULL;
	unsigned long value;

	if (!text)
		return 0;

	errno = 0;
	value = strtoul(text, &end, 10);
	if (text[0] < '0' || text[0] > '9' || !end || *end != '\0' ||
	    errno == ERANGE || value > UINT32_MAX) {
		fail(state, "a numeric attribute is not a number");
		return 0;
	}

	return (uint32_t)value;
}

static void on_start(void *user, const XML_Char *name,
                             const XML_Char **atts)
{
	ReadState *state = user;

	if (state->failed)
		return;

	if (!state->saw_root) {
		/* The first element decides whether this is a record at all.
		 * A well-formed document of some other shape is rejected here,
		 * before anything is reconstructed from it (LLR-XRD-04). */
		if (strcmp(name, "elc-report") != 0) {
			fail(state, "not an elc report");
			return;
		}
		state->saw_root = true;

		const char *version = attribute(atts, "format-version");

		if (!version) {
			fail(state, "no format-version identifier");
			return;
		}
		if (atoi(version) != ELC_XML_FORMAT_VERSION) {
			/* Naming both versions is the difference between a
			 * message a user can act on and one that only says no
			 * (LLR-XRD-05). */
			snprintf(state->detail, sizeof state->detail,
			         "format version %s is not supported; this "
			         "build reads version %d", version,
			         ELC_XML_FORMAT_VERSION);
			fail(state, state->detail);
			return;
		}
		return;
	}

	if (strcmp(name, "file") == 0 && !state->current) {
		const char *path = attribute(atts, "path");

		if (!path) {
			fail(state, "a file element has no path");
			return;
		}

		/* A <file> inside <skipped> carries a path and nothing else;
		 * one inside <files> carries metrics. They are told apart by
		 * their attributes rather than by tracking the parent, which
		 * would need a stack for one distinction. */
		if (!attribute(atts, "physical-lines")) {
			if (metrics_add_skipped(state->acc, path) != 0)
				fail(state, "out of memory");
			return;
		}

		FileMetrics *file = calloc(1, sizeof *file);

		if (!file) {
			fail(state, "out of memory");
			return;
		}

		file->path = strdup(path);
		if (!file->path) {
			free(file);
			fail(state, "out of memory");
			return;
		}

		const char *language = attribute(atts, "language");

		if (language && *language) {
			file->language = strdup(language);
			if (!file->language) {
				filemetrics_free(file);
				fail(state, "out of memory");
				return;
			}
		}

		file->physical_lines = uint_attribute(state, atts,
		                                      "physical-lines");
		file->eloc           = uint_attribute(state, atts, "eloc");
		/* Absent in a record written before the field existed, which
		 * reads back as zero — no damage — and is the right answer for
		 * a build that could not have measured any. */
		file->unparsed_lines = uint_attribute(state, atts,
		                                      "unparsed-lines");

		state->current  = file;
		state->capacity = 0;
		return;
	}

	if (strcmp(name, "calltree") == 0) {
		const char *which = attribute(atts, "depth-state");
		const char *depth = attribute(atts, "depth");

		if (!which || !depth) {
			fail(state, "a calltree element is incomplete");
			return;
		}
		state->depth_state = (DepthState)strtol(which, NULL, 10);
		state->depth       = (uint32_t)strtoul(depth, NULL, 10);
		return;
	}

	if (strcmp(name, "fanout") == 0) {
		const char *fn   = attribute(atts, "function");
		const char *file = attribute(atts, "file");
		const char *line = attribute(atts, "line");
		const char *val  = attribute(atts, "value");

		if (!fn || !file || !line || !val) {
			fail(state, "a fanout element is incomplete");
			return;
		}

		FanOutRow *grown = realloc(state->fan_out,
		                           (state->fan_out_count + 1) *
		                                   sizeof *grown);

		if (!grown) {
			fail(state, "out of memory");
			return;
		}
		state->fan_out = grown;

		FanOutRow *row = &state->fan_out[state->fan_out_count];

		memset(row, 0, sizeof *row);
		row->function = strdup(fn);
		row->file     = strdup(file);
		if (!row->function || !row->file) {
			fail(state, "out of memory");
			return;
		}
		row->line    = (uint32_t)strtoul(line, NULL, 10);
		row->fan_out = (uint32_t)strtoul(val, NULL, 10);
		state->fan_out_count++;
		return;
	}

	if (strcmp(name, "cycle") == 0) {
		CycleRow *grown = realloc(state->cycles,
		                          (state->cycle_count + 1) *
		                                  sizeof *grown);

		if (!grown) {
			fail(state, "out of memory");
			return;
		}
		state->cycles = grown;
		memset(&state->cycles[state->cycle_count], 0,
		       sizeof *state->cycles);
		state->cycle_count++;
		return;
	}

	if (strcmp(name, "member") == 0) {
		const char *fn = attribute(atts, "function");

		if (!fn || state->cycle_count == 0) {
			fail(state, "a cycle member outside any cycle");
			return;
		}

		CycleRow *row    = &state->cycles[state->cycle_count - 1];
		char    **members = realloc(row->members,
		                            (row->count + 1) * sizeof *members);

		if (!members) {
			fail(state, "out of memory");
			return;
		}
		row->members = members;
		row->members[row->count] = strdup(fn);
		if (!row->members[row->count]) {
			fail(state, "out of memory");
			return;
		}
		row->count++;
		return;
	}

	if (strcmp(name, "step") == 0) {
		const char *fn   = attribute(atts, "function");
		const char *file = attribute(atts, "file");
		const char *line = attribute(atts, "line");

		if (!fn || !file || !line) {
			fail(state, "a step element is incomplete");
			return;
		}

		ChainRow *grown = realloc(state->deepest,
		                          (state->deepest_count + 1) *
		                                  sizeof *grown);

		if (!grown) {
			fail(state, "out of memory");
			return;
		}
		state->deepest = grown;

		ChainRow *row = &state->deepest[state->deepest_count];

		memset(row, 0, sizeof *row);
		row->function = strdup(fn);
		row->file     = strdup(file);
		if (!row->function || !row->file) {
			fail(state, "out of memory");
			return;
		}
		row->line = (uint32_t)strtoul(line, NULL, 10);
		state->deepest_count++;
		return;
	}

	if (strcmp(name, "state") == 0) {
		const char *reach = attribute(atts, "reach-state");
		const char *scope = attribute(atts, "scope-state");

		if (!reach || !scope) {
			fail(state, "a state element is incomplete");
			return;
		}
		state->reach_state = (ReachState)strtol(reach, NULL, 10);
		state->scope_state = (ScopeState)strtol(scope, NULL, 10);
		return;
	}

	if (strcmp(name, "global") == 0) {
		const char *object  = attribute(atts, "object");
		const char *writers = attribute(atts, "writers");
		const char *readers = attribute(atts, "readers");
		const char *parts   = attribute(atts, "participants");
		const char *verdict = attribute(atts, "verdict");

		if (!object || !writers || !readers || !parts || !verdict) {
			fail(state, "a global element is incomplete");
			return;
		}

		GlobalStateRow *grown =
			realloc(state->global_state,
			        (state->global_state_count + 1) * sizeof *grown);

		if (!grown) {
			fail(state, "out of memory");
			return;
		}
		state->global_state = grown;

		GlobalStateRow *row = &state->global_state[state->global_state_count];

		memset(row, 0, sizeof *row);
		row->object       = strdup(object);
		row->writers      = strdup(writers);
		row->readers      = strdup(readers);
		row->participants = strdup(parts);
		if (!row->object || !row->writers || !row->readers ||
		    !row->participants) {
			fail(state, "out of memory");
			return;
		}
		row->verdict = (GlobalVerdict)strtol(verdict, NULL, 10);
		state->global_state_count++;
		return;
	}

	if (strcmp(name, "unreachable-function") == 0) {
		const char *fn   = attribute(atts, "function");
		const char *file = attribute(atts, "file");
		const char *line = attribute(atts, "line");

		if (!fn || !file || !line) {
			fail(state, "an unreachable-function element is incomplete");
			return;
		}

		UnreachableRow *grown =
			realloc(state->unreachable,
			        (state->unreachable_count + 1) * sizeof *grown);

		if (!grown) {
			fail(state, "out of memory");
			return;
		}
		state->unreachable = grown;

		UnreachableRow *row = &state->unreachable[state->unreachable_count];

		memset(row, 0, sizeof *row);
		row->function = strdup(fn);
		row->file     = strdup(file);
		if (!row->function || !row->file) {
			fail(state, "out of memory");
			return;
		}
		row->line = (uint32_t)strtoul(line, NULL, 10);
		state->unreachable_count++;
		return;
	}

	if (strcmp(name, "unreachable-global") == 0) {
		const char *object = attribute(atts, "object");

		if (!object) {
			fail(state, "an unreachable-global element has no object");
			return;
		}

		char **grown = realloc(state->unreachable_globals,
		                       (state->unreachable_global_count + 1) *
		                               sizeof *grown);

		if (!grown) {
			fail(state, "out of memory");
			return;
		}
		state->unreachable_globals = grown;
		state->unreachable_globals[state->unreachable_global_count] =
			strdup(object);
		if (!state->unreachable_globals[state->unreachable_global_count]) {
			fail(state, "out of memory");
			return;
		}
		state->unreachable_global_count++;
		return;
	}

	if (strcmp(name, "cross-scope") == 0) {
		const char *from_scope = attribute(atts, "from-scope");
		const char *from       = attribute(atts, "from");
		const char *to_scope   = attribute(atts, "to-scope");
		const char *to         = attribute(atts, "to");
		const char *object     = attribute(atts, "object");

		if (!from_scope || !from || !to_scope || !to || !object) {
			fail(state, "a cross-scope element is incomplete");
			return;
		}

		CrossScopeRow *grown =
			realloc(state->cross_scope,
			        (state->cross_scope_count + 1) * sizeof *grown);

		if (!grown) {
			fail(state, "out of memory");
			return;
		}
		state->cross_scope = grown;

		CrossScopeRow *row = &state->cross_scope[state->cross_scope_count];

		memset(row, 0, sizeof *row);
		row->from_scope    = strdup(from_scope);
		row->from_function = strdup(from);
		row->to_scope      = strdup(to_scope);
		row->to_function   = strdup(to);
		row->object        = strdup(object);
		if (!row->from_scope || !row->from_function || !row->to_scope ||
		    !row->to_function || !row->object) {
			fail(state, "out of memory");
			return;
		}
		state->cross_scope_count++;
		return;
	}

	if (strcmp(name, "unanalysed") == 0) {
		const char *language = attribute(atts, "language");

		if (!language) {
			fail(state, "an unanalysed element names no language");
			return;
		}

		PathList *list = &state->dead_unanalysed;

		if (list->count == list->capacity) {
			size_t next   = list->capacity ? list->capacity * 2 : 4;
			char **bigger = realloc(list->paths, next * sizeof *bigger);

			if (!bigger) {
				fail(state, "out of memory");
				return;
			}
			list->paths    = bigger;
			list->capacity = next;
		}
		list->paths[list->count] = strdup(language);
		if (!list->paths[list->count]) {
			fail(state, "out of memory");
			return;
		}
		list->count++;
		return;
	}

	if (strcmp(name, "span") == 0) {
		const char *file = attribute(atts, "file");
		const char *fn   = attribute(atts, "function");

		if (!file || !fn) {
			fail(state, "a span element is incomplete");
			return;
		}

		DeadRow *grown = realloc(state->dead,
		                         (state->dead_count + 1) * sizeof *grown);

		if (!grown) {
			fail(state, "out of memory");
			return;
		}
		state->dead = grown;

		DeadRow *row = &state->dead[state->dead_count];

		memset(row, 0, sizeof *row);
		row->file     = strdup(file);
		row->function = strdup(fn);
		if (!row->file || !row->function) {
			fail(state, "out of memory");
			return;
		}
		row->start_line = uint_attribute(state, atts, "start-line");
		row->end_line   = uint_attribute(state, atts, "end-line");
		row->cause      = (DeadCause)uint_attribute(state, atts, "cause");
		state->dead_count++;
		return;
	}

	if (strcmp(name, "finding") == 0) {
		const char *severity    = attribute(atts, "severity");
		const char *measurement = attribute(atts, "measurement");
		const char *subject     = attribute(atts, "subject");
		const char *where       = attribute(atts, "where");
		const char *detail      = attribute(atts, "detail");
		const char *source      = attribute(atts, "source");

		if (!severity || !measurement || !subject || !where ||
		    !detail || !source) {
			fail(state, "a finding element is incomplete");
			return;
		}

		FindingRow *grown = realloc(state->findings,
		                            (state->finding_count + 1) *
		                                    sizeof *grown);

		if (!grown) {
			fail(state, "out of memory");
			return;
		}
		state->findings = grown;

		FindingRow *row = &state->findings[state->finding_count];

		memset(row, 0, sizeof *row);
		row->severity    = strdup(severity);
		row->measurement = strdup(measurement);
		row->subject     = strdup(subject);
		row->where       = strdup(where);
		row->detail      = strdup(detail);
		row->source      = strdup(source);
		if (!row->severity || !row->measurement || !row->subject ||
		    !row->where || !row->detail || !row->source) {
			fail(state, "out of memory");
			return;
		}
		row->line = uint_attribute(state, atts, "line");
		state->finding_count++;
		return;
	}

	if (strcmp(name, "architecture") == 0) {
		const char *strata = attribute(atts, "strata-state");

		if (!strata) {
			fail(state, "an architecture element is incomplete");
			return;
		}
		state->strata_state = (StrataState)strtol(strata, NULL, 10);
		state->bottleneck_threshold =
			uint_attribute(state, atts, "bottleneck-threshold");
		return;
	}

	if (strcmp(name, "coupling") == 0) {
		const char *component   = attribute(atts, "component");
		const char *instability = attribute(atts, "instability");
		const char *bottleneck  = attribute(atts, "bottleneck");

		if (!component || !instability || !bottleneck) {
			fail(state, "a coupling element is incomplete");
			return;
		}

		CouplingRow *grown = realloc(state->coupling,
		                             (state->coupling_count + 1) *
		                                     sizeof *grown);

		if (!grown) {
			fail(state, "out of memory");
			return;
		}
		state->coupling = grown;

		CouplingRow *row = &state->coupling[state->coupling_count];

		memset(row, 0, sizeof *row);
		row->component   = strdup(component);
		row->instability = strdup(instability);
		if (!row->component || !row->instability) {
			fail(state, "out of memory");
			return;
		}
		row->ca         = uint_attribute(state, atts, "ca");
		row->ce         = uint_attribute(state, atts, "ce");
		row->bottleneck = strcmp(bottleneck, "0") != 0;
		state->coupling_count++;
		return;
	}

	if (strcmp(name, "dependency-cycle") == 0) {
		const char *components = attribute(atts, "components");
		const char *path       = attribute(atts, "path");

		if (!components || !path) {
			fail(state, "a dependency-cycle element is incomplete");
			return;
		}

		CycleDependencyRow *grown =
			realloc(state->dep_cycles,
			        (state->dep_cycle_count + 1) * sizeof *grown);

		if (!grown) {
			fail(state, "out of memory");
			return;
		}
		state->dep_cycles = grown;

		CycleDependencyRow *row =
			&state->dep_cycles[state->dep_cycle_count];

		memset(row, 0, sizeof *row);
		row->components = strdup(components);
		row->path       = strdup(path);
		if (!row->components || !row->path) {
			fail(state, "out of memory");
			return;
		}
		state->dep_cycle_count++;
		return;
	}

	if (strcmp(name, "layering") == 0) {
		const char *from_stratum = attribute(atts, "from-stratum");
		const char *from         = attribute(atts, "from");
		const char *from_file    = attribute(atts, "from-file");
		const char *to_stratum   = attribute(atts, "to-stratum");
		const char *to           = attribute(atts, "to");
		const char *to_file      = attribute(atts, "to-file");
		const char *kind         = attribute(atts, "kind");

		if (!from_stratum || !from || !from_file || !to_stratum ||
		    !to || !to_file || !kind) {
			fail(state, "a layering element is incomplete");
			return;
		}

		LayeringRow *grown = realloc(state->layering,
		                             (state->layering_count + 1) *
		                                     sizeof *grown);

		if (!grown) {
			fail(state, "out of memory");
			return;
		}
		state->layering = grown;

		LayeringRow *row = &state->layering[state->layering_count];

		memset(row, 0, sizeof *row);
		row->from_stratum  = strdup(from_stratum);
		row->from_function = strdup(from);
		row->from_file     = strdup(from_file);
		row->to_stratum    = strdup(to_stratum);
		row->to_function   = strdup(to);
		row->to_file       = strdup(to_file);
		if (!row->from_stratum || !row->from_function ||
		    !row->from_file || !row->to_stratum || !row->to_function ||
		    !row->to_file) {
			fail(state, "out of memory");
			return;
		}
		row->layers_crossed = uint_attribute(state, atts, "layers");
		row->kind           = (LayerViolationKind)strtol(kind, NULL, 10);
		state->layering_count++;
		return;
	}

	if (strcmp(name, "graph") == 0) {
		const char *value = attribute(atts, "unresolved-calls");

		if (!value) {
			fail(state, "a graph element is incomplete");
			return;
		}
		state->unresolved = strtoull(value, NULL, 10);
		return;
	}

	if (strcmp(name, "route") == 0) {
		const char *target = attribute(atts, "target");
		const char *via    = attribute(atts, "via");

		if (!target || !via) {
			fail(state, "a route element is incomplete");
			return;
		}

		/* Named exhaustively rather than defaulted. A record carrying
		 * an unrecognised route would otherwise regenerate as
		 * "filesystem", which is not an unknown answer but a confident
		 * wrong one — in the section whose whole purpose is explaining
		 * a surprising file set. */
		DiscoveryRoute route;

		if (strcmp(via, "repository") == 0)
			route = ROUTE_REPOSITORY;
		else if (strcmp(via, "filesystem") == 0)
			route = ROUTE_FILESYSTEM;
		else {
			fail(state, "a route element names an unknown route");
			return;
		}

		if (routelist_add(&state->routes, target, route) != 0)
			fail(state, "out of memory");
		return;
	}

	if (strcmp(name, "function") == 0) {
		if (!state->current) {
			fail(state, "a function outside any file");
			return;
		}

		const char *fname = attribute(atts, "name");

		if (!fname) {
			fail(state, "a function element has no name");
			return;
		}

		if (state->current->function_count == state->capacity) {
			size_t          next = state->capacity ? state->capacity * 2 : 8;
			FunctionMetric *bigger =
				realloc(state->current->functions,
				        next * sizeof *bigger);

			if (!bigger) {
				fail(state, "out of memory");
				return;
			}
			state->current->functions = bigger;
			state->capacity           = next;
		}

		FunctionMetric *fn =
			&state->current->functions[state->current->function_count];

		memset(fn, 0, sizeof *fn);
		fn->name = strdup(fname);
		if (!fn->name) {
			fail(state, "out of memory");
			return;
		}
		fn->start_line = uint_attribute(state, atts, "start-line");
		fn->end_line   = uint_attribute(state, atts, "end-line");
		fn->eloc       = uint_attribute(state, atts, "eloc");
		fn->complexity = uint_attribute(state, atts, "complexity");

		state->current->function_count++;
		return;
	}

	/* Anything else is ignored. A record written by a newer build of the
	 * same format version may carry elements this one does not know, and
	 * ignoring them is what makes an addition a compatible change. */
}

static void on_end(void *user, const XML_Char *name)
{
	ReadState *state = user;

	if (state->failed || strcmp(name, "file") != 0 || !state->current)
		return;

	if (metrics_add(state->acc, state->current) != 0) {
		filemetrics_free(state->current);
		fail(state, "out of memory");
	}
	state->current = NULL;
}

int xml_read_report(const char *path, const ElcOptions *opts, Report *out)
{
	MetricsAccumulator acc    = { 0 };
	ReadState          state  = { 0 };
	XML_Parser         parser = NULL;
	FILE              *fp     = NULL;
	int                status = -1;

	memset(out, 0, sizeof *out);

	state.acc  = &acc;
	state.path = path;

	fp = fopen(path, "rb");
	if (!fp) {
		fprintf(stderr, "elc: %s: %s\n", path, strerror(errno));
		goto cleanup;
	}

	parser = XML_ParserCreate(NULL);
	if (!parser) {
		fputs("elc: out of memory creating the XML parser\n", stderr);
		goto cleanup;
	}

	XML_SetUserData(parser, &state);
	XML_SetElementHandler(parser, on_start, on_end);

	for (;;) {
		void  *buffer = XML_GetBuffer(parser, XML_READ_CHUNK);
		size_t got;

		if (!buffer) {
			fputs("elc: out of memory reading the record\n", stderr);
			goto cleanup;
		}

		got = fread(buffer, 1, XML_READ_CHUNK, fp);
		if (ferror(fp)) {
			fprintf(stderr, "elc: %s: %s\n", path, strerror(errno));
			goto cleanup;
		}

		if (XML_ParseBuffer(parser, (int)got, got == 0) ==
		    XML_STATUS_ERROR) {
			fprintf(stderr, "elc: %s:%lu: %s\n", path,
			        XML_GetCurrentLineNumber(parser),
			        XML_ErrorString(XML_GetErrorCode(parser)));
			goto cleanup;
		}

		if (state.failed) {
			fprintf(stderr, "elc: %s: %s\n", path, state.reason);
			goto cleanup;
		}

		if (got == 0)
			break;
	}

	if (!state.saw_root) {
		fprintf(stderr, "elc: %s: not an elc report\n", path);
		goto cleanup;
	}

	/* Assembled by the same function a live run uses, so every derived
	 * value is derived once and the two paths cannot drift (HLR-056). */
	if (report_assemble(&acc, &state.routes, opts, out) != 0)
		goto cleanup;
	report_set_unresolved(out, state.unresolved);

	/* Moved, not copied. The parser owns these until assembly succeeds,
	 * and the report owns them after — one transfer, so neither path
	 * frees what the other holds. */
	out->depth_state    = state.depth_state;
	out->depth          = state.depth;
	out->fan_out        = state.fan_out;
	out->fan_out_count  = state.fan_out_count;
	out->cycles         = state.cycles;
	out->cycle_count    = state.cycle_count;
	out->deepest        = state.deepest;
	out->deepest_count  = state.deepest_count;
	state.fan_out       = NULL;
	state.fan_out_count = 0;
	state.cycles        = NULL;
	state.cycle_count   = 0;
	state.deepest       = NULL;
	state.deepest_count = 0;

	out->reach_state              = state.reach_state;
	out->scope_state              = state.scope_state;
	out->global_state             = state.global_state;
	out->global_state_count       = state.global_state_count;
	out->unreachable              = state.unreachable;
	out->unreachable_count        = state.unreachable_count;
	out->unreachable_globals      = state.unreachable_globals;
	out->unreachable_global_count = state.unreachable_global_count;
	out->cross_scope              = state.cross_scope;
	out->cross_scope_count        = state.cross_scope_count;
	out->findings                 = state.findings;
	out->finding_count            = state.finding_count;
	out->coupling                 = state.coupling;
	out->coupling_count           = state.coupling_count;
	out->bottleneck_threshold     = state.bottleneck_threshold;
	out->dep_cycles               = state.dep_cycles;
	out->dep_cycle_count          = state.dep_cycle_count;
	out->strata_state             = state.strata_state;
	out->layering                 = state.layering;
	out->layering_count           = state.layering_count;
	out->dead                     = state.dead;
	out->dead_count               = state.dead_count;
	out->dead_unanalysed          = state.dead_unanalysed;
	state.global_state             = NULL;
	state.global_state_count       = 0;
	state.unreachable              = NULL;
	state.unreachable_count        = 0;
	state.unreachable_globals      = NULL;
	state.unreachable_global_count = 0;
	state.cross_scope              = NULL;
	state.cross_scope_count        = 0;
	state.findings                 = NULL;
	state.finding_count            = 0;
	state.coupling                 = NULL;
	state.coupling_count           = 0;
	state.dep_cycles               = NULL;
	state.dep_cycle_count          = 0;
	state.layering                 = NULL;
	state.layering_count           = 0;
	state.dead                     = NULL;
	state.dead_count               = 0;
	memset(&state.dead_unanalysed, 0, sizeof state.dead_unanalysed);

	status = 0;

cleanup:
	/* No partial conversion survives a rejection: whatever was
	 * reconstructed before the failure is released rather than rendered
	 * (LLR-XRD-06). */
	if (status != 0) {
		filemetrics_free(state.current);
		metrics_free(&acc);
		report_free(out);
		memset(out, 0, sizeof *out);
	}
	/* Released only on the paths where assembly did not take them. */
	for (size_t i = 0; i < state.fan_out_count; i++) {
		free(state.fan_out[i].function);
		free(state.fan_out[i].file);
	}
	free(state.fan_out);
	for (size_t i = 0; i < state.cycle_count; i++) {
		for (size_t m = 0; m < state.cycles[i].count; m++)
			free(state.cycles[i].members[m]);
		free(state.cycles[i].members);
	}
	free(state.cycles);
	for (size_t i = 0; i < state.deepest_count; i++) {
		free(state.deepest[i].function);
		free(state.deepest[i].file);
	}
	free(state.deepest);
	for (size_t i = 0; i < state.global_state_count; i++) {
		free(state.global_state[i].object);
		free(state.global_state[i].writers);
		free(state.global_state[i].readers);
		free(state.global_state[i].participants);
	}
	free(state.global_state);
	for (size_t i = 0; i < state.unreachable_count; i++) {
		free(state.unreachable[i].function);
		free(state.unreachable[i].file);
	}
	free(state.unreachable);
	for (size_t i = 0; i < state.unreachable_global_count; i++)
		free(state.unreachable_globals[i]);
	free(state.unreachable_globals);
	for (size_t i = 0; i < state.cross_scope_count; i++) {
		free(state.cross_scope[i].from_scope);
		free(state.cross_scope[i].from_function);
		free(state.cross_scope[i].to_scope);
		free(state.cross_scope[i].to_function);
		free(state.cross_scope[i].object);
	}
	free(state.cross_scope);
	for (size_t i = 0; i < state.finding_count; i++) {
		free(state.findings[i].severity);
		free(state.findings[i].measurement);
		free(state.findings[i].subject);
		free(state.findings[i].where);
		free(state.findings[i].detail);
		free(state.findings[i].source);
	}
	free(state.findings);
	for (size_t i = 0; i < state.coupling_count; i++) {
		free(state.coupling[i].component);
		free(state.coupling[i].instability);
	}
	free(state.coupling);
	for (size_t i = 0; i < state.dep_cycle_count; i++) {
		free(state.dep_cycles[i].components);
		free(state.dep_cycles[i].path);
	}
	free(state.dep_cycles);
	for (size_t i = 0; i < state.layering_count; i++) {
		free(state.layering[i].from_stratum);
		free(state.layering[i].from_function);
		free(state.layering[i].from_file);
		free(state.layering[i].to_stratum);
		free(state.layering[i].to_function);
		free(state.layering[i].to_file);
	}
	free(state.layering);
	for (size_t i = 0; i < state.dead_count; i++) {
		free(state.dead[i].file);
		free(state.dead[i].function);
	}
	free(state.dead);
	for (size_t i = 0; i < state.dead_unanalysed.count; i++)
		free(state.dead_unanalysed.paths[i]);
	free(state.dead_unanalysed.paths);
	routelist_free(&state.routes);
	if (parser)
		XML_ParserFree(parser);
	if (fp)
		fclose(fp);
	return status;
}
