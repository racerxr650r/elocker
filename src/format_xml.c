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
		        "\">\n", f->physical_lines, f->eloc);

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
	routelist_free(&state.routes);
	if (parser)
		XML_ParserFree(parser);
	if (fp)
		fclose(fp);
	return status;
}
