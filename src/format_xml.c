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

#include "diag.h"
#include "analyze.h"
#include "calltree.h"
#include "discover.h"
#include "elc.h"
#include "format_dsm.h"
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

/* --- one writer per section ------------------------------------------------
 *
 * None of them can fail. Every one writes to the stream and nothing else, and
 * whether the stream took it is asked once at the end — for the reason the
 * other writers give: a full disk shows up on the flush, not on the call that
 * happened to fill the buffer.
 */

/* Derived, and written for a consumer that is not elc. The reader recomputes
 * all of it rather than trusting it. */
static void write_summary(const Report *report, FILE *out)
{
	fputs("  <summary", out);
	fprintf(out, " files=\"%zu\"", report->summary.file_count);
	fprintf(out, " physical-lines=\"%" PRIu64 "\"",
	        report->summary.physical_lines);
	fprintf(out, " eloc=\"%" PRIu64 "\"", report->summary.eloc);
	fprintf(out, " functions=\"%" PRIu64 "\"",
	        report->summary.function_count);
	fputs("/>\n", out);
}

static void write_languages(const Report *report, FILE *out)
{
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
}

static void write_files(const Report *report, FILE *out)
{
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
}

/* A measurement of the run, so it lives in the record beside the others: it
 * cannot be recomputed later, since regeneration has no graph and no source to
 * build one from (HLR-054, HLR-056). */
static void write_graph(const Report *report, FILE *out)
{
	fprintf(out, "  <graph unresolved-calls=\"%zu\"/>\n",
	        report->unresolved_calls);
}

/* The call-tree measurements. Every one is a fact about the run that
 * regeneration cannot recompute — there is no graph and no source to build one
 * from — so the record carries them exactly as it carries the metrics
 * (HLR-054, HLR-056). */
