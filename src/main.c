/* main.c — entry point.
 *
 * Sequences the pipeline, owns the run-level state that outlives any single
 * stage, and translates the accumulated failure record into a process exit
 * status. Contains no analysis logic of its own (doc/SDD.md §3).
 *
 * Phase 0 wires only the first stage. Each later phase inserts its stage
 * between argument parsing and the exit-status computation, in the order the
 * SDD's flow describes.
 */

#include <stdio.h>
#include <stdlib.h>

#include "cli.h"
#include "elc.h"

int main(int argc, char *argv[])
{
	ElcOptions opts;
	int status = ELC_EXIT_OK;

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

	/* Stages arrive here as later phases build them: registry_open,
	 * discover_targets, analyze_file, graph_build, the analyses,
	 * thresholds_apply, report_assemble, and the renderers. Phase 0 has
	 * none of them, so a valid command line produces no report and the
	 * run is trivially successful. */

	cli_options_free(&opts);
	return status;
}
