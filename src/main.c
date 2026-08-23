/* main.c — entry point.
 *
 * Sequences the pipeline, owns the run-level state that outlives any single
 * stage, and translates the accumulated failure record into a process exit
 * status. Contains no analysis logic of its own (doc/SDD.md §3).
 *
 * The whole run happens on the thread main() was entered on; no stage
 * creates another (HLR-041, LLR-MAIN-14).
 *
 * Phase 2 wires the runtime registry ahead of discovery, and the single
 * parse behind it. Each later phase inserts its stage into this same
 * sequence, in the order the SDD's flow describes.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "analyze.h"
#include "arch.h"
#include "calltree.h"
#include "format_graph.h"
#include "graph.h"
#include "cli.h"
#include "discover.h"
#include "elc.h"
#include "elfsyms.h"
#include "format_csv.h"
#include "format_dsm.h"
#include "format_text.h"
#include "format_xml.h"
#include "purify.h"
#include "recover.h"
#include "registry.h"
#include "report.h"
#include "state.h"
#include "thresholds.h"

/* Dispatch to the renderer the options selected. Every one is a pure
 * consumer of the same assembled model, which is what makes the formats
 * views of one run rather than four separate reports.
 *
 * The verbosity reaches the two human-facing renderers and stops there. CSV
 * and XML are defined as complete — one record per function, and every element
 * of the run — so there is no presentation for a verbosity to select between,
 * and a verbose request against either is honoured by changing nothing rather
 * than rejected (HLR-152, LLR-MAIN-21).
 */
static int render(const Report *report, const ElcOptions *opts, FILE *out)
{
	Verbosity verbosity = opts->verbose ? VERBOSITY_VERBOSE
	                                    : VERBOSITY_SUMMARY;

	switch (opts->format) {
	case FORMAT_CSV:      return format_csv(report, out);
	case FORMAT_XML:      return xml_write_report(report, out);
	case FORMAT_MARKDOWN: return format_markdown(report, verbosity, out);
	case FORMAT_TABLE:
	default:              return format_table(report, verbosity, out);
	}
}

/* Everything one run acquires, in the order it is acquired — which is the
 * reverse of the order run_free releases it.
 *
 * Gathered into one record so that the stages of the run can be separate
 * functions without each taking a dozen arguments, and so that the teardown
 * HLR-125 requires is written once against the whole set rather than once per
 * exit (LLR-MAIN-16).
 */
typedef struct {
	ElcOptions         opts;
	Registry           registry;
	SymbolSet          image;
	bool               filtered;
	FileList           files;
	FactList           facts_list;
	Sdg                sdg;
	bool               graph_built;
	TreeResults        tree;
	StateResults       state;
	ArchResults        arch;
	Manifest           manifest;
	PurifyResults      purify;
	RecoveryResults    recovery;
	FindingList        findings;
	RouteList          routes;
	MetricsAccumulator acc;
	Report             report;
	FILE              *out;
	size_t             failures;
} Run;

/* Release every resource the run acquired, whatever stage it reached.
 *
 * Called on every path, the error paths included, so that a run ending in an
 * invalid target exits as leak-clean as one that succeeds (HLR-125,
 * LLR-MAIN-16). Each of the frees below tolerates a zeroed record, which is
 * what makes one teardown enough for every stage.
 */
static void run_free(Run *run)
{
	if (run->out && run->out != stdout)
		fclose(run->out);
	findinglist_free(&run->findings);
	recovery_results_free(&run->recovery);
	purify_results_free(&run->purify);
	manifest_free(&run->manifest);
	arch_results_free(&run->arch);
	state_results_free(&run->state);
	tree_results_free(&run->tree);
	graph_free(&run->sdg);
	report_free(&run->report);
	metrics_free(&run->acc);
	factlist_free(&run->facts_list);
	routelist_free(&run->routes);
	filelist_free(&run->files);
	elfsyms_free(&run->image);
	registry_close(&run->registry);
	cli_options_free(&run->opts);
}

