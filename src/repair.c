/* repair.c — repairing the source the grammar could not follow.
 *
 * See doc/SDD.md §25 and the header for what bounds this and why each bound is
 * a requirement rather than a courtesy.
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "repair.h"

/* A pass that achieves nothing ends the loop, so this is a backstop rather
 * than the mechanism: it bounds the pathological case where each pass reduces
 * the damage by one region on a file with thousands. */
#define REPAIR_MAX_PASSES 8u

const char *repair_rule_name(RepairRule rule)
{
	switch (rule) {
	case REPAIR_STRING_MACRO:     return "macro adjacent to a string";
	case REPAIR_LEADING_MACRO:    return "macro before a declaration";
	case REPAIR_DECLARATOR_MACRO: return "macro as a declarator";
	case REPAIR_RULE_COUNT:
	default:                      return "";
	}
}

/* ------------------------------------------------------------- regions -- */

typedef struct {
	uint32_t from;   /* byte offsets into the buffer */
	uint32_t to;
} Region;

typedef struct {
	Region *items;
	size_t  count;
	size_t  capacity;
} RegionList;

static int region_add(RegionList *list, uint32_t from, uint32_t to)
{
	if (list->count == list->capacity) {
		size_t  next   = list->capacity ? list->capacity * 2 : 16;
		Region *bigger = realloc(list->items, next * sizeof *bigger);

		if (!bigger)
			return -1;
		list->items    = bigger;
		list->capacity = next;
	}

	list->items[list->count].from = from;
	list->items[list->count].to   = to;
	list->count++;
	return 0;
}

/* The regions the grammar rejected, in document order.
 *
 * `has_error` holds for every ancestor of an error, so descending only where
 * it holds walks straight to the damage and skips the sound majority of the
 * tree — the traversal the damage tally already uses, for the same reason.
 */
static int collect_regions(TSNode node, RegionList *out)
{
	if (ts_node_is_error(node) || ts_node_is_missing(node))
		return region_add(out, ts_node_start_byte(node),
		                  ts_node_end_byte(node));

	if (!ts_node_has_error(node))
		return 0;

	for (uint32_t i = 0; i < ts_node_child_count(node); i++)
		if (collect_regions(ts_node_child(node, i), out) != 0)
			return -1;

	return 0;
}

/* Widen each region to the lines it touches, and merge those that meet.
 *
 * The regions tree-sitter reports are narrower than the construct that broke:
 * `printf(BOLD FG_BLUE "s" RESET, 1)` yields `(BOLD FG_BLUE`, which excludes
 * the very literal the first rule must see, and `local int g(void)` yields
 * `int`, which excludes the macro itself. A rule reading only those bytes
 * cannot recognise any shape it is looking for.
 *
 * Widening to the line keeps the confinement HLR-196 requires — nothing
 * outside a line the grammar rejected is ever rewritten — while giving the
 * rules the context the shapes are defined in. Merging matters because two
 * regions on one line would otherwise be repaired twice, the second working
 * from offsets the first had moved.
 */
static void widen_to_lines(RegionList *list, const char *data, size_t length)
{
	size_t kept = 0;

	for (size_t i = 0; i < list->count; i++) {
		uint32_t from = list->items[i].from;
		uint32_t to   = list->items[i].to;

		while (from > 0 && data[from - 1] != '\n')
			from--;
		while (to < length && data[to] != '\n')
			to++;

		if (kept > 0 && from <= list->items[kept - 1].to) {
			if (to > list->items[kept - 1].to)
				list->items[kept - 1].to = to;
			continue;
		}
		list->items[kept].from = from;
		list->items[kept].to   = to;
		kept++;
	}

	list->count = kept;
}

static size_t error_count(TSNode root)
{
	RegionList list = { 0 };
	size_t     n;

	if (collect_regions(root, &list) != 0) {
		free(list.items);
		return SIZE_MAX;   /* treat as "no improvement" and stop */
	}
	n = list.count;
	free(list.items);
	return n;
}

/* --------------------------------------------------------------- rules --
 *
 * Every rule rewrites in place and **keeps the byte width of what it
 * replaced** (LLR-RPR-03). The line count is what HLR-197 requires; the width
 * is how this implementation obtains it, and it buys the byte offsets too —
 * the regions collected before a pass stay valid throughout it.
 *
 * A rule whose replacement would not fit declines to fire, which is why each
 * one tests the width before writing.
 */

static bool is_ident(char c)
{
	return isalnum((unsigned char)c) || c == '_';
}

