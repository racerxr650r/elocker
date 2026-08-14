/* elc.h — types shared across the elc pipeline.
 *
 * Phase 0 defines only what the CLI and exit-status scheme require. Later
 * phases extend this header as the SDD's data dictionary describes; see
 * doc/SDD.md.
 */
#ifndef ELC_H
#define ELC_H

/* Process exit status (HLR-120).
 *
 * The three classes are distinct so a caller can tell a degraded run from a
 * run that never happened. No finding severity ever contributes (HLR-100).
 */
enum {
	ELC_EXIT_OK      = 0, /* every discovered file processed, or skipped   */
	ELC_EXIT_FAILURE = 1, /* run completed, but a file failed to be read
	                       * or parsed (HLR-035, HLR-037)                  */
	ELC_EXIT_FATAL   = 2  /* run did not complete: usage error, invalid
	                       * target, fatal runtime location, rejected
	                       * saved record (HLR-062, HLR-063, HLR-036)      */
};

/* What the run is being asked to do. Phase 5 adds MODE_REGENERATE. */
typedef enum {
	MODE_ANALYSE = 0
} RunMode;

/* The complete, validated configuration of one run.
 *
 * Populated only by cli_parse() and read-only thereafter (HLR-039): there is
 * no configuration file and no dotfile discovery, so this structure and the
 * runtime directory are the whole of elc's configuration surface.
 */
typedef struct {
	RunMode       mode;
	const char  **targets;      /* borrowed from argv; not owned          */
	size_t        target_count;
} ElcOptions;

#endif /* ELC_H */
