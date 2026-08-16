/* cli.h — command-line parsing and validation.
 *
 * cli.c is the only reader of argv and the only source of user-supplied
 * configuration (doc/SDD.md §4).
 */
#ifndef ELC_CLI_H
#define ELC_CLI_H

#include <stddef.h>
#include <stdio.h>

#include "elc.h"

/* cli_parse() outcomes. Anything other than CLI_OK means main() must not
 * proceed to analysis. */
enum {
	CLI_OK    = 0,
	CLI_HELP  = 1, /* help requested: usage already written to stdout,
	                * exit 0 — requesting help is not an error (HLR-117) */
	CLI_ERROR = 2  /* usage error: caller writes usage to stderr and
	                * exits ELC_EXIT_FATAL (HLR-063)                    */
};

/* Parse and validate argv into *out.
 *
 * Derives the options structure solely from the arguments passed to it,
 * opening no file in the working directory, the analysis target, or any
 * ancestor of either (LLR-CLI-14).
 */
int cli_parse(int argc, char *argv[], ElcOptions *out);

/* Write the usage summary — every accepted option, its argument if any, and
 * its default — to `stream` (LLR-USG-01, LLR-USG-02). */
void cli_usage(FILE *stream);

/* Parse one `name:glob[,glob…]` execution-scope declaration, appending it to
 * `out->scopes`, which takes ownership of every string it copies.
 *
 * Returns 0, or -1 after a diagnostic for a declaration that cannot be parsed
 * (LLR-SCP-01, LLR-SCP-02). Exposed so the unit level can drive the grammar of
 * the declaration directly rather than through a whole run.
 */
int parse_scope(const char *arg, ElcOptions *out);

/* Parse one `name:glob[,glob…]` architectural-stratum declaration, appending
 * it to `out->strata`, which takes ownership of every string it copies.
 * Repeating a name adds patterns to the layer already declared rather than
 * creating a second one, and a layer's ordinal is fixed when it is first
 * named (LLR-STR-01, LLR-STR-02, LLR-STR-03).
 *
 * Returns 0, or -1 after a diagnostic for a declaration that cannot be parsed.
 */
int parse_stratum(const char *arg, ElcOptions *out);

/* Release every heap allocation owned by the options structure. */
void cli_options_free(ElcOptions *opts);

#endif /* ELC_CLI_H */
