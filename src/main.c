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
#include "format_csv.h"
#include "format_text.h"
#include "format_xml.h"
#include "registry.h"
#include "report.h"
#include "state.h"
#include "thresholds.h"

/* Dispatch to the renderer the options selected. Every one is a pure
 * consumer of the same assembled model, which is what makes the formats
 * views of one run rather than four separate reports. */
static int render(const Report *report, OutputFormat format, FILE *out)
{
	switch (format) {
	case FORMAT_CSV:      return format_csv(report, out);
	case FORMAT_XML:      return xml_write_report(report, out);
	case FORMAT_MARKDOWN: return format_markdown(report, out);
	case FORMAT_TABLE:
	default:              return format_table(report, out);
	}
}

int main(int argc, char *argv[])
{
	ElcOptions         opts;
	Registry           registry = { 0 };
	FileList           files    = { 0 };
	FactList           facts_list = { 0 };
	Sdg                sdg      = { 0 };
	TreeResults        tree     = { 0 };
	StateResults       state    = { 0 };
	ArchResults        arch     = { 0 };
	FindingList        findings = { 0 };
	bool               graph_built = false;
	RouteList          routes   = { 0 };
	MetricsAccumulator acc      = { 0 };
	Report             report   = { 0 };
	FILE              *out      = NULL;
	size_t             failures = 0;
	int                status   = ELC_EXIT_OK;

	switch (cli_parse(argc, argv, &opts)) {
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
		 * and patterns — so returning without this leaks, and a run ending
		 * in a usage error must exit as leak-clean as one that
		 * succeeds (HLR-125, LLR-MAIN-16). */
		cli_usage(stderr);
		cli_options_free(&opts);
		return ELC_EXIT_FATAL;
	case CLI_OK:
	default:
		break;
	}

	/* A saved record is its own input: no source file is read, no
	 * language module is loaded, and nothing is discovered (HLR-055,
	 * LLR-MAIN-03). */
	if (opts.mode == MODE_REGENERATE) {
		if (xml_read_report(opts.input_path, &opts, &report) != 0) {
			status = ELC_EXIT_FATAL;
			goto cleanup;
		}
		goto render;
	}

	/* The registry comes before discovery: a runtime location that yields
	 * no language at all is fatal, and it is fatal before any file is
	 * read rather than after a full walk (HLR-036, LLR-MAIN-05). */
	if (registry_open(&opts, &registry) != 0) {
		status = ELC_EXIT_FATAL;
		goto cleanup;
	}

	/* An invalid target ends the run here, before any file is measured, so
	 * no report can silently cover fewer targets than were named
	 * (HLR-062, LLR-MAIN-10). Discovery asks the registry where the
	 * runtime location is rather than resolving it a second time. */
	if (discover_targets(&opts, registry_runtime_dir(&registry), &files,
	                     &routes, &failures) != 0) {
		status = ELC_EXIT_FATAL;
		goto cleanup;
	}

	for (size_t i = 0; i < files.count; i++) {
		FileMetrics *metrics = NULL;
		FileFacts   *facts   = NULL;

		/* A per-file failure is recorded, not propagated: the run
		 * continues over the remaining files and the status reflects it
		 * at the end (HLR-035, LLR-MAIN-07). A skip is not a failure —
		 * it is reported and leaves the status at 0 (HLR-012,
		 * HLR-037). */
		switch (analyze_file(&registry, &opts, files.paths[i], &metrics,
		                     &facts)) {
		case ANALYZE_SKIPPED:
			fprintf(stderr,
			        "elc: %s: no usable language module; skipped\n",
			        files.paths[i]);
			if (metrics_add_skipped(&acc, files.paths[i]) != 0)
				failures++;
			continue;
		case ANALYZE_DAMAGED:
			/* Metrics were produced and part of the file was not
			 * parsed. Both facts are kept: the measurements go into
			 * the report, and the run is a degraded one — the
			 * diagnostic is already on stderr and the exit status
			 * says so (HLR-035, HLR-037). Falls through, because
			 * the metrics are as usable as any other file's for
			 * the part they cover. */
			failures++;
			break;
		case ANALYZE_OK:
			break;
		default:
			failures++;
			continue;
		}

		if (metrics_add(&acc, metrics) != 0) {
			filemetrics_free(metrics);
			filefacts_free(facts);
			failures++;
			continue;
		}
		if (factlist_add(&facts_list, facts) != 0) {
			filefacts_free(facts);
			failures++;
		}
	}

	if (report_assemble(&acc, &routes, &opts, &report) != 0) {
		status = ELC_EXIT_FATAL;
		goto cleanup;
	}

	/* Before the facts are released, and before the graph is built: dead
	 * code within a function is a property of one file's syntax, needs no
	 * whole-project resolution, and is reported whether or not the graph
	 * later finds the function reachable (HLR-137, LLR-DED-06). */
	/* Beside the dead-code findings and for the same reason: a rule match
	 * is one file's syntax, needs no whole-project resolution, and must be
	 * copied before the facts are released (HLR-109). */
	if (report_set_rules(&report, &facts_list) != 0) {
		fputs("elc: out of memory collecting custom-rule matches\n",
		      stderr);
		status = ELC_EXIT_FATAL;
		goto cleanup;
	}

	if (report_set_dead(&report, &facts_list) != 0) {
		status = ELC_EXIT_FATAL;
		goto cleanup;
	}

	/* The graph is built from the assembled report, not from the raw file
	 * list: its node identifiers run in the report's sorted file order,
	 * which is what makes them a property of the source tree rather than
	 * of the order discovery happened to walk it (LLR-SDG-09).
	 *
	 * The facts are released immediately afterwards. graph_build copies
	 * what it keeps, and holding the whole project's call sites alive for
	 * the rest of the run would be memory spent on data nothing reads
	 * (SDD §18). */
	if (graph_build(&facts_list, &report, &sdg) != 0) {
		fputs("elc: out of memory building the dependence graph\n",
		      stderr);
		status = ELC_EXIT_FATAL;
		goto cleanup;
	}
	factlist_free(&facts_list);
	report_set_unresolved(&report, graph_unresolved_count(&sdg));
	graph_built = true;

	/* The analyses that read the graph. They measure; what the numbers
	 * mean is Phase 12's judgement (SDD §10). */
	if (calltree_analyse(&sdg, &opts, &tree) != 0 ||
	    report_set_calltree(&report, &tree, &sdg) != 0) {
		fputs("elc: out of memory analysing the call tree\n", stderr);
		status = ELC_EXIT_FATAL;
		goto cleanup;
	}

	if (state_analyse(&sdg, &opts, &state) != 0 ||
	    report_set_state(&report, &state, &sdg, &opts) != 0) {
		fputs("elc: out of memory analysing global state\n", stderr);
		status = ELC_EXIT_FATAL;
		goto cleanup;
	}

	if (arch_analyse(&sdg, &opts, &arch) != 0 ||
	    report_set_arch(&report, &arch, &sdg, &opts) != 0) {
		fputs("elc: out of memory analysing component coupling\n",
		      stderr);
		status = ELC_EXIT_FATAL;
		goto cleanup;
	}

	/* Last, and after every analysis, because it judges what they
	 * measured. No severity it assigns reaches `status`: the exit code is
	 * reserved for failures, and a critical finding on a run that read
	 * every file is still a successful run (HLR-100). */
	if (thresholds_apply(&arch, &tree, &state, &sdg, &opts, &findings) != 0 ||
	    report_set_findings(&report, &findings) != 0) {
		fputs("elc: out of memory evaluating thresholds\n", stderr);
		status = ELC_EXIT_FATAL;
		goto cleanup;
	}

render:
	out = stdout;
	if (opts.output_path) {
		out = fopen(opts.output_path, "w");
		if (!out) {
			fprintf(stderr, "elc: %s: %s\n", opts.output_path,
			        strerror(errno));
			status = ELC_EXIT_FATAL;
			goto cleanup;
		}
	}

	/* Results go to the selected destination and nothing else does; every
	 * diagnostic above and below went to stderr (HLR-038, LLR-MAIN-12). */
	if (render(&report, opts.format, out) != 0) {
		fprintf(stderr, "elc: %s: %s\n",
		        opts.output_path ? opts.output_path : "standard output",
		        strerror(errno));
		status = ELC_EXIT_FATAL;
		goto cleanup;
	}

	/* Both companions, after the report and never instead of it. One that
	 * cannot be written is a recorded failure, not a reason to withhold the
	 * results the user asked for (LLR-DOT-05). The `.dot` call tree is
	 * written unless refused and the GraphML export only when asked for;
	 * each predicate holds its own default, so this sequence expresses no
	 * opinion about either (HLR-103, HLR-106). */
	if (graph_built && graph_dot_warranted(&opts)) {
		char *companion = graph_companion_path(opts.output_path, "dot");

		if (!companion) {
			fputs("elc: out of memory naming the call-tree file\n",
			      stderr);
			failures++;
		} else {
			if (graph_write_dot(&sdg, &report, companion) != 0)
				failures++;
			free(companion);
		}
	}

	if (graph_built && graph_graphml_warranted(&opts)) {
		char *companion = graph_companion_path(opts.output_path,
		                                       "graphml");

		if (!companion) {
			fputs("elc: out of memory naming the GraphML file\n",
			      stderr);
			failures++;
		} else {
			if (graph_write_graphml(&sdg, companion) != 0)
				failures++;
			free(companion);
		}
	}

	if (failures > 0)
		status = ELC_EXIT_FAILURE;

cleanup:
	/* Every acquired resource is released on every path, the error paths
	 * included, so a run that ends in an invalid target exits as
	 * leak-clean as one that succeeds (HLR-125, LLR-MAIN-16). */
	if (out && out != stdout)
		fclose(out);
	findinglist_free(&findings);
	arch_results_free(&arch);
	state_results_free(&state);
	tree_results_free(&tree);
	graph_free(&sdg);
	report_free(&report);
	metrics_free(&acc);
	factlist_free(&facts_list);
	routelist_free(&routes);
	filelist_free(&files);
	registry_close(&registry);
	cli_options_free(&opts);
	return status;
}