/* An identifier in upper case: the spelling a macro conventionally takes.
 * Three characters at least, so a single-letter loop variable beside a string
 * is never mistaken for one. */
static bool upper_macro(const char *s, size_t from, size_t to)
{
	if (to - from < 3)
		return false;

	for (size_t i = from; i < to; i++)
		if (!(isupper((unsigned char)s[i]) || isdigit((unsigned char)s[i])
		      || s[i] == '_'))
			return false;

	return isupper((unsigned char)s[from]) || s[from] == '_';
}

/* Rule 1: an upper-case token adjacent to a string literal expands to a
 * string. Replaced by an empty literal padded to the width it occupied, which
 * concatenates with the literal beside it exactly as the expansion would. */
/* One scan of the region. Repeated by the caller until it settles, because a
 * replacement creates the adjacency the next token needs: in
 * `printf(BOLD FG_BLUE "text" RESET, x)` only `FG_BLUE` and `RESET` touch a
 * literal to begin with, and `BOLD` becomes eligible only once `FG_BLUE` is
 * one. A single scan would leave `BOLD ""`, which is two identifiers and
 * exactly the shape the grammar rejected. */
static bool string_macro_scan(char *buf, size_t from, size_t to)
{
	bool changed = false;

	for (size_t i = from; i < to; ) {
		size_t start;

		if (buf[i] == '"') {           /* skip over a literal entire */
			for (i++; i < to && buf[i] != '"'; i++)
				if (buf[i] == '\\' && i + 1 < to)
					i++;
			i++;
			continue;
		}
		if (!is_ident(buf[i])) {
			i++;
			continue;
		}

		start = i;
		while (i < to && is_ident(buf[i]))
			i++;

		if (!upper_macro(buf, start, i))
			continue;

		/* Adjacent to a literal on one side or the other, ignoring the
		 * single space that conventionally separates them. */
		size_t before = start;
		while (before > from && buf[before - 1] == ' ')
			before--;
		size_t after = i;
		while (after < to && buf[after] == ' ')
			after++;

		bool beside = (before > from && buf[before - 1] == '"')
		           || (after < to && buf[after] == '"');

		if (!beside)
			continue;

		/* `""` is two bytes, and the token is at least three. */
		buf[start]     = '"';
		buf[start + 1] = '"';
		memset(buf + start + 2, ' ', i - start - 2);
		changed = true;
	}

	return changed;
}

static bool rule_string_macro(char *buf, size_t from, size_t to)
{
	bool changed = false;

	while (string_macro_scan(buf, from, to))
		changed = true;

	return changed;
}

/* Rule 2: a token in front of a declaration is a storage-class or attribute
 * macro. Blanked, so the declaration beneath it is what the grammar sees. */
static bool rule_leading_macro(char *buf, size_t from, size_t to)
{
	size_t i = from;
	size_t start;

	while (i < to && (buf[i] == ' ' || buf[i] == '\t'))
		i++;
	if (i >= to || !is_ident(buf[i]) || isdigit((unsigned char)buf[i]))
		return false;

	start = i;
	while (i < to && is_ident(buf[i]))
		i++;

	/* A keyword in this position is the declaration, not a macro in front
	 * of one. Blanking it would delete the very thing being measured. */
	static const char *const KEYWORDS[] = {
		"static", "extern", "const", "volatile", "inline", "register",
		"unsigned", "signed", "struct", "union", "enum", "typedef",
		"void", "int", "char", "long", "short", "float", "double",
		"return", "if", "else", "for", "while", "do", "switch", "case",
		"break", "continue", "goto", "sizeof", "default"
	};
	size_t len = i - start;

	for (size_t k = 0; k < sizeof KEYWORDS / sizeof *KEYWORDS; k++)
		if (strlen(KEYWORDS[k]) == len &&
		    strncmp(buf + start, KEYWORDS[k], len) == 0)
			return false;

	/* Something must follow it on the line, or there is no declaration for
	 * it to be standing in front of. */
	size_t next = i;

	while (next < to && (buf[next] == ' ' || buf[next] == '\t'))
		next++;
	if (next >= to || !is_ident(buf[next]))
		return false;

	memset(buf + start, ' ', len);
	return true;
}

/* Rule 3: `NAME =` alone at the head of a line at file scope is a macro
 * expanding to a declarator. Given a type, so the initialiser attaches to an
 * object rather than to nothing.
 *
 * The one rule that cannot keep the byte width: `int ` is four bytes the line
 * does not have. It stays within the line, which is what HLR-197 actually
 * requires — the width was this implementation's convenience, and rule 3 is
 * where the convenience stops being free. The pass applies regions in reverse
 * so a widened line cannot move an offset still to be used.
 */