/* Open what the run reads from, before it reads anything.
 *
 * The order is the point. A runtime location that yields no language at all is
 * fatal, and it is fatal before any file is read rather than after a full walk
 * (HLR-036, LLR-MAIN-05). An image the user named and elc cannot read is fatal
 * for the same reason (HLR-146, LLR-MAIN-20). An invalid target ends the run
 * here too, so no report can silently cover fewer targets than were named
 * (HLR-062, LLR-MAIN-10).
 *
 * Returns 0, or -1 with the diagnostic already written.
 */
static int open_inputs(Run *run)
{
	if (registry_open(&run->opts, &run->registry) != 0)
		return -1;

	if (run->opts.image_path) {
		if (elfsyms_open(run->opts.image_path, &run->image) != 0)
			return -1;
		run->filtered = true;
	}

	/* Discovery asks the registry where the runtime location is rather than
	 * resolving it a second time. */
	if (discover_targets(&run->opts, registry_runtime_dir(&run->registry),
	                     &run->files, &run->routes, &run->failures) != 0)
		return -1;

	return 0;
}

/* Measure every discovered file, accumulating metrics and facts.
 *
 * Never fails as a whole: a per-file failure is recorded, not propagated, so
 * the run continues over the remaining files and the status reflects it at the
 * end (HLR-035, LLR-MAIN-07). A skip is not a failure — it is reported and
 * leaves the status at 0 (HLR-012, HLR-037).
 */
static void measure_files(Run *run)
{
	for (size_t i = 0; i < run->files.count; i++) {
		FileMetrics *metrics = NULL;
		FileFacts   *facts   = NULL;

		switch (analyze_file(&run->registry, &run->opts,
		                     run->filtered ? &run->image : NULL,
		                     run->files.paths[i], &metrics, &facts)) {
		case ANALYZE_SKIPPED:
			fprintf(stderr,
			        "elc: %s: no usable language module; skipped\n",
			        run->files.paths[i]);
			if (metrics_add_skipped(&run->acc, run->files.paths[i]) != 0)
				run->failures++;
			continue;
		case ANALYZE_DAMAGED:
			/* Metrics were produced and part of the file was not
			 * parsed. Both facts are kept: the measurements go into
			 * the report, and the run is a degraded one — the
			 * diagnostic is already on stderr and the exit status
			 * says so (HLR-035, HLR-037). Falls through, because
			 * the metrics are as usable as any other file's for
			 * the part they cover. */
			run->failures++;
			break;
		case ANALYZE_OK:
			break;
		default:
			run->failures++;
			continue;
		}

		if (metrics_add(&run->acc, metrics) != 0) {
			filemetrics_free(metrics);
			filefacts_free(facts);
			run->failures++;
			continue;
		}
		if (factlist_add(&run->facts_list, facts) != 0) {
			filefacts_free(facts);
			run->failures++;
		}
	}
}

/* Assemble the report, and record the findings one file's syntax already
 * answers on its own.
 *
 * Returns 0, or -1 with the diagnostic already written.
 */
static int assemble_report(Run *run)
{
	if (report_assemble(&run->acc, &run->routes, &run->opts,
	                    &run->report) != 0)
		return -1;

	/* The image the figures above describe. Recorded even though nothing
	 * later reads the set itself: a report that filtered and did not say
	 * which image it filtered by cannot be checked against the build it
	 * claims to describe (HLR-147). */
	if (run->filtered && report_set_image(&run->report, &run->image) != 0)
		return -1;

	/* Before the facts are released, and before the graph is built: a rule
	 * match is one file's syntax, needs no whole-project resolution, and
	 * must be copied before the facts are released (HLR-109). */
	if (report_set_rules(&run->report, &run->facts_list) != 0) {
		fputs("elc: out of memory collecting custom-rule matches\n",
		      stderr);
		return -1;
	}

	/* Beside the rule matches and for the same reason: dead code within a
	 * function is a property of one file's syntax, needs no whole-project
	 * resolution, and is reported whether or not the graph later finds the
	 * function reachable (HLR-137, LLR-DED-06). */
	if (report_set_dead(&run->report, &run->facts_list) != 0)
		return -1;

	return 0;
}

