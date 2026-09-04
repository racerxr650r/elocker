/* include/cfg.h — a function's control flow (HLR-229).
 *
 * The first analysis in `elc` about the routes *through* a function rather
 * than a count over it. Everything else measures the syntax tree; this builds
 * a graph of basic blocks from it and asks path questions.
 *
 * It exists for one question — is a critical section left held on some path
 * out of the function — and the shape of the interface follows from that.
 */
#ifndef ELC_CFG_H
#define ELC_CFG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tree_sitter/api.h>

#include "registry.h"

/* One basic block: a run of statements with a single entry, and the blocks
 * control may reach from its end. */
typedef struct {
	uint32_t *succ;
	size_t    succ_count;
	size_t    succ_capacity;
	int       delta;   /* acquisitions minus releases within the block */
	bool      is_exit; /* control leaves the function here             */
} CfgBlock;

typedef struct {
	CfgBlock *blocks;
	size_t    block_count;
	size_t    block_capacity;
	uint32_t  entry;
	/* **False where a construct was met that this builder does not
	 * model.** A graph missing an edge answers a path question *wrongly*
	 * rather than not at all — the path that would have failed is the one
	 * the missing edge carried — so a caller must report such a function
	 * as not analysed rather than as safe (LLR-CFG-02, HLR-138). */
	bool      complete;
	/* What was met, for the diagnostic that says why. */
	const char *unmodelled;
} Cfg;

/* Build the control-flow graph of one function body (LLR-CFG-01).
 *
 * `body` is the function's compound statement. Acquisitions and releases are
 * located with the language's synchronisation query, which may be absent — a
 * module supplying none yields a graph with no marks, and no finding.
 *
 * Returns 0 with `out` populated, or -1 on allocation failure.
 */
int cfg_build(TSNode body, const LanguageModule *lang, Registry *reg,
              const char *data, Cfg *out);

/* Whether some path from an acquisition reaches an exit still holding it
 * (LLR-CFG-03).
 *
 * The search is keyed on the block *together with the number held*, not on the
 * block alone: a lock taken inside a loop and released after it is not a leak,
 * and a search that has already visited a block in one lock state must visit
 * it again in another or it answers from whichever state it arrived in first.
 */
bool cfg_leaks(const Cfg *g);

void cfg_free(Cfg *g);

#endif /* ELC_CFG_H */