static char *rule_declarator_macro(const char *buf, size_t from, size_t to,
                                   size_t *out_len)
{
	size_t i = from;
	size_t start;
	char  *rep;

	if (from > 0 && buf[from - 1] != '\n')
		return NULL;                   /* not at the head of a line */
	if (i >= to || !isupper((unsigned char)buf[i]))
		return NULL;

	start = i;
	while (i < to && is_ident(buf[i]))
		i++;
	if (!upper_macro(buf, start, i))
		return NULL;

	size_t eq = i;

	while (eq < to && (buf[eq] == ' ' || buf[eq] == '\t'))
		eq++;
	if (eq >= to || buf[eq] != '=')
		return NULL;

	/* `int ` before the identifier; the rest of the region unchanged. */
	*out_len = (to - from) + 4;
	rep = malloc(*out_len);
	if (!rep)
		return NULL;

	memcpy(rep, "int ", 4);
	memcpy(rep + 4, buf + from, to - from);
	return rep;
}

/* The two width-preserving rules, expressed the same way so one splice serves
 * all three. Each returns an owned replacement or NULL where its shape does
 * not match. */
static char *rule_string_macro_rep(const char *buf, size_t from, size_t to,
                                   size_t *out_len)
{
	char *rep = malloc(to - from ? to - from : 1);

	if (!rep)
		return NULL;
	memcpy(rep, buf + from, to - from);
	if (!rule_string_macro(rep, 0, to - from)) {
		free(rep);
		return NULL;
	}
	*out_len = to - from;
	return rep;
}

static char *rule_leading_macro_rep(const char *buf, size_t from, size_t to,
                                    size_t *out_len)
{
	char *rep = malloc(to - from ? to - from : 1);

	if (!rep)
		return NULL;
	memcpy(rep, buf + from, to - from);
	if (!rule_leading_macro(rep, 0, to - from)) {
		free(rep);
		return NULL;
	}
	*out_len = to - from;
	return rep;
}

/* --------------------------------------------------------------- passes -- */

typedef char *(*RepairRuleFn)(const char *, size_t, size_t, size_t *);

/* Repairs made during one pass, held until the pass is known to survive.
 * Bounded, because a pathological file must not turn a debug log into the
 * largest artefact of the run. */
#define REPAIR_LOG_MAX 256u

typedef struct {
	struct { RepairRule rule; uint32_t offset; } items[REPAIR_LOG_MAX];
	size_t count;
	size_t dropped;
} RepairLog;

/* A growable copy of the source, since rule 3 widens the line it repairs. */
typedef struct {
	char  *data;
	size_t length;
	size_t capacity;
} Buffer;

static int buffer_splice(Buffer *b, size_t from, size_t to,
                         const char *rep, size_t rep_len)
{
	size_t tail  = b->length - to;
	size_t want  = from + rep_len + tail;

	if (want > b->capacity) {
		size_t next = b->capacity ? b->capacity : 1;
		char  *bigger;

		while (next < want)
			next *= 2;
		bigger = realloc(b->data, next);
		if (!bigger)
			return -1;
		b->data     = bigger;
		b->capacity = next;
	}

	memmove(b->data + from + rep_len, b->data + to, tail);
	memcpy(b->data + from, rep, rep_len);
	b->length = want;
	return 0;
}

/* One pass: every rejected region, taken in **reverse** document order.
 *
 * Reverse because a rule may widen the line it repairs, and a splice at a
 * later offset cannot disturb an earlier one. Forward order would need every
 * remaining region adjusted after each repair, which is a second place the
 * offsets could go wrong.
 *
 * Rules are tried in a fixed order and the first whose shape matches is the
 * one applied, so two runs over one target repair identically (LLR-RPR-05).
 */