/* Build the graph and run the analyses that read it.
 *
 * They measure; what the numbers mean is the threshold pass's judgement
 * (SDD §10), which is why it comes last and why no severity it assigns
 * reaches the exit status (HLR-100).
 *
 * Returns 0, or -1 with the diagnostic already written.
 */
static int analyse_graph(Run *run)
{
	/* The graph is built from the assembled report, not from the raw file
	 * list: its node identifiers run in the report's sorted file order,
	 * which is what makes them a property of the source tree rather than
	 * of the order discovery happened to walk it (LLR-SDG-09).
	 *
	 * The facts are released immediately afterwards. graph_build copies
	 * what it keeps, and holding the whole project's call sites alive for
	 * the rest of the run would be memory spent on data nothing reads
	 * (SDD §18). */
	if (graph_build(&run->facts_list, &run->report, &run->sdg) != 0) {
		fputs("elc: out of memory building the dependence graph\n",
		      stderr);
		return -1;
	}
	factlist_free(&run->facts_list);
	report_set_unresolved(&run->report, graph_unresolved_count(&run->sdg));
	run->graph_built = true;

	if (calltree_analyse(&run->sdg, &run->opts, &run->tree) != 0 ||
	    report_set_calltree(&run->report, &run->tree, &run->sdg) != 0) {
		fputs("elc: out of memory analysing the call tree\n", stderr);
		return -1;
	}

	if (state_analyse(&run->sdg, &run->opts, &run->state) != 0 ||
	    report_set_state(&run->report, &run->state, &run->sdg,
	                     &run->opts) != 0) {
		fputs("elc: out of memory analysing global state\n", stderr);
		return -1;
	}

	if (arch_analyse(&run->sdg, &run->opts, &run->arch) != 0 ||
	    report_set_arch(&run->report, &run->arch, &run->sdg,
	                    &run->opts) != 0) {
		fputs("elc: out of memory analysing component coupling\n",
		      stderr);
		return -1;
	}

	/* The matrix is built here rather than inside the arch pass, because
	 * it is an arrangement of the graph's call edges over the report's
	 * components and needs both. It is built whether or not strata were
	 * declared: with none its subjects are the analysed directories, which
	 * is what makes it useful to the reader who has declared nothing
	 * (HLR-165). */
	if (dsm_build(&run->sdg, &run->report, &run->opts,
	              &run->report.dsm) != 0) {
		fputs("elc: out of memory building the dependency matrix\n",
		      stderr);
		return -1;
	}

	/* Purification runs **last among the analyses**, and reads the graph
	 * every one of them read. It is the only stage that forms a view of
	 * `elc`'s own, so it is the only one placed where nothing downstream
	 * could take its output for a measurement: it produces a second graph
	 * and hands it to nobody but the report (HLR-167). Running it here
	 * rather than earlier changes no number — that is the point of the
	 * copy — and puts the ordering beyond argument as well. */
	/* **The manifest, and only where the user named one** (HLR-176). Read
	 * before the classifications it overrules, and fatal where it cannot be
	 * read or is not a manifest: the user named the file, so the failure is
	 * theirs to correct, and a run governed by half of what they wrote is
	 * worse than one that stops. The diagnostic is already on stderr. */
	if (run->opts.manifest_path &&
	    manifest_read(run->opts.manifest_path, &run->manifest) != 0)
		return -1;

	if (purify_analyse(&run->sdg, &run->opts, &run->manifest,
	                   &run->purify) != 0 ||
	    report_set_purify(&run->report, &run->purify, &run->sdg,
	                      &run->opts) != 0) {
		fputs("elc: out of memory purifying the recovery view\n",
		      stderr);
		return -1;
	}

	/* Recovery runs after purification and before nothing at all. It reads
	 * the masked copy and hands its result to the report; no analysis takes
	 * an input from it, and `arch.c` cannot reach it — which is how a
	 * proposal is kept from becoming the baseline it would be measured
	 * against (HLR-173). Its absence of a declaration is not a reason to
	 * omit it: recovery is what a user with no strata is *given*, and it is
	 * the conformance analyses that stay omitted (HLR-115). */
	if (recover_layers(&run->purify, &run->sdg, &run->report,
	                   &run->recovery) != 0 ||
	    report_set_recovery(&run->report, &run->recovery) != 0) {
		fputs("elc: out of memory recovering a layering\n", stderr);
		return -1;
	}

	if (thresholds_apply(&run->arch, &run->tree, &run->state, &run->sdg,
	                     &run->opts, &run->findings) != 0 ||
	    report_set_findings(&run->report, &run->findings) != 0) {
		fputs("elc: out of memory evaluating thresholds\n", stderr);
		return -1;
	}

	return 0;
}

