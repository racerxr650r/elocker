/* repair.h — repairing the source the grammar could not follow.
 *
 * Tree-sitter parses source the preprocessor has not touched, so a macro
 * standing where the grammar expects a keyword, a type, or a string is a parse
 * error — and the error region swallows the code around it, not merely the
 * macro. Three shapes account for every failure observed in the field:
 *
 *     local int f(void)                     `local` expands to `static`
 *     printf(BOLD FG_BLUE "text" RESET, x)  the leading macros are strings
 *     FUSES = { .WDTCFG = ... };            the macro is a whole declarator
 *
 * None is fixable in the grammar: `MACRO MACRO` is genuinely ambiguous with
 * `Type var`. Running a preprocessor would fix all three and cost a toolchain
 * `elc` refuses to require (HLR-135, HLR-040), include paths it cannot know,
 * header definitions attributed to whichever file included them, and the
 * correctness of every reported line number.
 *
 * So the source is repaired instead — enough for the grammar to proceed, and
 * without knowing what any macro means. Four properties bound that, and each
 * is a requirement rather than a courtesy:
 *
 *   * **Only inside a rejected region** (HLR-196). The rules are heuristics
 *     about the shape of a failure; applied to text the grammar accepted, a
 *     heuristic is a tool editing a measurement it had already taken.
 *   * **Every replacement keeps the line count** (HLR-197) — in this
 *     implementation, the byte width. That is what keeps ELOC, complexity and
 *     every reported location exactly what they would have been.
 *   * **The loop terminates** (HLR-198). A pass that does not reduce the
 *     damage is withdrawn, so a wrong rule leaves a file unrepaired rather
 *     than looping on source nobody here has seen.
 *   * **The tally is reported** (HLR-199). A repair is a guess the grammar
 *     could not make, and a figure resting on one says so.
 */
#ifndef ELC_REPAIR_H
#define ELC_REPAIR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tree_sitter/api.h>

/* Which rule made a repair, for the tally HLR-199 declares. */
typedef enum {
    REPAIR_STRING_MACRO = 0, /* an upper-case token beside a string literal */
    REPAIR_LEADING_MACRO,    /* a token in front of a declaration           */
    REPAIR_DECLARATOR_MACRO, /* `NAME =` alone at file scope                */
    REPAIR_RULE_COUNT
} RepairRule;

/* What one file's repair produced.
 *
 * `buffer` is the caller's own pointer where nothing was repaired, and an
 * owned copy where something was — so a caller frees through
 * `repair_result_free` and never decides which case it is in.
 */
typedef struct {
    TSTree     *tree;                    /* owned; the tree to measure     */
    const char *buffer;                  /* the text the tree describes    */
    size_t      length;
    char       *owned;                   /* the copy, or NULL              */
    size_t      counts[REPAIR_RULE_COUNT];
    size_t      total;
} RepairResult;

/* The name of a rule, for the report and the debug companion. */
const char *repair_rule_name(RepairRule rule);

/* Parse `data`, repairing what the grammar rejects until repairing stops
 * helping.
 *
 * A file the grammar accepts is parsed exactly once and its buffer is never
 * copied, which is what leaves the single-parse rule of HLR-076 unchanged for
 * source that needs no repair (LLR-RPR-01).
 *
 * Returns 0 with `out` filled, or -1 having written a diagnostic and taken
 * ownership of nothing.
 */
int repair_parse(TSParser *parser, const char *data, size_t length,
                 const char *path, RepairResult *out);

/* Release the tree, and the copied buffer where one was made. Safe on NULL,
 * and safe twice. */
void repair_result_free(RepairResult *out);

#endif /* ELC_REPAIR_H */