static int repair_pass(Buffer *buf, const RegionList *regions, size_t *counts,
                       RepairLog *log, bool *touched)
{
	static const RepairRuleFn RULES[REPAIR_RULE_COUNT] = {
		rule_string_macro_rep,
		rule_leading_macro_rep,
		rule_declarator_macro
	};

	for (size_t n = regions->count; n > 0; n--) {
		const Region *r = &regions->items[n - 1];

		if (r->to > buf->length || r->from >= r->to)
			continue;

		for (size_t k = 0; k < REPAIR_RULE_COUNT; k++) {
			size_t  rep_len = 0;
			char   *rep     = RULES[k](buf->data, r->from, r->to,
			                           &rep_len);

			if (!rep)
				continue;

			if (buffer_splice(buf, r->from, r->to, rep,
			                  rep_len) != 0) {
				free(rep);
				return -1;
			}
			free(rep);
			counts[k]++;
			*touched = true;
			/* Recorded, not yet reported: a pass that fails to
			 * reduce the damage is withdrawn, and a log naming
			 * repairs that were undone would describe a buffer
			 * nothing was measured from (HLR-199). */
			if (log->count < REPAIR_LOG_MAX) {
				log->items[log->count].rule   = (RepairRule)k;
				log->items[log->count].offset = r->from;
				log->count++;
			}
			log->dropped += (log->count == REPAIR_LOG_MAX);
			break;
		}
	}

	return 0;
}

int repair_parse(TSParser *parser, const char *data, size_t length,
                 const char *path, RepairResult *out)
{
	Buffer buf      = { 0 };
	char  *previous = NULL;
	size_t prev_len = 0;

	memset(out, 0, sizeof *out);

	out->tree   = ts_parser_parse_string(parser, NULL, data,
	                                     (uint32_t)length);
	out->buffer = data;
	out->length = length;
	if (!out->tree)
		return -1;

	/* The sound majority: one parse, no copy, the caller's own pointer
	 * returned. The cost of this feature to a code base that needs none of
	 * it is this one test (LLR-RPR-01). */
	if (!ts_node_has_error(ts_tree_root_node(out->tree)))
		return 0;

	buf.data = malloc(length ? length : 1);
	previous = malloc(length ? length : 1);
	if (!buf.data || !previous) {
		free(buf.data);
		free(previous);
		ts_tree_delete(out->tree);
		out->tree = NULL;
		return -1;
	}
	memcpy(buf.data, data, length);
	buf.length = buf.capacity = length;

	for (unsigned pass = 0; pass < REPAIR_MAX_PASSES; pass++) {
		RegionList regions = { 0 };
		size_t     before  = error_count(ts_tree_root_node(out->tree));
		size_t     counts[REPAIR_RULE_COUNT] = { 0 };
		bool       touched = false;

		if (collect_regions(ts_tree_root_node(out->tree),
		                    &regions) != 0) {
			free(regions.items);
			break;
		}
		widen_to_lines(&regions, buf.data, buf.length);

		/* Kept so the pass can be withdrawn whole. */
		if (buf.length > prev_len) {
			char *bigger = realloc(previous, buf.length);

			if (!bigger) {
				free(regions.items);
				break;
			}
			previous = bigger;
		}
		memcpy(previous, buf.data, buf.length);
		prev_len = buf.length;

		RepairLog log = { .count = 0, .dropped = 0 };
		int status = repair_pass(&buf, &regions, counts, &log,
		                         &touched);

		free(regions.items);
		if (status != 0 || !touched)
			break;

		TSTree *next = ts_parser_parse_string(parser, NULL, buf.data,
		                                      (uint32_t)buf.length);

		/* A pass that did not reduce the damage is withdrawn whole,
		 * which is what makes a wrong rule cheap: the file is measured
		 * unrepaired, which is where it started (LLR-RPR-04). */
		if (!next || error_count(ts_tree_root_node(next)) >= before) {
			if (next)
				ts_tree_delete(next);
			memcpy(buf.data, previous, prev_len);
			buf.length = prev_len;
			break;
		}

		ts_tree_delete(out->tree);
		out->tree = next;
		for (size_t k = 0; k < REPAIR_RULE_COUNT; k++) {
			out->counts[k] += counts[k];
			out->total     += counts[k];
		}

		/* The pass stands, so what it did is now worth recording. */
		for (size_t i = 0; i < log.count; i++)
			diag_detail("repair: %s at %s byte %u\n",
			            repair_rule_name(log.items[i].rule), path,
			            log.items[i].offset);
		if (log.dropped)
			diag_detail("repair: %zu further repairs not listed\n",
			            log.dropped);
	}

	free(previous);

	if (out->total == 0) {
		/* Nothing survived the loop: the caller's own buffer is what
		 * the tree describes, so the copy is not kept. */
		free(buf.data);
		out->buffer = data;
		out->length = length;
		return 0;
	}

	out->owned  = buf.data;
	out->buffer = buf.data;
	out->length = buf.length;
	return 0;
}

void repair_result_free(RepairResult *out)
{
	if (!out)
		return;

	if (out->tree)
		ts_tree_delete(out->tree);
	free(out->owned);
	memset(out, 0, sizeof *out);
}