static int companion_dot(Run *run, const char *path)
{
	return graph_write_dot(&run->sdg, &run->report, path);
}

static int companion_graphml(Run *run, const char *path)
{
	return graph_write_graphml(&run->sdg, path);
}

static int companion_raw_dot(Run *run, const char *path)
{
	return graph_write_purify_dot(&run->sdg, &run->purify, false, path);
}

static int companion_purified_dot(Run *run, const char *path)
{
	return graph_write_purify_dot(&run->sdg, &run->purify, true, path);
}

/* The manifest, written from the classifications rather than from the report.
 *
 * The report drops the masking flag once a row is rendered, and the manifest
 * exists to be handed back — so it is written from what purification decided,
 * which is the only place both the class and the action it carried still are
 * (HLR-175).
 */
static int companion_manifest(Run *run, const char *path)
{
	return manifest_write(&run->purify, &run->sdg, path);
}

/* The matrix as CSV, beside the report (HLR-180).
 *
 * Written from the report model rather than from the graph, which is what
 * makes it available in regeneration mode where the other two companions are
 * not: a saved record carries the matrix, and carries no topology to redraw
 * the call tree from (HLR-054, HLR-122).
 */
static int companion_dsm(Run *run, const char *path)
{
	FILE *file = fopen(path, "w");
	int   status;

	if (!file) {
		fprintf(stderr, "elc: %s: %s\n", path, strerror(errno));
		return -1;
	}

	status = format_dsm_csv(&run->report.dsm, file);
	if (fclose(file) != 0 || status != 0) {
		fprintf(stderr, "elc: %s: the dependency matrix could not be "
		        "written\n", path);
		return -1;
	}
	return 0;
}

/* Name a companion artefact beside the report and hand the path to its writer.
 *
 * The two companions differ only in the extension they take and the writer
 * that fills them, so the naming, the diagnostic, and the rule that governs
 * both are written once: a companion that cannot be written is a recorded
 * failure, not a reason to withhold the results the user asked for
 * (LLR-DOT-05).
 */
static void write_companion(Run *run, const char *extension, const char *what,
                            int (*write_file)(Run *run, const char *path))
{
	char *companion = graph_companion_path(run->opts.output_path, extension);

	if (!companion) {
		fprintf(stderr, "elc: out of memory naming the %s file\n", what);
		run->failures++;
		return;
	}
	if (write_file(run, companion) != 0)
		run->failures++;
	free(companion);
}

/* Write the report to the selected destination, then the companions.
 *
 * Returns 0, or -1 with the diagnostic already written. A companion that fails
 * is counted in run->failures rather than returned, because the report itself
 * succeeded.
 */
