/* registry.h — runtime-loaded language support.
 *
 * registry.c owns every piece of runtime-loaded data: where `runtime/` is,
 * the extension map, the lazily loaded language modules, and their compiled
 * queries. It is the boundary that keeps language knowledge out of the
 * binary — no language name, file extension, or grammar node type appears in
 * any `.c` file (doc/SDD.md §6).
 *
 * The query filenames and their capture names are a published contract
 * (HLR-121); see runtime/queries/README.md.
 */
#ifndef ELC_REGISTRY_H
#define ELC_REGISTRY_H

#include <stdbool.h>
#include <stddef.h>

#include <tree_sitter/api.h>

#include "elc.h"

/* The queries a language module may supply, in the order they are loaded.
 * The names are the contract; the order is an implementation detail of this
 * array.
 *
 * The first `QUERY_REQUIRED_COUNT` are **required**: a module omitting one is
 * unusable. Everything at or beyond that mark is **optional**, and its absence
 * leaves a NULL in `queries` for the consumer to notice. Making an optional
 * query required would invalidate every language module already shipped, which
 * is the thing the contract exists to prevent (HLR-121, HLR-139).
 *
 * A file that is *present* and will not compile is a defect either way, and
 * makes the module unusable whichever side of the mark it falls on: omitting a
 * file is a choice, writing a broken one is not.
 */
typedef enum {
	QUERY_FUNCTIONS = 0,
	QUERY_COMMENTS,
	QUERY_COMPLEXITY,
	QUERY_ELOC,
	QUERY_CALLS,
	QUERY_GLOBALS,
	QUERY_REQUIRED_COUNT,
	QUERY_DEADCODE = QUERY_REQUIRED_COUNT, /* optional (HLR-139) */
	QUERY_CONDITIONALS,                    /* optional (HLR-134) */
	QUERY_VISIBILITY,                      /* optional (HLR-209) */
	QUERY_SIGNATURE,                       /* optional (HLR-221) */
	QUERY_COUNT
} QueryKind;

/* One dynamically loaded language, cached after first use. */
typedef struct {
	char             *language_name; /* resolved from the extension map   */
	void             *dl_handle;     /* from dlopen(); closed last        */
	const TSLanguage *ts_lang;       /* resolved grammar entry point      */
	TSQuery          *queries[QUERY_COUNT];
	bool              usable;        /* false once a failure was reported,
	                                  * so it is not retried (HLR-070)    */
} LanguageModule;

/* One compiled user-supplied rule, bound to the language it applies to.
 *
 * The binding is not decoration: a Tree-sitter query compiles against one
 * specific TSLanguage, so a rule has no meaning apart from the grammar it was
 * compiled for (HLR-107). It arrives either from the directory holding the
 * file or from the `lang:path` argument form, and by the time it is here the
 * two are indistinguishable — which is the point, since what a rule *does* is
 * the same however it was supplied. Only failure handling differs, and that is
 * settled before this record exists (HLR-116).
 *
 * `stem` is half an identity. The other half is the capture name that matched,
 * so one file expresses as many named rules as it holds captures (HLR-109).
 */
typedef struct {
	char    *stem;     /* the file's basename, extension removed; owned */
	char    *language; /* the language it is bound to; owned            */
	TSQuery *query;    /* compiled against that language's grammar      */
} CustomRule;

/* One extension-to-language association, read from runtime data. */
typedef struct {
	char *extension; /* including its leading period, matched caselessly */
	char *language;
} ExtensionMapping;

/* Everything loaded from the runtime location, plus the parser and cursor
 * reused across the whole run. Allocating either per file is expensive and
 * buys nothing: reuse needs only ts_parser_set_language() per file. */
typedef struct {
	char             *dir;          /* the resolved runtime location      */
	ExtensionMapping *map;
	size_t            map_count;
	size_t            map_capacity;
	LanguageModule   *modules;
	size_t            module_count;
	size_t            module_capacity;
	CustomRule       *rules;         /* HLR-107; empty unless supplied   */
	size_t            rule_count;
	size_t            rule_capacity;
	TSParser         *parser;
	TSQueryCursor    *cursor;
} Registry;

/* Resolve the runtime location and load the extension map.
 *
 * The location is $ELC_RUNTIME_DIR when that is set, and the `runtime`
 * directory adjacent to the executable otherwise; the environment variable
 * wins when both are present (HLR-059, LLR-ROP-01, LLR-ROP-02). Nothing
 * outside that location is read — no configuration file, no dotfile
 * (LLR-ROP-06).
 *
 * Returns 0 on success. Returns non-zero, after a diagnostic, when the
 * location is absent or unreadable or yields no usable language whatsoever;
 * the caller then aborts before any file is processed, since no analysis is
 * possible in that state (HLR-036, LLR-ROP-04).
 *
 * No particular language is required or assumed: the registry succeeds over
 * whatever valid modules are present (HLR-011, LLR-ROP-05).
 */
int registry_open(const ElcOptions *opts, Registry *out);

/* The language module governing a file, loaded on first use of its
 * extension and cached thereafter, so a language is loaded at most once per
 * run and a mixed-language target needs one pass (LLR-RFP-02).
 *
 * Returns NULL when the extension maps to no available language — a skip,
 * not an error (HLR-012) — and also when the module is present but unusable,
 * in which case a diagnostic has been emitted and the language marked so it
 * is not retried (HLR-070, LLR-RFP-06).
 */
const LanguageModule *registry_for_path(Registry *reg, const char *path);

/* Load and compile every user-supplied rule, binding each to its language.
 *
 * Two provenances, and the difference between them is the whole of the error
 * handling. A rule found under `runtime/queries/<lang>/rules/` is bound by the
 * directory that holds it, and one that will not read or compile is a
 * malformed runtime component: diagnosed, excluded, and survived (HLR-070). A
 * rule named on the command line is bound by the `lang:path` argument form,
 * and the same failure is a user error: diagnosed and fatal, before any file
 * is analysed (HLR-116, LLR-RLR-06, LLR-RLR-07).
 *
 * A rule naming a language with no available module is reported and skipped
 * from either provenance, because what is missing is the module rather than
 * the rule — the absence that makes a source file a skip and not a failure
 * (LLR-RLR-03).
 *
 * Nothing is read outside the runtime location and the command line: no
 * working directory, no analysis target, no dotfile (HLR-110, LLR-RLR-05).
 *
 * Returns 0 on success, non-zero when a command-line rule failed and the run
 * must stop.
 */
int registry_load_rules(Registry *reg, const ElcOptions *opts);

/* The resolved runtime location, so that another module needing runtime data
 * asks for it rather than repeating the precedence rule of HLR-059. */
const char *registry_runtime_dir(const Registry *reg);

/* Release everything the registry owns.
 *
 * The order is load-bearing and is the reason this is one function rather
 * than several: a TSQuery holds pointers into the TSLanguage that dlclose
 * unmaps, so every query is deleted first, then the parser and cursor, and
 * only then are the handles closed. The reverse order dereferences unmapped
 * memory and crashes at exit with a backtrace that explains nothing
 * (LLR-RCL-01). Safe on NULL and on a zeroed registry.
 */
void registry_close(Registry *reg);

#endif /* ELC_REGISTRY_H */
