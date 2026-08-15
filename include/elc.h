/* elc.h — types shared across the elc pipeline.
 *
 * The header grows one phase at a time: each phase adds the fields the SDD's
 * data dictionary describes for the stage it builds, and no more, so that a
 * field in this header always has code behind it. See doc/SDD.md §18.
 */
#ifndef ELC_H
#define ELC_H

#include <stddef.h>
#include <stdint.h>

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

/* Name of the environment variable that overrides the runtime location
 * adjacent to the executable (HLR-059). */
#define ELC_RUNTIME_DIR_ENV "ELC_RUNTIME_DIR"

/* The complexity at or above which a function is listed for its file
 * (HLR-021, HLR-022). Reporting only: no threshold ever reaches the exit
 * status (HLR-023). */
#define ELC_DEFAULT_COMPLEXITY_THRESHOLD 15u

/* The structure of the XML record this build writes and accepts (HLR-061).
 *
 * Incremented whenever an element is removed or its meaning changes — not
 * when one is added, since a reader ignores elements it does not recognise.
 * A record carrying any other version is rejected rather than read
 * optimistically (HLR-058). */
#define ELC_XML_FORMAT_VERSION 1

/* What the run is being asked to do. */
typedef enum {
	MODE_ANALYSE = 0,
	MODE_REGENERATE   /* rebuild a report from a saved record (HLR-055) */
} RunMode;

/* The rendered form of the report. */
typedef enum {
	FORMAT_TABLE = 0, /* the default (HLR-027)                          */
	FORMAT_CSV,       /* one record per function, flat (HLR-028)         */
	FORMAT_XML,       /* the complete record of a run (HLR-054)          */
	FORMAT_MARKDOWN   /* GitHub-Flavored Markdown (HLR-029)              */
} OutputFormat;

/* The complete, validated configuration of one run.
 *
 * Populated only by cli_parse() and read-only thereafter (HLR-039): there is
 * no configuration file and no dotfile discovery, so this structure and the
 * runtime directory are the whole of elc's configuration surface.
 */
typedef struct {
	RunMode       mode;
	OutputFormat  format;
	const char   *input_path;   /* the saved record, in regeneration mode */
	const char   *output_path;  /* NULL when writing to stdout (HLR-030) */
	uint32_t      complexity_threshold; /* listing only; never the exit
	                                     * status (HLR-022, HLR-023)     */
	const char  **targets;      /* borrowed from argv; not owned          */
	size_t        target_count;
} ElcOptions;

/* The metrics for one reported function, including nested named functions.
 *
 * Phase 2 carries identity. `eloc` arrives in Phase 3, `complexity` in
 * Phase 4, and `node_id` with the graph in Phase 8 (doc/SDD.md §18).
 */
typedef struct {
	char     *name;       /* copied out of the mapping before it is
	                       * released, since the name outlives it        */
	uint32_t  start_line; /* 1-based; TSPoint.row is 0-based and
	                       * converted exactly once                      */
	uint32_t  end_line;   /* 1-based                                     */
	uint32_t  eloc;       /* statements attributed to this function
	                       * alone, never to one enclosing it (HLR-068)  */
	uint32_t  complexity; /* 1 + the decision points attributed to it    */
} FunctionMetric;

/* Per-file totals and the functions the file defines. */
typedef struct {
	char           *path;           /* canonical absolute path; owned   */
	char           *language;       /* owned; a copy of the language
	                                 * module's name, so that a model
	                                 * rebuilt from a saved record — where
	                                 * no module exists — releases it the
	                                 * same way                          */
	uint32_t        physical_lines; /* newline count from the mapping    */
	uint32_t        eloc;           /* file-level ELOC, including code
	                                 * outside any function (HLR-019)    */
	FunctionMetric *functions;      /* dynamic array, grown by doubling  */
	size_t          function_count;
} FileMetrics;

#endif /* ELC_H */
