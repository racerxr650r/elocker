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

#include "concurrency.h"
#include "diag.h"
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
#include "report_html.h"
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
static int render_to(const Report *report, const Sdg *g,
                     const ElcOptions *opts, FILE *out)
{
	Verbosity verbosity = opts->verbose ? VERBOSITY_VERBOSE
	                                    : VERBOSITY_SUMMARY;

	switch (opts->format) {
	case FORMAT_CSV:      return format_csv(report, out);
	case FORMAT_XML:      return xml_write_report(report, out);
	case FORMAT_MARKDOWN: return format_markdown(report, verbosity, out);
	/* The one renderer that takes the graph as well as the model: the
	 * topology is the thing it presents rather than a figure derived from
	 * it. Reachable only outside regeneration mode, where there is no
	 * graph to hand it and the format is refused at parse time
	 * (HLR-122). */
	case FORMAT_HTML:     return format_html(report, g, opts, out);
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
	/* The second thread of control (HLR-227, HLR-228). `async_roots`
	 * carries its own state, so an empty set says whether it is empty
	 * because nothing was found or because nothing was supplied to look
	 * with — which are different claims and are reported differently. */
	RootSet            async_roots;
	bool              *reentrant;
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
	rootset_free(&run->async_roots);
	free(run->reentrant);
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
			diag_printf("elc: %s: no usable language module; skipped\n",
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
	/* Before anything is reported: a filtered run whose result would rest
	 * on a guess is refused rather than qualified (HLR-193). The check
	 * needs the whole project, which is why it is here and not in the
	 * per-file parse the filter is applied in. */
	if (run->filtered &&
	    report_check_image_ambiguity(&run->report, &run->image) != 0)
		return -1;

	if (run->filtered && report_set_image(&run->report, &run->image) != 0)
		return -1;

	/* Before the facts are released, and before the graph is built: a rule
	 * match is one file's syntax, needs no whole-project resolution, and
	 * must be copied before the facts are released (HLR-109). */
	if (report_set_rules(&run->report, &run->facts_list) != 0) {
		diag_printf("elc: out of memory collecting custom-rule matches\n");
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

/* Build the dependence graph and release what it was built from.
 *
 * The graph is built from the assembled report, not from the raw file list:
 * its node identifiers run in the report's sorted file order, which is what
 * makes them a property of the source tree rather than of the order discovery
 * happened to walk it (LLR-SDG-09).
 *
 * The facts are released immediately afterwards. graph_build copies what it
 * keeps, and holding the whole project's call sites alive for the rest of the
 * run would be memory spent on data nothing reads (SDD §18).
 */
static int build_dependence_graph(Run *run)
{
	if (graph_build(&run->facts_list, &run->report, &run->sdg) != 0) {
		diag_printf("elc: out of memory building the dependence graph\n");
		return -1;
	}

	factlist_free(&run->facts_list);
	report_set_unresolved(&run->report, graph_unresolved_count(&run->sdg));
	run->graph_built = true;
	return 0;
}

/* The four measurements taken over the graph as it was built.
 *
 * Every one of them reads the same graph and none reads another's output, so
 * the order below is the order they are reported in and carries no other
 * meaning.
 */
static int analyse_measurements(Run *run)
{
	if (calltree_analyse(&run->sdg, &run->opts, &run->tree) != 0 ||
	    report_set_calltree(&run->report, &run->tree, &run->sdg) != 0) {
		diag_printf("elc: out of memory analysing the call tree\n");
		return -1;
	}

	/* The degrees join the functions they belong to, and the threshold
	 * listing is rebuilt over the joined result. Here rather than inside
	 * `report_set_calltree` because the regeneration path calls the same
	 * function at the equivalent point, and a join buried inside a setter
	 * would be a join one of the two paths could forget (HLR-183,
	 * HLR-187). */
	if (report_attach_flow(&run->report) != 0) {
		diag_printf("elc: out of memory listing threshold breaches\n");
		return -1;
	}

	if (state_analyse(&run->sdg, &run->opts, &run->state) != 0 ||
	    report_set_state(&run->report, &run->state, &run->sdg,
	                     &run->opts) != 0) {
		diag_printf("elc: out of memory analysing global state\n");
		return -1;
	}

	if (arch_analyse(&run->sdg, &run->opts, &run->arch) != 0 ||
	    report_set_arch(&run->report, &run->arch, &run->sdg,
	                    &run->opts) != 0) {
		diag_printf("elc: out of memory analysing component coupling\n");
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
		diag_printf("elc: out of memory building the dependency matrix\n");
		return -1;
	}

	return 0;
}

/* The recovery view, and the layering proposed over it.
 *
 * **Purification runs last among the analyses**, and reads the graph every one
 * of them read. It is the only stage that forms a view of `elc`'s own, so it
 * is the only one placed where nothing downstream could take its output for a
 * measurement: it produces a second graph and hands it to nobody but the
 * report (HLR-167). Running it here rather than earlier changes no number —
 * that is the point of the copy — and puts the ordering beyond argument as
 * well.
 *
 * Recovery then reads the masked copy and hands its result to the report; no
 * analysis takes an input from it, and `arch.c` cannot reach it — which is how
 * a proposal is kept from becoming the baseline it would be measured against
 * (HLR-173). Its absence of a declaration is not a reason to omit it: recovery
 * is what a user with no strata is *given*, and it is the conformance analyses
 * that stay omitted (HLR-115).
 */
static int analyse_recovery(Run *run)
{
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
		diag_printf("elc: out of memory purifying the recovery view\n");
		return -1;
	}

	if (recover_layers(&run->purify, &run->sdg, &run->report,
	                   &run->recovery) != 0 ||
	    report_set_recovery(&run->report, &run->recovery) != 0) {
		diag_printf("elc: out of memory recovering a layering\n");
		return -1;
	}

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
/* The two row builders of `publish_concurrency`.
 *
 * Both copy out of the graph and into the model, because the report outlives
 * the graph: a row holding a borrowed node name renders as plausible garbage
 * once the graph is released, which is the worst way for it to be wrong
 * (LLR-SDG-12).
 *
 * Both hand back whatever they managed to build and report success separately,
 * so a failed `strdup` halfway through leaves the rows owned by exactly one
 * party: the report frees them either way, and a builder that freed its own
 * partial result on the way out would leave the caller holding a pointer to it
 * (HLR-125).
 */
static AsyncRootRow *build_root_rows(Run *run, int *status)
{
	AsyncRootRow *rows;

	*status = 0;
	if (!run->async_roots.count)
		return NULL;

	rows = calloc(run->async_roots.count, sizeof *rows);
	if (!rows) {
		*status = -1;
		return NULL;
	}

	for (size_t i = 0; i < run->async_roots.count; i++) {
		const AsyncRoot *r = &run->async_roots.items[i];
		const SdgNode   *n = &run->sdg.nodes[r->node];

		rows[i].function   = strdup(n->name);
		rows[i].file       = strdup(n->file ? n->file : "");
		rows[i].by_address = r->origin == ROOT_BY_ADDRESS;
		rows[i].by_wrapper = r->origin == ROOT_BY_WRAPPER;
		if (!rows[i].function || !rows[i].file)
			*status = -1;
	}

	return rows;
}

/* One row per re-entrant function, carrying where it is and what puts it there
 * (HLR-228, HLR-233).
 *
 * The attributing root travels with the name because the two together are the
 * claim: a function re-entered from an interrupt vector and one re-entered from
 * a callback the application dispatches itself are the same mark and not the
 * same finding.
 */
static ReentrantRow *build_reentrant_rows(Run *run, const bool *reentrant,
                                          const uint32_t *via, size_t *count,
                                          int *status)
{
	ReentrantRow *rows;

	*count  = 0;
	*status = 0;
	if (!reentrant)
		return NULL;

	rows = calloc(run->sdg.node_count ? run->sdg.node_count : 1,
	              sizeof *rows);
	if (!rows) {
		*status = -1;
		return NULL;
	}

	for (size_t i = 0; i < run->sdg.node_count; i++) {
		const SdgNode *n = &run->sdg.nodes[i];
		ReentrantRow  *row;
		uint32_t       root;

		if (!reentrant[i])
			continue;

		row  = &rows[(*count)++];
		root = via ? via[i] : UINT32_MAX;

		row->function    = strdup(n->name);
		row->file        = strdup(n->file ? n->file : "");
		row->via         = strdup(root < run->sdg.node_count
		                          ? run->sdg.nodes[root].name : "");
		row->via_handler = root < run->sdg.node_count &&
		                   run->sdg.nodes[root].is_interrupt;
		if (!row->function || !row->file || !row->via)
			*status = -1;
	}

	return rows;
}

/* Copy the roots and the re-entrant set into the report (HLR-227, HLR-228). */
static int publish_concurrency(Run *run, const bool *reentrant,
                               const uint32_t *via, ConcurrencyState state)
{
	int           roots_ok, marks_ok;
	AsyncRootRow *rows  = build_root_rows(run, &roots_ok);
	size_t        count = 0;
	ReentrantRow *marks = build_reentrant_rows(run, reentrant, via, &count,
	                                           &marks_ok);

	/* Handed over on every path, failure included: the rows are owned by
	 * the report from here, and returning -1 without giving them up would
	 * leak exactly what the failing allocation was short of. */
	report_set_concurrency(&run->report, state, rows,
	                       run->async_roots.count, marks, count);

	return roots_ok == 0 && marks_ok == 0 ? 0 : -1;
}

/* The two reachability sets and the re-entrant marking (HLR-228).
 *
 * Split from the caller because the two do different jobs — this one answers
 * *what does each thread reach*, and the caller decides what to report from
 * that — and because with them together the caller stood at fifteen against
 * the threshold `elc` enforces on everyone else (LLR-BLD-23).
 */
/* What each thread of control reaches, and the marking that falls out of it.
 *
 * Five arrays that are allocated together, freed together and always the same
 * length, gathered into one name so that passing them does not put five
 * parameters on every function between the analysis and its result.
 */
typedef struct {
	bool     *from_main;
	bool     *from_handler;   /* the interrupt/macro-admitted roots     */
	bool     *from_callback;  /* the address-taken roots                */
	bool     *reentrant;
	uint32_t *via;            /* the root each mark is attributed to    */
} ThreadTrees;

static void trees_free(ThreadTrees *t)
{
	free(t->from_main);
	free(t->from_handler);
	free(t->from_callback);
	free(t->reentrant);
	free(t->via);
	memset(t, 0, sizeof *t);
}

static int trees_alloc(ThreadTrees *t, size_t n)
{
	memset(t, 0, sizeof *t);

	t->from_main     = calloc(n, sizeof *t->from_main);
	t->from_handler  = calloc(n, sizeof *t->from_handler);
	t->from_callback = calloc(n, sizeof *t->from_callback);
	t->reentrant     = calloc(n, sizeof *t->reentrant);
	t->via           = calloc(n, sizeof *t->via);

	if (t->from_main && t->from_handler && t->from_callback &&
	    t->reentrant && t->via)
		return 0;

	trees_free(t);
	return -1;
}

/* The roots of one evidence class, as node identifiers the walk can take. */
static uint32_t *roots_of_class(const RootSet *roots, bool handlers,
                                size_t *count)
{
	uint32_t *nodes = calloc(roots->count ? roots->count : 1,
	                         sizeof *nodes);

	*count = 0;
	if (!nodes)
		return NULL;

	for (size_t i = 0; i < roots->count; i++)
		if (root_is_handler(roots->items[i].origin) == handlers)
			nodes[(*count)++] = roots->items[i].node;

	return nodes;
}

/* The three reachability sets and the re-entrant marking (HLR-228, HLR-231).
 *
 * **Three, because the roots are not one kind of thing.** A handler begins a
 * second thread of control; a function registered by its address may equally be
 * a callback the application's own loop dispatches, on the first. Re-entrancy is
 * measured against both together, which is what HLR-228 asks for — but the
 * qualifier findings are not, because a claim that two threads share an object
 * cannot rest on a root that may not be a second thread at all.
 *
 * Split from the caller because the two do different jobs — this one answers
 * *what does each thread reach*, and the caller decides what to report from
 * that — and because with them together the caller stood at fifteen against the
 * threshold `elc` enforces on everyone else (LLR-BLD-23).
 */
static int concurrency_trees(Run *run, const RootSet *roots, ThreadTrees *t)
{
	uint32_t *entries     = NULL;
	uint32_t *handlers    = NULL;
	uint32_t *callbacks   = NULL;
	size_t    entry_count = 0;
	size_t    n_handler   = 0;
	size_t    n_callback  = 0;
	int       status      = -1;

	handlers  = roots_of_class(roots, true, &n_handler);
	callbacks = roots_of_class(roots, false, &n_callback);
	if (!handlers || !callbacks)
		goto cleanup;

	if (graph_entry_nodes(&run->sdg, &run->opts, &entries,
	                      &entry_count) != 0)
		goto cleanup;

	if (concurrency_reentrant(&run->sdg, entries, entry_count, roots,
	                          t->reentrant, t->via) != 0)
		goto cleanup;

	if (state_reachable(&run->sdg, entries, entry_count, t->from_main) != 0)
		goto cleanup;
	if (state_reachable(&run->sdg, handlers, n_handler,
	                    t->from_handler) != 0)
		goto cleanup;
	if (state_reachable(&run->sdg, callbacks, n_callback,
	                    t->from_callback) != 0)
		goto cleanup;

	status = 0;

cleanup:
	free(entries);
	free(handlers);
	free(callbacks);
	return status;
}

/* Mark the re-entrant and the handler functions on the per-function records
 * (HLR-228, HLR-233).
 *
 * Here rather than in `report.c` because it needs both the report and the
 * graph, and `report.h` is deliberately not given sight of the graph: the
 * report is the model every renderer reads, and one that had to include an
 * analysis's header to describe its own fields would tie the two together for
 * nothing.
 *
 * Matched on the definition site — file and start line — rather than on the
 * name, because a name is not unique across translation units and a `static`
 * helper repeated in three files would otherwise take the first one's mark
 * (HLR-075, LLR-BLD-25).
 *
 * **Each mark is set and never cleared**, which matters because the match is
 * not one-to-one: two nodes can share a start line — a nested function declared
 * on the line its enclosing body opens — and an assignment would let the second
 * take back what the first established.
 */
static void mark_records(Run *run, const bool *reentrant)
{
	for (size_t n = 0; n < run->sdg.node_count; n++) {
		const SdgNode *node = &run->sdg.nodes[n];

		if (!node->file || (!reentrant[n] && !node->is_interrupt))
			continue;

		for (size_t i = 0; i < run->report.file_count; i++) {
			FileMetrics *f = run->report.files[i];

			if (!f->path || strcmp(f->path, node->file) != 0)
				continue;
			for (size_t j = 0; j < f->function_count; j++) {
				if (f->functions[j].start_line !=
				    node->line_start)
					continue;
				if (reentrant[n])
					f->functions[j].is_reentrant = true;
				if (node->is_interrupt)
					f->functions[j].is_interrupt = true;
			}
		}
	}
}

/* The marks onto the nodes, and the rows into the report (HLR-228, HLR-232).
 *
 * Both are "publish what was found", and separating them from the analysis
 * above keeps that function under the complexity threshold `elc` enforces on
 * everyone else (LLR-BLD-23).
 */
static int record_concurrency(Run *run, const ThreadTrees *t, size_t n)
{
	for (size_t i = 0; i < n; i++)
		run->sdg.nodes[i].is_reentrant = t->reentrant[i];

	mark_records(run, t->reentrant);

	return publish_concurrency(run, t->reentrant, t->via,
	                           CONCURRENCY_MEASURED);
}

/* The second thread of control (HLR-227 - HLR-231).
 *
 * Runs after the thresholds because it adds to the same finding list, and
 * before the report is given that list.
 *
 * **An empty root set is not a clean bill of health**, and the two ways of
 * having one are not the same claim. Where no evidence was supplied the whole
 * analysis is omitted; where evidence was supplied and matched nothing, the
 * program has no asynchronous roots and there is nothing to report. Reporting
 * silence for both would tell a reader who forgot `--elf` that their program
 * is concurrency-clean (HLR-115).
 */
static int analyse_concurrency(Run *run)
{
	ThreadTrees t;
	size_t      n      = run->sdg.node_count;
	int         status = -1;

	/* **Published before the early return, not after it.** The state is
	 * the whole point of this analysis where there are no roots: a run
	 * given no evidence has not looked, and a reader who sees nothing must
	 * be told which of the two happened (HLR-115, HLR-227). */
	if (run->async_roots.state != ROOTS_IDENTIFIED || !n)
		return publish_concurrency(run, NULL, NULL,
			run->async_roots.state == ROOTS_NO_EVIDENCE
			        ? CONCURRENCY_OMITTED_NO_EVIDENCE
			        : CONCURRENCY_NO_ROOTS);

	if (trees_alloc(&t, n) != 0)
		return -1;

	if (concurrency_trees(run, &run->async_roots, &t) != 0)
		goto cleanup;

	if (concurrency_qualifiers(&run->sdg, t.from_main, t.from_handler,
	                           t.from_callback, &run->findings) != 0 ||
	    concurrency_sections(&run->sdg, t.reentrant, &run->findings) != 0)
		goto cleanup;

	if (record_concurrency(run, &t, n) != 0)
		goto cleanup;

	run->reentrant = t.reentrant;
	t.reentrant    = NULL;
	status         = 0;

cleanup:
	trees_free(&t);
	return status;
}

/* The asynchronous roots, identified as soon as the graph exists (HLR-227).
 *
 * **Before the measurements, not with the rest of the concurrency analysis,
 * and the order is the fix rather than a tidying.** Reachability's root set
 * includes them (LLR-STA-05), and reachability runs first — so identifying them
 * afterwards left every handler and everything below it reported as dead code
 * on one page while the next page listed the same functions as the roots of the
 * second thread of control. Nothing here reads a measurement, so there is
 * nothing to run it after.
 */
static int identify_async_roots(Run *run)
{
	const SymbolSet *image = run->opts.image_path ? &run->image : NULL;

	if (concurrency_roots(&run->sdg, &run->opts, image,
	                      &run->async_roots) != 0) {
		diag_printf("elc: out of memory identifying asynchronous "
		            "roots\n");
		return -1;
	}

	for (size_t i = 0; i < run->async_roots.count; i++) {
		const AsyncRoot *r = &run->async_roots.items[i];

		run->sdg.nodes[r->node].is_async_root = true;
		run->sdg.nodes[r->node].is_interrupt  =
			root_is_handler(r->origin);
	}

	return 0;
}

static int analyse_graph(Run *run)
{
	if (build_dependence_graph(run) != 0 ||
	    identify_async_roots(run) != 0 ||
	    analyse_measurements(run) != 0 ||
	    analyse_recovery(run) != 0)
		return -1;

	if (thresholds_apply(&run->arch, &run->tree, &run->state, &run->sdg,
	                     &run->opts, &run->findings) != 0 ||
	    analyse_concurrency(run) != 0 ||
	    report_set_findings(&run->report, &run->findings) != 0) {
		diag_printf("elc: out of memory evaluating thresholds\n");
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
		diag_printf("elc: %s: %s\n", path, strerror(errno));
		return -1;
	}

	status = format_dsm_csv(&run->report.dsm, file);
	if (fclose(file) != 0 || status != 0) {
		diag_printf("elc: %s: the dependency matrix could not be "
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
		diag_printf("elc: out of memory naming the %s file\n", what);
		run->failures++;
		return;
	}
	if (write_file(run, companion) != 0)
		run->failures++;
	free(companion);
}

/* Every companion the options warrant, after the report and never instead of
 * it.
 *
 * Each follows the one companion rule: a name derived from the report's own
 * output path, no path of its own, and nothing written when the report goes to
 * standard output (HLR-119, HLR-175, HLR-178). Each predicate holds its own
 * default, so this sequence expresses no opinion about any of them.
 */
static void write_companions(Run *run)
{
	/* The `.dot` call tree is written unless refused; the GraphML export
	 * only when asked for (HLR-103, HLR-106). */
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

	/* The two drawings are written together because they exist to be
	 * compared — one of them alone answers half the question. */
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
			diag_printf("elc: %s: %s\n", run->opts.output_path,
			        strerror(errno));
			return -1;
		}
	}

	/* The diagnostic block closes here, immediately before the report it
	 * precedes: one blank line where anything was diagnosed, and nothing
	 * where a clean run diagnosed nothing (HLR-236). */
	diag_banner_end();

	/* Results go to the selected destination and nothing else does; every
	 * diagnostic above and below went to stderr (HLR-038, LLR-MAIN-12). */
	if (render_to(&run->report, &run->sdg, &run->opts, run->out) != 0) {
		diag_printf("elc: %s: %s\n",
		        run->opts.output_path ? run->opts.output_path
		                              : "standard output",
		        strerror(errno));
		return -1;
	}

	write_companions(run);

	return 0;
}

/* The debug companion, opened before any work is done.
 *
 * Before any work because that is the whole of its usefulness: a diagnostic
 * written before the companion existed is a diagnostic it does not hold, and
 * the stages that diagnose most — locating the runtime, discovering targets,
 * loading a grammar — are the earliest ones (HLR-194, LLR-MAIN-26).
 *
 * Named by the companion rule of HLR-119, so a report going to standard output
 * produces none: there is no name to derive one from, and that is not a usage
 * error. A companion that cannot be opened is a recorded failure and not a
 * reason to withhold the report the user asked for, exactly as the `.dot` is
 * (LLR-DOT-05).
 *
 * A function of its own rather than four lines in `main`, because `main` is
 * a sequencer and the four lines were enough to put it over the complexity
 * threshold `elc` holds its own source to — which its self-analysis caught
 * (LLR-BLD-23).
 *
 * Returns 0, or -1 where the companion was asked for and could not be opened.
 */
static int open_debug_companion(Run *run, int argc, char *argv[])
{
	char *path;
	int   status;

	if (!run->opts.debug_log || !run->opts.output_path)
		return 0;

	path = graph_companion_path(run->opts.output_path, "dbg");
	if (!path) {
		diag_printf("elc: out of memory naming the debug file\n");
		return -1;
	}

	status = diag_open(path, argc, argv);
	free(path);
	return status;
}

/* Head this run's diagnostics, where the report they precede is the aligned
 * table (HLR-236).
 *
 * The banner is a frame around a terminal session. A run whose report is a
 * saved document, a record, a companion or a drawing has no such session to
 * frame, and a heading on its standard error would be decoration nobody asked
 * for. `diag` writes it on the first diagnostic, so a clean run heads nothing.
 *
 * Called after `cli_parse`, because a usage error is diagnosed before a format
 * is known and before any file is read — heading that "Parsing Notifications"
 * would describe work that never began.
 *
 * A function of its own rather than a test inline, because with it inline
 * `main` stood at fifteen against the threshold `elc` enforces on everyone
 * else, and said so of itself (LLR-BLD-23).
 */
static void arm_diagnostic_banner(const ElcOptions *opts)
{
	if (opts->format == FORMAT_TABLE)
		diag_banner("Parsing Notifications");
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

	if (open_debug_companion(&run, argc, argv) != 0)
		run.failures++;

	arm_diagnostic_banner(&run.opts);

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
	diag_close();
	return status;
}
