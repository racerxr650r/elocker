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
#include "cli.h"
#include "discover.h"
#include "elc.h"
#include "format_text.h"
#include "registry.h"
#include "report.h"

int main(int argc, char *argv[])
{
	ElcOptions         opts;
	Registry           registry = { 0 };
	FileList           files    = { 0 };
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
		 * user sees what was expected (HLR-063). */
		cli_usage(stderr);
		return ELC_EXIT_FATAL;
	case CLI_OK:
	default:
		break;
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
	                     &failures) != 0) {
		status = ELC_EXIT_FATAL;
		goto cleanup;
	}

	for (size_t i = 0; i < files.count; i++) {
		FileMetrics *metrics = NULL;

		/* A per-file failure is recorded, not propagated: the run
		 * continues over the remaining files and the status reflects it
		 * at the end (HLR-035, LLR-MAIN-07). A skip is not a failure —
		 * it is reported and leaves the status at 0 (HLR-012,
		 * HLR-037). */
		switch (analyze_file(&registry, files.paths[i], &metrics)) {
		case ANALYZE_SKIPPED:
			fprintf(stderr,
			        "elc: %s: no usable language module; skipped\n",
			        files.paths[i]);
			if (metrics_add_skipped(&acc, files.paths[i]) != 0)
				failures++;
			continue;
		case ANALYZE_OK:
			break;
		default:
			failures++;
			continue;
		}

		if (metrics_add(&acc, metrics) != 0) {
			filemetrics_free(metrics);
			failures++;
		}
	}

	if (report_assemble(&acc, &opts, &report) != 0) {
		status = ELC_EXIT_FATAL;
		goto cleanup;
	}

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
	if (format_table(&report, out) != 0) {
		fprintf(stderr, "elc: %s: %s\n",
		        opts.output_path ? opts.output_path : "standard output",
		        strerror(errno));
		status = ELC_EXIT_FATAL;
		goto cleanup;
	}

	if (failures > 0)
		status = ELC_EXIT_FAILURE;

cleanup:
	/* Every acquired resource is released on every path, the error paths
	 * included, so a run that ends in an invalid target exits as
	 * leak-clean as one that succeeds (HLR-125, LLR-MAIN-16). */
	if (out && out != stdout)
		fclose(out);
	report_free(&report);
	metrics_free(&acc);
	filelist_free(&files);
	registry_close(&registry);
	cli_options_free(&opts);
	return status;
}