static int emit(Run *run)
{
	run->out = stdout;
	if (run->opts.output_path) {
		run->out = fopen(run->opts.output_path, "w");
		if (!run->out) {
			fprintf(stderr, "elc: %s: %s\n", run->opts.output_path,
			        strerror(errno));
			return -1;
		}
	}

	/* Results go to the selected destination and nothing else does; every
	 * diagnostic above and below went to stderr (HLR-038, LLR-MAIN-12). */
	if (render(&run->report, &run->opts, run->out) != 0) {
		fprintf(stderr, "elc: %s: %s\n",
		        run->opts.output_path ? run->opts.output_path
		                              : "standard output",
		        strerror(errno));
		return -1;
	}

	/* Both companions, after the report and never instead of it. The `.dot`
	 * call tree is written unless refused and the GraphML export only when
	 * asked for; each predicate holds its own default, so this sequence
	 * expresses no opinion about either (HLR-103, HLR-106). */
	if (run->graph_built && graph_dot_warranted(&run->opts))
		write_companion(run, "dot", "call-tree", companion_dot);

	if (run->graph_built && graph_graphml_warranted(&run->opts))
		write_companion(run, "graphml", "GraphML", companion_graphml);

	/* The third companion, and the only one a regenerated report can also
	 * produce: it is written from the model rather than from the graph
	 * (HLR-180). Off unless asked for, like the GraphML export. */
	if (dsm_warranted(&run->opts))
		write_companion(run, "dsm.csv", "dependency matrix",
		                companion_dsm);

	/* The pair of drawings, and the manifest. Each follows the one
	 * companion rule: a name derived from the report's own output path, no
	 * path of its own, and nothing written when the report goes to standard
	 * output (HLR-119, HLR-175, HLR-178). The two drawings are written
	 * together because they exist to be compared — one of them alone
	 * answers half the question. */
	if (run->graph_built && graph_purify_dot_warranted(&run->opts)) {
		write_companion(run, "raw.dot", "raw call graph",
		                companion_raw_dot);
		write_companion(run, "purified.dot", "purified recovery view",
		                companion_purified_dot);
	}

	if (run->graph_built && run->opts.write_manifest &&
	    run->opts.output_path)
		write_companion(run, "manifest.json", "purification manifest",
		                companion_manifest);

	return 0;
}

int main(int argc, char *argv[])
{
	Run run    = { 0 };
	int status = ELC_EXIT_OK;

	switch (cli_parse(argc, argv, &run.opts)) {
	case CLI_HELP:
		/* Usage has already gone to stdout. Requesting help is not an
		 * error (HLR-117). */
		return ELC_EXIT_OK;
	case CLI_ERROR:
		/* The specific diagnostic is on stderr; add the summary so the
		 * user sees what was expected (HLR-063).
		 *
		 * The options are released even here, and that is not
		 * housekeeping. A declaration parsed before the offending
		 * argument has already allocated — a `--stratum` accepted
		 * before a bad `--stratum-order` leaves a layer owning its name
		 * and patterns — so returning without this leaks, and a run
		 * ending in a usage error must exit as leak-clean as one that
		 * succeeds (HLR-125, LLR-MAIN-16). */
		cli_usage(stderr);
		cli_options_free(&run.opts);
		return ELC_EXIT_FATAL;
	case CLI_OK:
	default:
		break;
	}

	/* A saved record is its own input: no source file is read, no language
	 * module is loaded, and nothing is discovered (HLR-055, LLR-MAIN-03).
	 * It goes straight to rendering, which is the only stage a record has
	 * the material for. */
	if (run.opts.mode == MODE_REGENERATE) {
		if (xml_read_report(run.opts.input_path, &run.opts,
		                    &run.report) != 0)
			status = ELC_EXIT_FATAL;
	} else if (open_inputs(&run) != 0) {
		status = ELC_EXIT_FATAL;
	} else {
		measure_files(&run);
		if (assemble_report(&run) != 0 || analyse_graph(&run) != 0)
			status = ELC_EXIT_FATAL;
	}

	if (status == ELC_EXIT_OK && emit(&run) != 0)
		status = ELC_EXIT_FATAL;

	/* A file that could not be read or parsed makes the run a degraded one
	 * even though a report was produced (HLR-035); a run that failed
	 * outright already says so. */
	if (status == ELC_EXIT_OK && run.failures > 0)
		status = ELC_EXIT_FAILURE;

	run_free(&run);
	return status;
}