static void write_calltree(const Report *report, FILE *out)
{
	fprintf(out, "  <calltree depth-state=\"%d\" depth=\"%" PRIu32 "\">\n",
	        (int)report->depth_state, report->depth);
	/* The element keeps the name it was given when fan-out was the only
	 * figure it carried. Renaming it would be a *removal* from the record
	 * format, which is what the format version marks; adding attributes is
	 * an addition an older reader ignores, which is what the format was
	 * designed to allow.
	 *
	 * All four values are written because none can be recomputed later:
	 * fan-in and fan-out need the graph, and regeneration has neither, nor
	 * any source to build one from (LLR-XWR-08, HLR-156).
	 *
	 * The `hk` attribute this element carried until Phase 24 is gone with
	 * the metric it held. That is a *removal*, which is what
	 * ELC_XML_FORMAT_VERSION counts — hence version 2, and hence a
	 * version-1 record rejected rather than read with a field missing
	 * (HLR-061, HLR-058). */
	for (size_t i = 0; i < report->fan_out_count; i++) {
		fputs("    <fanout", out);
		write_attribute(out, "function", report->fan_out[i].function);
		write_attribute(out, "file", report->fan_out[i].file);
		fprintf(out, " line=\"%" PRIu32 "\" value=\"%" PRIu32 "\""
		        " fan-in=\"%" PRIu32 "\" eloc=\"%" PRIu32 "\"/>\n",
		        report->fan_out[i].line, report->fan_out[i].fan_out,
		        report->fan_out[i].fan_in, report->fan_out[i].eloc);
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
}

/* The global-state and reachability measurements. Carried for the same reason
 * the call-tree ones are: regeneration has no graph and no source to build one
 * from, so a value not written here is a value the regenerated report cannot
 * have (HLR-054, HLR-056).
 *
 * The attribution of a verdict is deliberately *not* written. It is derived
 * from the verdict by one function both paths call, so a record cannot carry a
 * citation that disagrees with a live run's (LLR-GLB-04). */
static void write_state(const Report *report, FILE *out)
{
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
}

/* The languages dead code was *not* looked for in are written beside the
 * findings, because the two together are the claim: a record carrying the
 * spans alone would regenerate into a report that reads as a clean bill of
 * health for a language nobody analysed (HLR-139). */
static void write_deadcode(const Report *report, FILE *out)
{
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
}

/* What the user's rules matched. Carried for the reason every other section
 * is: regeneration has no source tree to re-run a query over, so a match not
 * written here is one the regenerated report cannot have (HLR-054, HLR-056).
 * The rule *file* is not recorded — the identity is, which is what the report
 * presents and all a reader of a regenerated report can act on. */
static void write_rules(const Report *report, FILE *out)
{
	fputs("  <rules>\n", out);
	for (size_t i = 0; i < report->rule_match_count; i++) {
		const RuleMatchRow *r = &report->rule_matches[i];

		fputs("    <match", out);
		write_attribute(out, "rule", r->rule);
		write_attribute(out, "file", r->file);
		fprintf(out, " start-line=\"%" PRIu32 "\" end-line=\"%" PRIu32
		        "\"/>\n", r->start_line, r->end_line);
	}
	fputs("  </rules>\n", out);
}

/* The configuration these figures describe, and how completely it could be
 * applied. Pruning happens when a file is measured, so a record that omitted
 * this would regenerate into a report describing a configuration it does not
 * name — and one that could not be told from a report of the whole source
 * (HLR-136). */
static void write_configuration(const Report *report, FILE *out)
{
	fprintf(out, "  <configuration undecided-regions=\"%" PRIu64 "\">\n",
	        report->undecided_regions);
	for (size_t i = 0; i < report->definition_count; i++) {
		fputs("    <define", out);
		write_attribute(out, "value", report->definitions[i]);
		fputs("/>\n", out);
	}
	fputs("  </configuration>\n", out);
}

/* The image the run was filtered by, both directions of mismatch against it,
 * and the one figure the filter did not narrow.
 *
 * Written only for a filtered run, and that asymmetry with every other section
 * is the requirement rather than an oversight: an unfiltered run must produce
 * exactly the record it produced before the option existed (HLR-140). Without
 * this element a regenerated report would describe a filtered run while naming
 * no filter (HLR-147, LLR-XWR-14). */
static void write_image(const Report *report, FILE *out)
{
	if (!report->image)
		return;

	fputs("  <image", out);
	write_attribute(out, "path", report->image);
	/* The line-granularity counts travel with the image element rather
	 * than with the per-file metrics, for the reason file-scope ELOC does:
	 * they are properties of what the filter did to the run, and
	 * `report_assemble` on the regeneration path has no image to re-derive
	 * them from (HLR-155, LLR-XWR-14). */
	fprintf(out, " unresolved=\"%" PRIu64 "\" file-scope-eloc=\"%" PRIu64
	        "\" pruned-lines=\"%" PRIu64 "\" uncovered-files=\"%" PRIu64
	        "\">\n", report->image_unresolved, report->file_scope_eloc,
	        report->pruned_lines, report->uncovered_files);
	for (size_t i = 0; i < report->absent_count; i++) {
		const AbsentRow *r = &report->absent[i];

		fputs("    <absent", out);
		write_attribute(out, "function", r->function);
		write_attribute(out, "file", r->file);
		fprintf(out, " line=\"%" PRIu32 "\"/>\n", r->line);
	}
	fputs("  </image>\n", out);
}

/* One conformance index, as the run rendered it.
 *
 * The figures are written rather than the division that produced them, for the
 * reason a component's Instability is: "undefined" is one of the legitimate
 * answers, and a record that carried only the counts would leave the
 * regenerated report to decide how to say so a second time. */
static void write_conformance(FILE *out, const char *kind,
                              const ConformanceRow *row)
{
	fputs("    <conformance", out);
	write_attribute(out, "kind", kind);
	write_attribute(out, "index", row->index ? row->index : "undefined");
	write_attribute(out, "conforming",
	                row->conforming ? row->conforming : "undefined");
	fprintf(out, " violations=\"%" PRIu64 "\" edges=\"%" PRIu64 "\"/>\n",
	        row->violations, row->edges);
}

/* The component-level measurements. Carried for the reason every other
 * analysis result is: regeneration has no graph and no source tree to rebuild
 * one from, so a value not written here is one the regenerated report cannot
 * have (HLR-054, HLR-056).
 *
 * The attributions are absent by the same rule the global-state citation is:
 * both are derived from one function each path calls, so a record cannot carry
 * an attribution that disagrees with a live run's. */
static void write_architecture(const Report *report, FILE *out)
{
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
	/* The two conformance indices, rendered rather than as their inputs.
	 * "undefined" is one of the legitimate values and the record carries
	 * the same answer the live run printed, exactly as it carries a
	 * component's Instability (HLR-162, HLR-163). The counts travel beside
	 * them because a proportion is not interpretable without the number it
	 * is over. */
	write_conformance(out, "back-call", &report->back_call);
	write_conformance(out, "skip-call", &report->skip_call);
	fputs("  </architecture>\n", out);
}

/* The dependency matrix (HLR-165, HLR-166).
 *
 * Carried because a record has no call graph to rebuild it from, which is the
 * rule every other analysis result in this document obeys (HLR-054).
 *
 * **Only the non-zero cells are written.** A matrix over a real project is
 * mostly zeroes — that is what makes it readable — and a cell absent from the
 * document reads back as the zero it was. The subjects precede the cells, so
 * the reader knows the order of the grid before an index into it arrives.
 */
static void write_dsm(const Report *report, FILE *out)
{
	const Dsm *m = &report->dsm;

	fprintf(out, "  <dsm from-strata=\"%d\">\n", m->from_strata ? 1 : 0);
	for (size_t i = 0; i < m->count; i++) {
		fputs("    <dsm-subject", out);
		write_attribute(out, "name", m->subjects[i]);
		fputs("/>\n", out);
	}
	for (size_t row = 0; row < m->count; row++)
		for (size_t col = 0; col < m->count; col++) {
			size_t calls = m->cells[row * m->count + col];

			if (calls == 0)
				continue;
			fprintf(out, "    <dsm-cell row=\"%zu\" col=\"%zu\""
			        " calls=\"%zu\"/>\n", row, col, calls);
		}
	fputs("  </dsm>\n", out);
}

/* What purification concluded (HLR-174).
 *
 * Carried for the reason every other analysis result is: regeneration has no
 * graph to recompute a centrality over, so a classification absent from this
 * element is one a regenerated report cannot present — and a record that
 * dropped the transparency report would leave a reader of it with the masking
 * and without the account of it.
 *
 * The thresholds are written beside the rows because they are what the rows
 * were decided against, and because a record read a year later has no command
 * line to consult. **No severity is written**, here or anywhere: a
 * classification does not have one (HLR-171).
 */
static void write_purification(const Report *report, FILE *out)
{
	fprintf(out, "  <purification sink-authority=\"%" PRIu32
	        "\" sink-hub=\"%" PRIu32 "\" god-betweenness=\"%" PRIu32
	        "\" god-hub=\"%" PRIu32 "\" core-depth=\"%" PRIu32
	        "\" retained=\"%zu\" masked-edges=\"%zu\">\n",
	        report->purify_thresholds.sink_authority,
	        report->purify_thresholds.sink_hub,
	        report->purify_thresholds.god_betweenness,
	        report->purify_thresholds.god_hub,
	        report->purify_thresholds.core_depth,
	        report->purified_nodes, report->purified_edges);
	for (size_t i = 0; i < report->purification_count; i++) {
		const PurificationRow *r = &report->purification[i];

		fputs("    <classification", out);
		write_attribute(out, "function", r->function);
		write_attribute(out, "file", r->file);
		write_attribute(out, "class", r->class_name);
		write_attribute(out, "metric", r->metric);
		write_attribute(out, "value", r->value);
		write_attribute(out, "action", r->action);
		/* Where the class came from. A record that dropped it would
		 * leave a reader of a regenerated report unable to tell the
		 * tool's assumptions from their own team's, which is the whole
		 * of what HLR-177 asks the report to distinguish. */
		write_attribute(out, "source", r->source);
		fprintf(out, " line=\"%" PRIu32 "\"/>\n", r->line);
	}
	fputs("  </purification>\n", out);
}

/* The layering recovery proposed, and the arguments that would declare it
 * (HLR-172, HLR-173).
 *
 * Carried for the reason every other analysis result is: a record holds no
 * graph to re-order, so a proposal absent from it is one a regenerated report
 * cannot present. **It is a proposal in the record too** — nothing reads these
 * elements back as a declaration, and the conformance analyses of a
 * regenerated report are exactly as omitted as they were in the run it
 * describes (HLR-115, HLR-173).
 */
static void write_recovery(const Report *report, FILE *out)
{
	static const char *const STATES[] = { "omitted-empty", "cyclic",
	                                      "proposed" };
	size_t                   state = (size_t)report->recovery_state;

	fprintf(out, "  <recovery state=\"%s\" layers=\"%zu\" masked=\"%zu\""
	        " excluded=\"%zu\">\n",
	        state < sizeof STATES / sizeof *STATES ? STATES[state]
	                                              : "omitted-empty",
	        report->recovery_strata, report->recovery_masked,
	        report->recovery_excluded);
	for (size_t i = 0; i < report->recovery_count; i++) {
		fputs("    <recovered", out);
		write_attribute(out, "directory", report->recovery[i].directory);
		fprintf(out, " layer=\"%zu\" functions=\"%zu\"/>\n",
		        report->recovery[i].layer,
		        report->recovery[i].functions);
	}
	for (size_t i = 0; i < report->recovery_cycles.count; i++) {
		fputs("    <recovery-cycle", out);
		write_attribute(out, "members",
		                report->recovery_cycles.paths[i]);
		fputs("/>\n", out);
	}
	if (report->recovery_proposal) {
		fputs("    <proposal", out);
		write_attribute(out, "arguments", report->recovery_proposal);
		fputs("/>\n", out);
	}
	fputs("  </recovery>\n", out);
}

/* The findings. Carried because regeneration has no measurements to re-band
 * and no catalogue call to make against them, exactly as the measurements
 * themselves are (HLR-054, HLR-056).
 *
 * The source *is* written here, unlike the verdict citations elsewhere in this
 * record. Those are derived from a verdict the record already carries; a
 * finding's source is derived from its measurement kind, which the record
 * would otherwise have to carry as a number whose meaning could shift between
 * builds. A string that reads the same in both is the more durable of the
 * two. */
static void write_findings(const Report *report, FILE *out)
{
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
}

/* Carried because the report presents it. A record that omitted the routes
 * would regenerate into a report with an empty Discovery section, which is a
 * different report — and HLR-056 says it must not be (LLR-XWR-06). */
static void write_discovery(const Report *report, FILE *out)
{
	fputs("  <discovery>\n", out);
	for (size_t i = 0; i < report->routes.count; i++) {
		fputs("    <route", out);
		write_attribute(out, "target", report->routes.items[i].target);
		fprintf(out, " via=\"%s\"/>\n",
		        report->routes.items[i].route == ROUTE_REPOSITORY
		                ? "repository" : "filesystem");
	}
	fputs("  </discovery>\n", out);
}

static void write_skipped(const Report *report, FILE *out)
{
	fputs("  <skipped>\n", out);
	for (size_t i = 0; i < report->skipped_files.count; i++) {
		fputs("    <file", out);
		write_attribute(out, "path", report->skipped_files.paths[i]);
		fputs("/>\n", out);
	}
	fputs("  </skipped>\n", out);
}

int xml_write_report(const Report *report, FILE *out)
{
	/* The order the sections appear in the file, which is the order the
	 * reader expects them and the order the schema in doc/ describes. Each
	 * writes one element and returns; none inspects what another wrote. */
	static void (*const SECTIONS[])(const Report *, FILE *) = {
		write_summary,
		write_languages,
		write_files,
		write_graph,
		write_calltree,
		write_state,
		write_deadcode,
		write_rules,
		write_configuration,
		write_image,
		write_architecture,
		write_dsm,
		write_purification,
		write_recovery,
		write_findings,
		write_discovery,
		write_skipped
	};

	fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", out);
	fprintf(out, "<elc-report format-version=\"%d\">\n",
	        ELC_XML_FORMAT_VERSION);

	for (size_t i = 0; i < sizeof SECTIONS / sizeof *SECTIONS; i++)
		SECTIONS[i](report, out);

	/* The architectural findings and omission notices are written here once
	 * the analyses that produce them exist. The elements are absent rather
	 * than empty, so that adding them is an addition a reader of an older
	 * record ignores — which is why the format version marks removals and
	 * meaning changes only. */

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
	ConformanceRow      back_call;
	ConformanceRow      skip_call;
	Dsm                 dsm;
	PurificationRow    *purification;
	size_t              purification_count;
	PurifyThresholds    purify_thresholds;
	size_t              purified_nodes;
	size_t              purified_edges;
	RecoveryState       recovery_state;
	RecoveredRow       *recovery;
	size_t              recovery_count;
	size_t              recovery_strata;
	PathList            recovery_cycles;
	size_t              recovery_masked;
	size_t              recovery_excluded;
	char               *recovery_proposal;
	DeadRow            *dead;
	size_t              dead_count;
	RuleMatchRow       *rule_matches;
	size_t              rule_match_count;
	char              **definitions;
	size_t              definition_count;
	uint64_t            undecided_regions;
	char               *image;
	uint64_t            image_unresolved;
	uint64_t            file_scope_eloc;
	uint64_t            pruned_lines;
	uint64_t            uncovered_files;
	AbsentRow          *absent;
	size_t              absent_count;
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

/* One handler per element of a record, and a table that finds it.
 *
 * The alternative — and what this was — is a single function testing the
 * element name against every name in turn. It reached a cyclomatic complexity
 * of 169, which is to say it had more independent paths through it than the
 * rest of this module put together, and every one of them was the same path:
 * compare a name, read some attributes, append a row. Nothing about the record
 * format made it complicated; the shape of the dispatch did.
 *
 * Each handler is now reached only when its element has already been matched,
 * so it begins where the interesting part begins. `on_start` does nothing but
 * find the right one, and an element added to the format is a row in the table
 * rather than another branch in a function nobody can hold in their head.
 */
static void on_root(ReadState *state, const XML_Char *name,
                    const XML_Char **atts)
{
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
}

static void on_file(ReadState *state, const XML_Char **atts)
{
	/* A <file> while one is already open is a nested element of
	 * some other shape, not a second file. It was ignored when the
	 * test was part of the branch condition, and is ignored here. */
	if (state->current)
		return;

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

	file->path      = strdup(path);
	/* Derived rather than recorded in the document, because it is a
	 * property of the path the record already carries. One derivation
	 * serves the live run and the regenerated one, so a component's
	 * directory cannot differ between them (HLR-160). */
	file->directory = component_directory(path);
	if (!file->path || !file->directory) {
		filemetrics_free(file);
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

static void on_calltree(ReadState *state, const XML_Char **atts)
{
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

static void on_fanout(ReadState *state, const XML_Char **atts)
{
	const char *fn   = attribute(atts, "function");
	const char *file = attribute(atts, "file");
	const char *line = attribute(atts, "line");
	const char *val  = attribute(atts, "value");
	/* Optional, because a record written by a build that predates the
	 * information-flow measurements carries the same format version and
	 * must still read. Absent means zero, which is the value a function
	 * with no callers has anyway — and the alternative, rejecting the
	 * record, would break the compatibility the version number promises. */
	const char *in   = attribute(atts, "fan-in");
	const char *eloc = attribute(atts, "eloc");

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
	row->fan_in  = in ? (uint32_t)strtoul(in, NULL, 10) : 0;
	row->eloc    = eloc ? (uint32_t)strtoul(eloc, NULL, 10) : 0;
	state->fan_out_count++;
	return;
}

static void on_cycle(ReadState *state, const XML_Char **atts)
{
	/* A <cycle> carries no attributes of its own: it opens a group that
	 * the <member> elements after it fill in. The parameter stays for the
	 * table's one signature, which is what lets the dispatch be a lookup. */
	(void)atts;

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

static void on_member(ReadState *state, const XML_Char **atts)
{
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

static void on_step(ReadState *state, const XML_Char **atts)
{
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

static void on_state(ReadState *state, const XML_Char **atts)
{
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

static void on_global(ReadState *state, const XML_Char **atts)
{
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

static void on_unreachable_function(ReadState *state, const XML_Char **atts)
{
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

static void on_unreachable_global(ReadState *state, const XML_Char **atts)
{
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

static void on_cross_scope(ReadState *state, const XML_Char **atts)
{
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

static void on_unanalysed(ReadState *state, const XML_Char **atts)
{
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

static void on_span(ReadState *state, const XML_Char **atts)
{
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

static void on_image(ReadState *state, const XML_Char **atts)
{
	const char *image = attribute(atts, "path");

	if (!image) {
		fail(state, "an image element has no path");
		return;
	}
	/* A record `elc` wrote holds one image element. A hand-edited
	 * one may hold two, and the last would then silently replace
	 * the first and leak it — a rejected record must exit as
	 * leak-clean as an accepted one (HLR-125). */
	free(state->image);
	state->image = strdup(image);
	if (!state->image) {
		fail(state, "out of memory");
		return;
	}
	state->image_unresolved = uint_attribute(state, atts,
	                                         "unresolved");
	state->file_scope_eloc  = uint_attribute(state, atts,
	                                         "file-scope-eloc");
	/* Absent from a record written before debug-line pruning existed, and
	 * absent from one written by a run whose image carried no line
	 * information. `uint_attribute` answers zero for a missing attribute,
	 * which is the right figure in both cases. */
	state->pruned_lines     = uint_attribute(state, atts, "pruned-lines");
	state->uncovered_files  = uint_attribute(state, atts,
	                                         "uncovered-files");
	return;
}

static void on_absent(ReadState *state, const XML_Char **atts)
{
	const char *function = attribute(atts, "function");
	const char *file     = attribute(atts, "file");

	if (!function || !file) {
		fail(state, "an absent element is incomplete");
		return;
	}

	AbsentRow *grown = realloc(state->absent,
	                           (state->absent_count + 1) *
	                                   sizeof *grown);

	if (!grown) {
		fail(state, "out of memory");
		return;
	}
	state->absent = grown;

	AbsentRow *row = &state->absent[state->absent_count];

	memset(row, 0, sizeof *row);
	row->function = strdup(function);
	row->file     = strdup(file);
	if (!row->function || !row->file) {
		fail(state, "out of memory");
		return;
	}
	row->line = uint_attribute(state, atts, "line");
	state->absent_count++;
	return;
}

static void on_configuration(ReadState *state, const XML_Char **atts)
{
	state->undecided_regions = uint_attribute(state, atts,
	                                          "undecided-regions");
	return;
}

static void on_define(ReadState *state, const XML_Char **atts)
{
	const char *value = attribute(atts, "value");

	if (!value) {
		fail(state, "a define element is incomplete");
		return;
	}

	char **grown = realloc(state->definitions,
	                       (state->definition_count + 1) * sizeof *grown);

	if (!grown) {
		fail(state, "out of memory");
		return;
	}
	state->definitions = grown;
	state->definitions[state->definition_count] = strdup(value);
	if (!state->definitions[state->definition_count]) {
		fail(state, "out of memory");
		return;
	}
	state->definition_count++;
	return;
}

static void on_match(ReadState *state, const XML_Char **atts)
{
	const char *rule = attribute(atts, "rule");
	const char *file = attribute(atts, "file");

	if (!rule || !file) {
		fail(state, "a rule match element is incomplete");
		return;
	}

	RuleMatchRow *grown =
		realloc(state->rule_matches,
		        (state->rule_match_count + 1) * sizeof *grown);

	if (!grown) {
		fail(state, "out of memory");
		return;
	}
	state->rule_matches = grown;

	RuleMatchRow *row = &state->rule_matches[state->rule_match_count];

	memset(row, 0, sizeof *row);
	row->rule = strdup(rule);
	row->file = strdup(file);
	if (!row->rule || !row->file) {
		fail(state, "out of memory");
		return;
	}
	row->start_line = uint_attribute(state, atts, "start-line");
	row->end_line   = uint_attribute(state, atts, "end-line");
	state->rule_match_count++;
	return;
}

static void on_finding(ReadState *state, const XML_Char **atts)
{
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

static void on_architecture(ReadState *state, const XML_Char **atts)
{
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

static void on_coupling(ReadState *state, const XML_Char **atts)
{
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

static void on_dependency_cycle(ReadState *state, const XML_Char **atts)
{
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

/* One conformance index, restored as the live run rendered it (HLR-162,
 * HLR-163).
 *
 * The strings are taken from the record rather than recomputed from the
 * counts beside them, so that a regenerated report cannot round a figure
 * differently from the report it came from. */
static void on_conformance(ReadState *state, const XML_Char **atts)
{
	const char *kind       = attribute(atts, "kind");
	const char *index      = attribute(atts, "index");
	const char *conforming = attribute(atts, "conforming");

	if (!kind || !index || !conforming) {
		fail(state, "a conformance element is incomplete");
		return;
	}

	ConformanceRow *row = strcmp(kind, "skip-call") == 0
	                              ? &state->skip_call
	                              : &state->back_call;

	free(row->index);
	free(row->conforming);
	row->index      = strdup(index);
	row->conforming = strdup(conforming);
	if (!row->index || !row->conforming) {
		fail(state, "out of memory");
		return;
	}
	row->violations = uint_attribute(state, atts, "violations");
	row->edges      = uint_attribute(state, atts, "edges");
}

static void on_dsm(ReadState *state, const XML_Char **atts)
{
	const char *from_strata = attribute(atts, "from-strata");

	state->dsm.from_strata = from_strata && strtol(from_strata, NULL, 10);
}

static void on_dsm_subject(ReadState *state, const XML_Char **atts)
{
	const char *name = attribute(atts, "name");

	if (!name) {
		fail(state, "a dsm-subject element has no name");
		return;
	}

	char **grown = realloc(state->dsm.subjects,
	                       (state->dsm.count + 1) * sizeof *grown);

	if (!grown) {
		fail(state, "out of memory");
		return;
	}
	state->dsm.subjects = grown;

	state->dsm.subjects[state->dsm.count] = strdup(name);
	if (!state->dsm.subjects[state->dsm.count]) {
		fail(state, "out of memory");
		return;
	}
	state->dsm.count++;
}

/* One non-zero cell. The grid is allocated on the first of them, by which
 * point every subject has been read: the subjects precede the cells in the
 * document, so the order of the matrix is known before any index into it
 * arrives. A record whose cells preceded its subjects would name indices into
 * a grid of unknown size, and those cells are dropped rather than guessed at. */
static void on_dsm_cell(ReadState *state, const XML_Char **atts)
{
	uint64_t row   = uint_attribute(state, atts, "row");
	uint64_t col   = uint_attribute(state, atts, "col");
	uint64_t calls = uint_attribute(state, atts, "calls");

	if (state->dsm.count == 0)
		return;

	if (!state->dsm.cells) {
		state->dsm.cells = calloc(state->dsm.count * state->dsm.count,
		                          sizeof *state->dsm.cells);
		if (!state->dsm.cells) {
			fail(state, "out of memory");
			return;
		}
	}

	if (row >= state->dsm.count || col >= state->dsm.count) {
		fail(state, "a dsm-cell element names a cell outside the grid");
		return;
	}
	state->dsm.cells[row * state->dsm.count + col] = (size_t)calls;
}

/* The thresholds purification was made against, and what it left behind.
 *
 * Restored as text and counts rather than recomputed, exactly as the
 * conformance indices are: a record carries no graph, and a regenerated report
 * must say what the run it describes said rather than what this build would
 * conclude today (HLR-174, LLR-XRD-18).
 */
static void on_purification(ReadState *state, const XML_Char **atts)
{
	state->purify_thresholds.sink_authority =
		(uint32_t)uint_attribute(state, atts, "sink-authority");
	state->purify_thresholds.sink_hub =
		(uint32_t)uint_attribute(state, atts, "sink-hub");
	state->purify_thresholds.god_betweenness =
		(uint32_t)uint_attribute(state, atts, "god-betweenness");
	state->purify_thresholds.god_hub =
		(uint32_t)uint_attribute(state, atts, "god-hub");
	state->purify_thresholds.core_depth =
		(uint32_t)uint_attribute(state, atts, "core-depth");
	state->purified_nodes = (size_t)uint_attribute(state, atts, "retained");
	state->purified_edges =
		(size_t)uint_attribute(state, atts, "masked-edges");
}

/* Whether every one of `n` required attributes was present on the element.
 *
 * A record missing one is refused rather than half-read: a reader that filled
 * the gap with an empty string would produce a report that differs from the
 * one the record was written from, which is the whole of what HLR-056 forbids.
 */
static bool all_present(const char *const *values, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (!values[i])
			return false;
	return true;
}

/* Copy `n` attribute values, releasing what it took where it cannot take them
 * all. Returns true only having copied every one, so a row is either whole or
 * absent and never a partly-filled entry the teardown does not know about
 * (HLR-125).
 */
static bool dup_all(char **dest, const char *const *src, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		dest[i] = strdup(src[i]);
		if (!dest[i]) {
			while (i-- > 0) {
				free(dest[i]);
				dest[i] = NULL;
			}
			return false;
		}
	}
	return true;
}

static void on_classification(ReadState *state, const XML_Char **atts)
{
	const char *values[] = {
		attribute(atts, "function"), attribute(atts, "file"),
		attribute(atts, "class"),    attribute(atts, "metric"),
		attribute(atts, "value"),    attribute(atts, "action"),
		attribute(atts, "source")
	};
	const size_t     n = sizeof values / sizeof *values;
	char            *owned[7] = { 0 };
	PurificationRow *grown;
	PurificationRow *row;

	if (!all_present(values, n)) {
		fail(state, "a classification element is incomplete");
		return;
	}

	grown = realloc(state->purification,
	                (state->purification_count + 1) * sizeof *grown);
	if (!grown) {
		fail(state, "out of memory");
		return;
	}
	state->purification = grown;

	if (!dup_all(owned, values, n)) {
		fail(state, "out of memory");
		return;
	}

	row = &state->purification[state->purification_count];
	memset(row, 0, sizeof *row);
	row->function   = owned[0];
	row->file       = owned[1];
	row->class_name = owned[2];
	row->metric     = owned[3];
	row->value      = owned[4];
	row->action     = owned[5];
	row->source     = owned[6];
	row->line       = (uint32_t)uint_attribute(state, atts, "line");
	state->purification_count++;
}

/* The proposal, restored as it was written (HLR-172, HLR-173).
 *
 * **Restored, never re-derived, and never adopted.** A record carries no graph
 * to re-order, so what a regenerated report presents is what the run it
 * describes proposed; and nothing reads these elements as a declaration, so
 * the conformance analyses of a regenerated report stay exactly as omitted as
 * they were then (HLR-115).
 */
static void on_recovery(ReadState *state, const XML_Char **atts)
{
	static const char *const STATES[] = { "omitted-empty", "cyclic",
	                                      "proposed" };
	const char              *name = attribute(atts, "state");

	state->recovery_state = RECOVERY_OMITTED_EMPTY;
	for (size_t i = 0; name && i < sizeof STATES / sizeof *STATES; i++)
		if (strcmp(STATES[i], name) == 0)
			state->recovery_state = (RecoveryState)i;

	state->recovery_strata   = (size_t)uint_attribute(state, atts, "layers");
	state->recovery_masked   = (size_t)uint_attribute(state, atts, "masked");
	state->recovery_excluded =
		(size_t)uint_attribute(state, atts, "excluded");
}

static void on_recovered(ReadState *state, const XML_Char **atts)
{
	const char   *directory = attribute(atts, "directory");
	RecoveredRow *grown;

	if (!directory) {
		fail(state, "a recovered element names no directory");
		return;
	}

	grown = realloc(state->recovery,
	                (state->recovery_count + 1) * sizeof *grown);
	if (!grown) {
		fail(state, "out of memory");
		return;
	}
	state->recovery = grown;

	RecoveredRow *row = &state->recovery[state->recovery_count];

	memset(row, 0, sizeof *row);
	row->directory = strdup(directory);
	if (!row->directory) {
		fail(state, "out of memory");
		return;
	}
	row->layer     = (size_t)uint_attribute(state, atts, "layer");
	row->functions = (size_t)uint_attribute(state, atts, "functions");
	state->recovery_count++;
}

static void on_recovery_cycle(ReadState *state, const XML_Char **atts)
{
	const char *members = attribute(atts, "members");
	char      **grown;

	if (!members) {
		fail(state, "a recovery cycle names no members");
		return;
	}

	grown = realloc(state->recovery_cycles.paths,
	                (state->recovery_cycles.count + 1) * sizeof *grown);
	if (!grown) {
		fail(state, "out of memory");
		return;
	}
	state->recovery_cycles.paths = grown;
	state->recovery_cycles.paths[state->recovery_cycles.count] =
		strdup(members);
	if (!state->recovery_cycles.paths[state->recovery_cycles.count]) {
		fail(state, "out of memory");
		return;
	}
	state->recovery_cycles.count++;
	state->recovery_cycles.capacity = state->recovery_cycles.count;
}

static void on_proposal(ReadState *state, const XML_Char **atts)
{
	const char *arguments = attribute(atts, "arguments");

	if (!arguments) {
		fail(state, "a proposal carries no arguments");
		return;
	}
	free(state->recovery_proposal);
	state->recovery_proposal = strdup(arguments);
	if (!state->recovery_proposal)
		fail(state, "out of memory");
}

static void on_layering(ReadState *state, const XML_Char **atts)
{
	const char *kind     = attribute(atts, "kind");
	const char *values[] = {
		attribute(atts, "from-stratum"), attribute(atts, "from"),
		attribute(atts, "from-file"),    attribute(atts, "to-stratum"),
		attribute(atts, "to"),           attribute(atts, "to-file")
	};
	const size_t n = sizeof values / sizeof *values;
	char        *owned[6] = { 0 };
	LayeringRow *grown;
	LayeringRow *row;

	if (!kind || !all_present(values, n)) {
		fail(state, "a layering element is incomplete");
		return;
	}

	grown = realloc(state->layering,
	                (state->layering_count + 1) * sizeof *grown);
	if (!grown) {
		fail(state, "out of memory");
		return;
	}
	state->layering = grown;

	if (!dup_all(owned, values, n)) {
		fail(state, "out of memory");
		return;
	}

	row = &state->layering[state->layering_count];
	memset(row, 0, sizeof *row);
	row->from_stratum   = owned[0];
	row->from_function  = owned[1];
	row->from_file      = owned[2];
	row->to_stratum     = owned[3];
	row->to_function    = owned[4];
	row->to_file        = owned[5];
	row->layers_crossed = uint_attribute(state, atts, "layers");
	row->kind           = (LayerViolationKind)strtol(kind, NULL, 10);
	state->layering_count++;
}

static void on_graph(ReadState *state, const XML_Char **atts)
{
	const char *value = attribute(atts, "unresolved-calls");

	if (!value) {
		fail(state, "a graph element is incomplete");
		return;
	}
	state->unresolved = strtoull(value, NULL, 10);
	return;
}

static void on_route(ReadState *state, const XML_Char **atts)
{
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

static void on_function(ReadState *state, const XML_Char **atts)
{
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

typedef void (*ElementFn)(ReadState *state, const XML_Char **atts);

static const struct {
	const char *name;
	ElementFn   handle;
} ELEMENT_HANDLERS[] = {
	{ "file",                on_file },
	{ "calltree",            on_calltree },
	{ "fanout",              on_fanout },
	{ "cycle",               on_cycle },
	{ "member",              on_member },
	{ "step",                on_step },
	{ "state",               on_state },
	{ "global",              on_global },
	{ "unreachable-function", on_unreachable_function },
	{ "unreachable-global",  on_unreachable_global },
	{ "cross-scope",         on_cross_scope },
	{ "unanalysed",          on_unanalysed },
	{ "span",                on_span },
	{ "image",               on_image },
	{ "absent",              on_absent },
	{ "configuration",       on_configuration },
	{ "define",              on_define },
	{ "match",               on_match },
	{ "finding",             on_finding },
	{ "architecture",        on_architecture },
	{ "coupling",            on_coupling },
	{ "dependency-cycle",    on_dependency_cycle },
	{ "layering",            on_layering },
	{ "conformance",         on_conformance },
	{ "dsm",                 on_dsm },
	{ "dsm-subject",         on_dsm_subject },
	{ "dsm-cell",            on_dsm_cell },
	{ "purification",        on_purification },
	{ "recovery",            on_recovery },
	{ "recovered",           on_recovered },
	{ "recovery-cycle",      on_recovery_cycle },
	{ "proposal",            on_proposal },
	{ "classification",      on_classification },
	{ "graph",               on_graph },
	{ "route",               on_route },
	{ "function",            on_function },
};

static void on_start(void *user, const XML_Char *name,
                             const XML_Char **atts)
{
	ReadState *state = user;

	if (state->failed)
		return;

	/* The first element decides whether this is a record at all. */
	if (!state->saw_root) {
		on_root(state, name, atts);
		return;
	}

	for (size_t i = 0; i < sizeof ELEMENT_HANDLERS / sizeof *ELEMENT_HANDLERS;
	     i++) {
		if (strcmp(name, ELEMENT_HANDLERS[i].name) == 0) {
			ELEMENT_HANDLERS[i].handle(state, atts);
			return;
		}
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

/* --- what the parser still owns ------------------------------------------
 *
 * Released only on the paths where assembly did not take them. Grouped as
 * the record itself is grouped, so that a row type added to one section is
 * freed beside the rows it was added next to.
 */

static void free_calltree_state(ReadState *state)
{
	for (size_t i = 0; i < state->fan_out_count; i++) {
		free(state->fan_out[i].function);
		free(state->fan_out[i].file);
	}
	free(state->fan_out);
	for (size_t i = 0; i < state->cycle_count; i++) {
		for (size_t m = 0; m < state->cycles[i].count; m++)
			free(state->cycles[i].members[m]);
		free(state->cycles[i].members);
	}
	free(state->cycles);
	for (size_t i = 0; i < state->deepest_count; i++) {
		free(state->deepest[i].function);
		free(state->deepest[i].file);
	}
	free(state->deepest);
}

static void free_global_state(ReadState *state)
{
	for (size_t i = 0; i < state->global_state_count; i++) {
		free(state->global_state[i].object);
		free(state->global_state[i].writers);
		free(state->global_state[i].readers);
		free(state->global_state[i].participants);
	}
	free(state->global_state);
	for (size_t i = 0; i < state->unreachable_count; i++) {
		free(state->unreachable[i].function);
		free(state->unreachable[i].file);
	}
	free(state->unreachable);
	for (size_t i = 0; i < state->unreachable_global_count; i++)
		free(state->unreachable_globals[i]);
	free(state->unreachable_globals);
	for (size_t i = 0; i < state->cross_scope_count; i++) {
		free(state->cross_scope[i].from_scope);
		free(state->cross_scope[i].from_function);
		free(state->cross_scope[i].to_scope);
		free(state->cross_scope[i].to_function);
		free(state->cross_scope[i].object);
	}
	free(state->cross_scope);
}

static void free_findings_state(ReadState *state)
{
	for (size_t i = 0; i < state->finding_count; i++) {
		free(state->findings[i].severity);
		free(state->findings[i].measurement);
		free(state->findings[i].subject);
		free(state->findings[i].where);
		free(state->findings[i].detail);
		free(state->findings[i].source);
	}
	free(state->findings);
	for (size_t i = 0; i < state->coupling_count; i++) {
		free(state->coupling[i].component);
		free(state->coupling[i].instability);
	}
	free(state->coupling);
	for (size_t i = 0; i < state->dep_cycle_count; i++) {
		free(state->dep_cycles[i].components);
		free(state->dep_cycles[i].path);
	}
	free(state->dep_cycles);
	for (size_t i = 0; i < state->layering_count; i++) {
		free(state->layering[i].from_stratum);
		free(state->layering[i].from_function);
		free(state->layering[i].from_file);
		free(state->layering[i].to_stratum);
		free(state->layering[i].to_function);
		free(state->layering[i].to_file);
	}
	free(state->layering);
	free(state->back_call.index);
	free(state->back_call.conforming);
	free(state->skip_call.index);
	free(state->skip_call.conforming);
	dsm_free(&state->dsm);
	for (size_t i = 0; i < state->purification_count; i++) {
		free(state->purification[i].function);
		free(state->purification[i].file);
		free(state->purification[i].class_name);
		free(state->purification[i].metric);
		free(state->purification[i].value);
		free(state->purification[i].action);
		free(state->purification[i].source);
	}
	free(state->purification);
	for (size_t i = 0; i < state->recovery_count; i++)
		free(state->recovery[i].directory);
	free(state->recovery);
	for (size_t i = 0; i < state->recovery_cycles.count; i++)
		free(state->recovery_cycles.paths[i]);
	free(state->recovery_cycles.paths);
	free(state->recovery_proposal);
}

static void free_source_state(ReadState *state)
{
	for (size_t i = 0; i < state->dead_count; i++) {
		free(state->dead[i].file);
		free(state->dead[i].function);
	}
	free(state->dead);
	for (size_t i = 0; i < state->rule_match_count; i++) {
		free(state->rule_matches[i].rule);
		free(state->rule_matches[i].file);
	}
	free(state->rule_matches);
	for (size_t i = 0; i < state->definition_count; i++)
		free(state->definitions[i]);
	free(state->definitions);
	for (size_t i = 0; i < state->absent_count; i++) {
		free(state->absent[i].function);
		free(state->absent[i].file);
	}
	free(state->absent);
	free(state->image);
	for (size_t i = 0; i < state->dead_unanalysed.count; i++)
		free(state->dead_unanalysed.paths[i]);
	free(state->dead_unanalysed.paths);
}

static void read_state_free(ReadState *state)
{
	free_calltree_state(state);
	free_global_state(state);
	free_findings_state(state);
	free_source_state(state);
	routelist_free(&state->routes);
}

/* Feed the file to the parser a chunk at a time, stopping at the first
 * thing that is not a record this build can read: an I/O failure, a
 * document that is not well formed, or a handler that rejected what it
 * was given.
 *
 * Returns 0, or -1 with the diagnostic already written.
 */
static int parse_record(const char *path, XML_Parser parser, FILE *fp,
                        ReadState *state)
{
	for (;;) {
		void  *buffer = XML_GetBuffer(parser, XML_READ_CHUNK);
		size_t got;

		if (!buffer) {
			diag_printf("elc: out of memory reading the record\n");
			return -1;
		}

		got = fread(buffer, 1, XML_READ_CHUNK, fp);
		if (ferror(fp)) {
			diag_printf("elc: %s: %s\n", path, strerror(errno));
			return -1;
		}

		if (XML_ParseBuffer(parser, (int)got, got == 0) ==
		    XML_STATUS_ERROR) {
			diag_printf("elc: %s:%lu: %s\n", path,
			        XML_GetCurrentLineNumber(parser),
			        XML_ErrorString(XML_GetErrorCode(parser)));
			return -1;
		}

		if (state->failed) {
			diag_printf("elc: %s: %s\n", path, state->reason);
			return -1;
		}

		if (got == 0)
			break;
	}

	return 0;
}

/* Move what the parser reconstructed onto the report.
 *
 * Moved, not copied. The parser owns these until assembly succeeds, and the
 * report owns them after — one transfer, so neither path frees what the
 * other holds.
 */
static void move_to_report(ReadState *state, Report *out)
{
	out->depth_state     = state->depth_state;
	out->depth           = state->depth;
	out->fan_out         = state->fan_out;
	out->fan_out_count   = state->fan_out_count;
	out->cycles          = state->cycles;
	out->cycle_count     = state->cycle_count;
	out->deepest         = state->deepest;
	out->deepest_count   = state->deepest_count;
	state->fan_out       = NULL;
	state->fan_out_count = 0;
	state->cycles        = NULL;
	state->cycle_count   = 0;
	state->deepest       = NULL;
	state->deepest_count = 0;

	out->reach_state              = state->reach_state;
	out->scope_state              = state->scope_state;
	out->global_state             = state->global_state;
	out->global_state_count       = state->global_state_count;
	out->unreachable              = state->unreachable;
	out->unreachable_count        = state->unreachable_count;
	out->unreachable_globals      = state->unreachable_globals;
	out->unreachable_global_count = state->unreachable_global_count;
	out->cross_scope              = state->cross_scope;
	out->cross_scope_count        = state->cross_scope_count;
	out->findings                 = state->findings;
	out->finding_count            = state->finding_count;
	out->coupling                 = state->coupling;
	out->coupling_count           = state->coupling_count;
	out->bottleneck_threshold     = state->bottleneck_threshold;
	out->dep_cycles               = state->dep_cycles;
	out->dep_cycle_count          = state->dep_cycle_count;
	out->strata_state             = state->strata_state;
	out->layering                 = state->layering;
	out->layering_count           = state->layering_count;
	out->back_call                = state->back_call;
	out->skip_call                = state->skip_call;
	out->dsm                      = state->dsm;
	out->purification             = state->purification;
	out->purification_count       = state->purification_count;
	out->purify_thresholds        = state->purify_thresholds;
	out->purified_nodes           = state->purified_nodes;
	out->purified_edges           = state->purified_edges;
	out->recovery_state           = state->recovery_state;
	out->recovery                 = state->recovery;
	out->recovery_count           = state->recovery_count;
	out->recovery_strata          = state->recovery_strata;
	out->recovery_cycles          = state->recovery_cycles;
	out->recovery_masked          = state->recovery_masked;
	out->recovery_excluded        = state->recovery_excluded;
	out->recovery_proposal        = state->recovery_proposal;
	state->recovery               = NULL;
	state->recovery_count         = 0;
	memset(&state->recovery_cycles, 0, sizeof state->recovery_cycles);
	state->recovery_proposal      = NULL;
	out->dead                     = state->dead;
	out->dead_count               = state->dead_count;
	out->rule_matches             = state->rule_matches;
	out->rule_match_count         = state->rule_match_count;
	out->definitions              = state->definitions;
	out->definition_count         = state->definition_count;
	out->undecided_regions        = state->undecided_regions;
	/* The filter provenance the record carries, moved onto the model so a
	 * regenerated report names the image the direct run named (HLR-147,
	 * LLR-XRD-14). The rows are already ordered: they were sorted when the
	 * record was written, and re-sorting a record's contents would let a
	 * regenerated report disagree with the one it came from. */
	out->image                      = state->image;
	out->image_unresolved           = state->image_unresolved;
	out->file_scope_eloc            = state->file_scope_eloc;
	out->pruned_lines               = state->pruned_lines;
	out->uncovered_files            = state->uncovered_files;
	out->absent                     = state->absent;
	out->absent_count               = state->absent_count;
	out->dead_unanalysed            = state->dead_unanalysed;
	state->global_state             = NULL;
	state->global_state_count       = 0;
	state->unreachable              = NULL;
	state->unreachable_count        = 0;
	state->unreachable_globals      = NULL;
	state->unreachable_global_count = 0;
	state->cross_scope              = NULL;
	state->cross_scope_count        = 0;
	state->findings                 = NULL;
	state->finding_count            = 0;
	state->coupling                 = NULL;
	state->coupling_count           = 0;
	state->dep_cycles               = NULL;
	state->dep_cycle_count          = 0;
	state->layering                 = NULL;
	state->layering_count           = 0;
	memset(&state->back_call, 0, sizeof state->back_call);
	memset(&state->skip_call, 0, sizeof state->skip_call);
	memset(&state->dsm, 0, sizeof state->dsm);
	state->purification             = NULL;
	state->purification_count       = 0;
	state->dead                     = NULL;
	state->dead_count               = 0;
	state->rule_matches             = NULL;
	state->rule_match_count         = 0;
	state->definitions              = NULL;
	state->definition_count         = 0;
	state->image                    = NULL;
	state->absent                   = NULL;
	state->absent_count             = 0;
	memset(&state->dead_unanalysed, 0, sizeof state->dead_unanalysed);
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
		diag_printf("elc: %s: %s\n", path, strerror(errno));
		goto cleanup;
	}

	parser = XML_ParserCreate(NULL);
	if (!parser) {
		diag_printf("elc: out of memory creating the XML parser\n");
		goto cleanup;
	}

	XML_SetUserData(parser, &state);
	XML_SetElementHandler(parser, on_start, on_end);

	if (parse_record(path, parser, fp, &state) != 0)
		goto cleanup;

	if (!state.saw_root) {
		diag_printf("elc: %s: not an elc report\n", path);
		goto cleanup;
	}

	/* Assembled by the same function a live run uses, so every derived
	 * value is derived once and the two paths cannot drift (HLR-056). */
	if (report_assemble(&acc, &state.routes, opts, out) != 0)
		goto cleanup;
	report_set_unresolved(out, state.unresolved);

	move_to_report(&state, out);
	/* After the rows are restored, and by the same function the live path
	 * calls: the degrees are joined onto the functions and the threshold
	 * listing is rebuilt over the joined result. `report_assemble` cannot
	 * do it — the per-file metrics it works over carry no degree until
	 * this runs (HLR-183, HLR-187). */
	if (report_attach_flow(out) != 0) {
		diag_printf("elc: out of memory restoring the flow figures\n");
		goto cleanup;
	}

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
	read_state_free(&state);
	if (parser)
		XML_ParserFree(parser);
	if (fp)
		fclose(fp);
	return status;
}
