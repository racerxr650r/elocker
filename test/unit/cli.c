/* test/unit/cli.c — unit tests for src/cli.c.
 *
 * One Criterion binary per src/ module, linked against that module. Tests
 * register automatically, so a test cannot be written and silently never run
 * (doc/STP.md §2.2).
 *
 * Suite names match the module they exercise, so the suite, the module, and
 * the LLR prefix line up when a failure is read.
 */

#include <criterion/criterion.h>
#include <criterion/redirect.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "elc.h"

/* --------------------------------------------------------------- --wrap ---
 *
 * Phase 0 proves the link-time interception the whole unit level rests on
 * (STP §2.2), before anything depends on it. A wrapped allocator is the
 * mechanism by which the checked-growth contracts of later phases —
 * LLR-ANL-34, LLR-RPT-16 — become testable at all: allocation failure cannot
 * otherwise be provoked from a test.
 */
extern void *__real_malloc(size_t);

/* `volatile` is load-bearing, not decoration. The compiler treats malloc as a
 * builtin that cannot read the caller's globals, so in
 *
 *     armed = 1;  p = malloc(n);  armed = 0;
 *
 * it considers the first store dead — overwritten by the second before
 * anything could observe it — and removes it. The wrapper then runs with the
 * flag still clear and the interception silently does not happen. Every
 * arm/disarm flag guarding a wrapped function must be volatile for the same
 * reason (doc/STP.md §2.2). */
static volatile int malloc_should_fail;

void *__wrap_malloc(size_t n)
{
	if (malloc_should_fail)
		return NULL;
	return __real_malloc(n);
}

Test(cli, wrap_passes_through_when_not_armed)
{
	malloc_should_fail = 0;
	void *p = malloc(32);
	cr_assert_not_null(p, "unarmed wrap must delegate to the real malloc");
	free(p);
}

Test(cli, wrap_intercepts_when_armed)
{
	malloc_should_fail = 1;
	void *p = malloc(32);
	malloc_should_fail = 0;
	cr_assert_null(p, "armed wrap must intercept and fail the allocation");
}

/* ------------------------------------------------------------- cli_parse ---
 *
 * getopt_long keeps global state across calls, so each test resets optind
 * via cli_parse() itself; Criterion's per-test process isolation means no
 * test can leak that state into another regardless.
 */

Test(cli, help_short_option_reports_help)
{
	char *argv[] = { "elc", "-h", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(2, argv, &o), CLI_HELP);
}

Test(cli, help_long_option_reports_help)
{
	char *argv[] = { "elc", "--help", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(2, argv, &o), CLI_HELP);
}

Test(cli, unrecognised_option_is_a_usage_error)
{
	char *argv[] = { "elc", "--bogus", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(2, argv, &o), CLI_ERROR,
	             "an unrecognised option must be rejected (HLR-063)");
}

Test(cli, missing_target_is_a_usage_error)
{
	char *argv[] = { "elc", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(1, argv, &o), CLI_ERROR,
	             "an invocation with no target must be rejected (HLR-063)");
}

Test(cli, single_target_is_collected)
{
	char *argv[] = { "elc", "a.c", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(2, argv, &o), CLI_OK);
	cr_assert_eq(o.target_count, 1);
	cr_assert_str_eq(o.targets[0], "a.c");
	cli_options_free(&o);
}

Test(cli, several_targets_are_collected_in_order)
{
	char *argv[] = { "elc", "a.c", "src/", "b.c", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(4, argv, &o), CLI_OK,
	             "files and directories may be intermixed (HLR-071)");
	cr_assert_eq(o.target_count, 3);
	cr_assert_str_eq(o.targets[0], "a.c");
	cr_assert_str_eq(o.targets[1], "src/");
	cr_assert_str_eq(o.targets[2], "b.c");
	cli_options_free(&o);
}

Test(cli, help_takes_precedence_over_a_target)
{
	char *argv[] = { "elc", "--help", "a.c", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(3, argv, &o), CLI_HELP,
	             "help is reported without validating other arguments");
}

Test(cli, mode_defaults_to_analyse)
{
	char *argv[] = { "elc", "a.c", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(2, argv, &o), CLI_OK);
	cr_assert_eq(o.mode, MODE_ANALYSE);
	cli_options_free(&o);
}

Test(cli, the_output_destination_defaults_to_standard_output)
{
	char *argv[] = { "elc", "a.c", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(2, argv, &o), CLI_OK);
	cr_assert_null(o.output_path,
	               "a null output path records standard output as the "
	               "destination (HLR-030)");
	cli_options_free(&o);
}

Test(cli, an_output_path_is_collected)
{
	char *argv[] = { "elc", "--output", "report.txt", "a.c", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(4, argv, &o), CLI_OK);
	cr_assert_str_eq(o.output_path, "report.txt");
	cr_assert_eq(o.target_count, 1,
	             "the option's argument is not mistaken for a target");
	cli_options_free(&o);
}

Test(cli, the_short_output_option_behaves_as_the_long_one)
{
	char *argv[] = { "elc", "-o", "report.txt", "a.c", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(4, argv, &o), CLI_OK);
	cr_assert_str_eq(o.output_path, "report.txt");
	cli_options_free(&o);
}

Test(cli, an_output_option_without_its_argument_is_a_usage_error)
{
	char *argv[] = { "elc", "-o", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(2, argv, &o), CLI_ERROR,
	             "an option requiring an argument must be given one "
	             "(HLR-063)");
}

Test(cli, the_complexity_threshold_defaults_to_fifteen)
{
	char *argv[] = { "elc", "a.c", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(2, argv, &o), CLI_OK);
	cr_assert_eq(o.complexity_threshold, ELC_DEFAULT_COMPLEXITY_THRESHOLD);
	cr_assert_eq(o.complexity_threshold, 15,
	             "the documented default is 15 (HLR-022)");
	cli_options_free(&o);
}

Test(cli, a_complexity_threshold_is_collected)
{
	char *argv[] = { "elc", "--complexity-threshold", "7", "a.c", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(4, argv, &o), CLI_OK);
	cr_assert_eq(o.complexity_threshold, 7);
	cr_assert_eq(o.target_count, 1);
	cli_options_free(&o);
}

Test(cli, the_short_threshold_option_behaves_as_the_long_one)
{
	char *argv[] = { "elc", "-c", "7", "a.c", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(4, argv, &o), CLI_OK);
	cr_assert_eq(o.complexity_threshold, 7);
	cli_options_free(&o);
}

Test(cli, a_threshold_of_zero_is_accepted)
{
	/* Zero lists every function, which is a legitimate thing to ask for
	 * and must not be confused with "unset". */
	char *argv[] = { "elc", "-c", "0", "a.c", NULL };
	ElcOptions o;
	cr_assert_eq(cli_parse(4, argv, &o), CLI_OK);
	cr_assert_eq(o.complexity_threshold, 0);
	cli_options_free(&o);
}

Test(cli, a_malformed_threshold_is_a_usage_error)
{
	/* strtoul would accept every one of these: a sign, a trailing tail,
	 * leading whitespace, and an empty string all parse to something. */
	const char *bad[] = { "abc", "7x", "-1", " 7", "", "0x10" };

	for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
		char      *argv[] = { "elc", "-c", (char *)bad[i], "a.c", NULL };
		ElcOptions o;

		cr_assert_eq(cli_parse(4, argv, &o), CLI_ERROR,
		             "'%s' is not a threshold", bad[i]);
	}
}

Test(cli, options_free_is_safe_on_null)
{
	cli_options_free(NULL);
	cr_assert(1, "releasing a null options structure must not fault");
}

/* ------------------------------------------------- execution scopes (HLR-094) */

Test(cli, a_scope_declaration_is_parsed_into_a_name_and_patterns)
{
	ElcOptions o;

	memset(&o, 0, sizeof o);
	cr_assert_eq(parse_scope("host:src/host/*,src/shared/*", &o), 0);
	cr_assert_eq(o.scopes.count, 1);
	cr_assert_str_eq(o.scopes.items[0].name, "host");
	cr_assert_eq(o.scopes.items[0].pattern_count, 2);
	cr_assert_str_eq(o.scopes.items[0].patterns[0], "src/host/*");
	cr_assert_str_eq(o.scopes.items[0].patterns[1], "src/shared/*");
	cli_options_free(&o);
}

Test(cli, scope_declarations_accumulate)
{
	ElcOptions o;

	memset(&o, 0, sizeof o);
	cr_assert_eq(parse_scope("a:one/*", &o), 0);
	cr_assert_eq(parse_scope("b:two/*", &o), 0);
	cr_assert_eq(o.scopes.count, 2);
	cr_assert_str_eq(o.scopes.items[1].name, "b");
	cli_options_free(&o);
}

Test(cli, a_malformed_scope_declaration_is_rejected)
{
	/* A scope with no name, a name with no components, and an empty
	 * pattern between separators. Each would otherwise become a scope
	 * matching nothing, which reports nothing and looks like a clean
	 * result (LLR-SCP-02). */
	const char *bad[] = { "no-colon", ":patterns", "name:", "", ":",
	                      "name:a,,b" };

	for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
		ElcOptions o;

		memset(&o, 0, sizeof o);
		cr_assert_eq(parse_scope(bad[i], &o), -1,
		             "'%s' is not a scope declaration", bad[i]);
		cli_options_free(&o);
	}
}

Test(cli, the_scope_option_reaches_the_options_structure)
{
	char      *argv[] = { "elc", "--scope", "host:a/*", "t.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(4, argv, &o), CLI_OK);
	cr_assert_eq(o.scopes.count, 1);
	cr_assert_str_eq(o.scopes.items[0].name, "host");
	cli_options_free(&o);
}

Test(cli, a_malformed_scope_option_is_a_usage_error)
{
	char      *argv[] = { "elc", "--scope", "broken", "t.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(4, argv, &o), CLI_ERROR);
}

Test(cli, no_scope_declared_leaves_the_list_empty)
{
	/* Empty means the analysis is omitted with a stated reason, not that
	 * everything belongs to one scope (HLR-115). */
	char      *argv[] = { "elc", "t.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(2, argv, &o), CLI_OK);
	cr_assert_eq(o.scopes.count, 0);
	cli_options_free(&o);
}

/* -------------------------------------------- architectural strata (HLR-078) */

Test(cli, a_stratum_declaration_is_parsed_into_a_name_and_patterns)
{
	ElcOptions o;

	memset(&o, 0, sizeof o);
	cr_assert_eq(parse_stratum("hal:src/hal/*,src/bsp/*", &o), 0);
	cr_assert_eq(o.strata.count, 1);
	cr_assert_str_eq(o.strata.items[0].name, "hal");
	cr_assert_eq(o.strata.items[0].pattern_count, 2);
	cr_assert_str_eq(o.strata.items[0].patterns[1], "src/bsp/*");
	cli_options_free(&o);
}

Test(cli, the_declared_order_fixes_the_ordinals)
{
	/* Declaration order is the dependency direction until --stratum-order
	 * says otherwise: the first layer named is the top (LLR-STR-02). */
	ElcOptions o;

	memset(&o, 0, sizeof o);
	cr_assert_eq(parse_stratum("app:a/*", &o), 0);
	cr_assert_eq(parse_stratum("hal:h/*", &o), 0);
	cr_assert_eq(parse_stratum("drv:d/*", &o), 0);
	cr_assert_eq(o.strata.items[0].ordinal, 0);
	cr_assert_eq(o.strata.items[1].ordinal, 1);
	cr_assert_eq(o.strata.items[2].ordinal, 2);
	cli_options_free(&o);
}

Test(cli, repeating_a_stratum_name_adds_patterns_to_that_layer)
{
	/* One layer of two patterns, not two layers — which would also shift
	 * every ordinal below it and change what the layering is measured
	 * against. */
	ElcOptions o;

	memset(&o, 0, sizeof o);
	cr_assert_eq(parse_stratum("hal:a/*", &o), 0);
	cr_assert_eq(parse_stratum("drv:d/*", &o), 0);
	cr_assert_eq(parse_stratum("hal:b/*", &o), 0);
	cr_assert_eq(o.strata.count, 2);
	cr_assert_eq(o.strata.items[0].pattern_count, 2);
	cr_assert_eq(o.strata.items[1].ordinal, 1);
	cli_options_free(&o);
}

Test(cli, a_malformed_stratum_declaration_is_rejected)
{
	const char *bad[] = { "no-colon", ":patterns", "name:", "", ":",
	                      "name:a,,b" };

	for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
		ElcOptions o;

		memset(&o, 0, sizeof o);
		cr_assert_eq(parse_stratum(bad[i], &o), -1,
		             "'%s' is not a stratum declaration", bad[i]);
		cli_options_free(&o);
	}
}

Test(cli, stratum_order_reassigns_the_ordinals)
{
	/* Given after the layers it orders, which is why it is resolved once
	 * at the end of parsing rather than as it is seen. */
	char      *argv[] = { "elc", "--stratum", "drv:d/*",
	                      "--stratum", "app:a/*",
	                      "--stratum-order", "app>drv", "t.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(8, argv, &o), CLI_OK);
	cr_assert_str_eq(o.strata.items[0].name, "drv");
	cr_assert_eq(o.strata.items[0].ordinal, 1);
	cr_assert_str_eq(o.strata.items[1].name, "app");
	cr_assert_eq(o.strata.items[1].ordinal, 0);
	cli_options_free(&o);
}

Test(cli, stratum_order_may_precede_the_layers_it_orders)
{
	char      *argv[] = { "elc", "--stratum-order", "app>drv",
	                      "--stratum", "drv:d/*",
	                      "--stratum", "app:a/*", "t.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(8, argv, &o), CLI_OK);
	cr_assert_eq(o.strata.items[1].ordinal, 0);
	cli_options_free(&o);
}

Test(cli, stratum_order_naming_an_undeclared_layer_is_an_error)
{
	/* A typo whose silent acceptance would leave the layering validated
	 * against something the user did not write. */
	char      *argv[] = { "elc", "--stratum", "app:a/*",
	                      "--stratum-order", "app>nope", "t.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(6, argv, &o), CLI_ERROR);
	/* Released even on the error path: the stratum accepted before the bad
	 * order has already allocated, and the caller still owns it. main()
	 * does the same (LLR-MAIN-19). */
	cli_options_free(&o);
}

Test(cli, a_partial_stratum_order_is_an_error)
{
	/* A partial order determines no direction, and completing it silently
	 * would invent one. */
	char      *argv[] = { "elc", "--stratum", "app:a/*",
	                      "--stratum", "drv:d/*",
	                      "--stratum-order", "app", "t.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(8, argv, &o), CLI_ERROR);
	cli_options_free(&o);
}

Test(cli, stratum_order_without_any_stratum_is_an_error)
{
	char      *argv[] = { "elc", "--stratum-order", "a>b", "t.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(4, argv, &o), CLI_ERROR);
}

Test(cli, the_bottleneck_threshold_defaults_to_five)
{
	char      *argv[] = { "elc", "a.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(2, argv, &o), CLI_OK);
	cr_assert_eq(o.bottleneck_threshold, ELC_DEFAULT_BOTTLENECK_THRESHOLD);
	cli_options_free(&o);
}

Test(cli, the_bottleneck_threshold_is_configurable)
{
	char      *argv[] = { "elc", "-b", "12", "a.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(4, argv, &o), CLI_OK);
	cr_assert_eq(o.bottleneck_threshold, 12);
	cli_options_free(&o);
}

Test(cli, the_purification_thresholds_default_to_elcs_own)
{
	char      *argv[] = { "elc", "a.c", NULL };
	ElcOptions o;

	/* A zeroed structure would classify every function as everything,
	 * since every rank is at or above zero — so the defaults are what makes
	 * purification mean anything at all, and they are `elc`'s own
	 * heuristics rather than published standards (HLR-171). */
	cr_assert_eq(cli_parse(2, argv, &o), CLI_OK);
	cr_assert_eq(o.purify.sink_authority, ELC_DEFAULT_SINK_AUTHORITY);
	cr_assert_eq(o.purify.sink_hub, ELC_DEFAULT_SINK_HUB);
	cr_assert_eq(o.purify.god_betweenness, ELC_DEFAULT_GOD_BETWEENNESS);
	cr_assert_eq(o.purify.god_hub, ELC_DEFAULT_GOD_HUB);
	cr_assert_eq(o.purify.core_depth, ELC_DEFAULT_CORE_DEPTH);
	cli_options_free(&o);
}

Test(cli, every_purification_threshold_is_configurable)
{
	char      *argv[] = { "elc", "--sink-authority", "80",
	                      "--sink-hub", "5", "--god-betweenness", "70",
	                      "--god-hub", "75", "--core-depth", "3",
	                      "a.c", NULL };
	ElcOptions o;

	/* Each of the five, because a heuristic that cannot be adjusted is one
	 * whose disagreements have nowhere to go (HLR-171). */
	cr_assert_eq(cli_parse(12, argv, &o), CLI_OK);
	cr_assert_eq(o.purify.sink_authority, 80);
	cr_assert_eq(o.purify.sink_hub, 5);
	cr_assert_eq(o.purify.god_betweenness, 70);
	cr_assert_eq(o.purify.god_hub, 75);
	cr_assert_eq(o.purify.core_depth, 3);
	cli_options_free(&o);
}

Test(cli, a_rank_threshold_above_one_hundred_is_a_usage_error)
{
	static const char *const options[] = { "--sink-authority", "--sink-hub",
	                                       "--god-betweenness",
	                                       "--god-hub" };

	/* The four centrality thresholds are positions in an ordered
	 * distribution, so 100 is the ceiling and a figure above it names a
	 * position no function can occupy. Rejected rather than silently
	 * classifying nothing (HLR-063). */
	for (size_t i = 0; i < sizeof options / sizeof *options; i++) {
		char      *argv[] = { "elc", (char *)options[i], "101", "a.c",
		                      NULL };
		ElcOptions o;

		cr_assert_eq(cli_parse(4, argv, &o), CLI_ERROR,
		             "%s accepts no rank above 100", options[i]);
	}
}

Test(cli, a_rank_threshold_of_one_hundred_is_accepted)
{
	char      *argv[] = { "elc", "--sink-authority", "100", "a.c", NULL };
	ElcOptions o;

	/* The boundary itself is legitimate: a function outranking every other
	 * is exactly what a strict reading of "high authority" asks for. */
	cr_assert_eq(cli_parse(4, argv, &o), CLI_OK);
	cr_assert_eq(o.purify.sink_authority, 100);
	cli_options_free(&o);
}

Test(cli, a_malformed_purification_threshold_is_a_usage_error)
{
	const char *bad[] = { "abc", "7x", "-1", " 7", "", "0x10" };

	for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
		char      *argv[] = { "elc", "--core-depth", (char *)bad[i],
		                      "a.c", NULL };
		ElcOptions o;

		cr_assert_eq(cli_parse(4, argv, &o), CLI_ERROR,
		             "'%s' is not a core depth", bad[i]);
	}
}

Test(cli, a_malformed_bottleneck_threshold_is_a_usage_error)
{
	const char *bad[] = { "abc", "7x", "-1", " 7", "", "0x10" };

	for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
		char      *argv[] = { "elc", "-b", (char *)bad[i], "a.c", NULL };
		ElcOptions o;

		cr_assert_eq(cli_parse(4, argv, &o), CLI_ERROR,
		             "'%s' is not a threshold", bad[i]);
	}
}

/* --------------------------------------------------------- custom rules -- */

/* Verifies LLR-RLR-02: the `lang:path` argument is recorded as given. The
 * parser does not split it, and deliberately: whether the named language
 * exists is the registry's question, and answering it here would put a second
 * copy of that knowledge in the option parser. */
Test(cli, a_rule_argument_is_recorded_unsplit)
{
	char      *argv[] = { "elc", "--rules", "c:house.scm", "a.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(4, argv, &o), CLI_OK);
	cr_assert_eq(o.rule_count, 1);
	cr_assert_str_eq(o.rules[0], "c:house.scm");
	cli_options_free(&o);
}

Test(cli, rule_arguments_accumulate)
{
	char      *argv[] = { "elc", "--rules", "c:one.scm",
	                      "--rules", "rust:two.scm", "a.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(6, argv, &o), CLI_OK);
	cr_assert_eq(o.rule_count, 2,
	             "the option is repeatable; a second must not replace the "
	             "first");
	cr_assert_str_eq(o.rules[0], "c:one.scm");
	cr_assert_str_eq(o.rules[1], "rust:two.scm");
	cli_options_free(&o);
}

Test(cli, no_rule_is_supplied_by_default)
{
	char      *argv[] = { "elc", "a.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(2, argv, &o), CLI_OK);
	cr_assert_eq(o.rule_count, 0,
	             "elc supplies no rule of its own beyond the catalogued "
	             "metrics and thresholds (HLR-111)");
	cli_options_free(&o);
}

/* -------------------------------------------------- conditional compilation */

/* Verifies LLR-CLI-25: a definition reaches the options as given, `NAME` and
 * `NAME=VALUE` alike. What a definition *means* belongs to the evaluation,
 * which is the only place that knows what a language's conditions can test. */
/* Verifies LLR-CLI-18, LLR-CLI-08: --entry accumulates one symbol per
   occurrence, and the symbols are borrowed from argv rather than copied. */
Test(cli, entry_points_accumulate_across_occurrences)
{
	char      *argv[] = { "elc", "--entry", "main", "--entry", "isr",
	                      "a.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(6, argv, &o), CLI_OK);
	cr_assert_eq(o.entry_point_count, 2,
	             "a second --entry adds a root rather than replacing the "
	             "first; a program with two entry points is the case the "
	             "option exists for (HLR-095)");
	cr_assert_str_eq(o.entry_points[0], "main");
	cr_assert_str_eq(o.entry_points[1], "isr");
	cr_assert_eq(o.entry_points[0], argv[2],
	             "borrowed from argv, not copied: only the array is owned");
	cli_options_free(&o);
}

/* Verifies LLR-CLI-08: with no --entry the set is empty, which is what makes
   reachability and depth omit themselves with a reason rather than measure
   from a root elc chose (HLR-095, HLR-115). */
Test(cli, no_entry_point_leaves_the_set_empty)
{
	char      *argv[] = { "elc", "a.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(2, argv, &o), CLI_OK);
	cr_assert_eq(o.entry_point_count, 0);
	cli_options_free(&o);
}

/* Verifies LLR-CLI-07: the GraphML request is a flag. The target immediately
   following it is a target and not a path the flag consumed. */
Test(cli, the_graphml_export_is_a_flag_taking_no_path)
{
	char      *argv[] = { "elc", "--graphml", "a.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(3, argv, &o), CLI_OK);
	cr_assert(o.graphml);
	cr_assert_eq(o.target_count, 1,
	             "the argument after --graphml is the target; a flag that "
	             "took a path would have swallowed it (HLR-119: the "
	             "companion is named from the report, never given)");
	cr_assert_str_eq(o.targets[0], "a.c");
	cli_options_free(&o);
}

/* Verifies LLR-HTM-01: the `.html` extension selects the format, exactly as
   every other extension selects one (HLR-148). */
Test(cli, the_html_extension_selects_the_html_format)
{
	char      *argv[] = { "elc", "-o", "report.html", "a.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(4, argv, &o), CLI_OK);
	cr_assert_eq(o.format, FORMAT_HTML);
	cli_options_free(&o);
}

/* Verifies LLR-HTM-01: there is no option requesting the format. The filename
   has already said what it is, and a flag saying it again is the disagreement
   HLR-149 exists to prevent, arriving as a third spelling (HLR-215). */
Test(cli, there_is_no_option_requesting_the_html_format)
{
	char      *argv[] = { "elc", "--html", "-o", "report.html", "a.c",
	                      NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(5, argv, &o), CLI_ERROR);
	cli_options_free(&o);
}

/* Verifies LLR-HTM-01: standard output has no extension, so the option is the
   means there — the rule every other format follows (HLR-149). */
Test(cli, the_format_option_spells_html)
{
	char      *argv[] = { "elc", "-f", "html", "a.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(4, argv, &o), CLI_OK);
	cr_assert_eq(o.format, FORMAT_HTML);
	cr_assert_null(o.output_path);
	cli_options_free(&o);
}

/* Verifies LLR-CLI-15 for the html format: a saved record carries the findings
   of a run and not the graph they came from, so there is no topology to draw.
   The request is refused rather than answered with an empty drawing (HLR-122,
   HLR-215). */
Test(cli, the_html_format_is_refused_with_from_xml)
{
	char      *argv[] = { "elc", "--from-xml", "r.xml", "-o", "r.html",
	                      NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(5, argv, &o), CLI_ERROR);
	cli_options_free(&o);
}

/* Verifies LLR-CLI-06: the call tree is generated by default, and the option
   declines it rather than requesting it (HLR-103). */
Test(cli, the_call_tree_is_on_unless_declined)
{
	char      *on[]  = { "elc", "a.c", NULL };
	char      *off[] = { "elc", "--no-dot", "a.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(2, on, &o), CLI_OK);
	cr_assert_not(o.no_dot, "default-on is the whole of HLR-103");
	cli_options_free(&o);

	cr_assert_eq(cli_parse(3, off, &o), CLI_OK);
	cr_assert(o.no_dot);
	cr_assert_eq(o.target_count, 1, "--no-dot takes no argument");
	cli_options_free(&o);
}

Test(cli, a_definition_is_recorded_as_given)
{
	char      *argv[] = { "elc", "-D", "FOO", "--define", "BAR=2",
	                      "a.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(6, argv, &o), CLI_OK);
	cr_assert_eq(o.define_count, 2);
	cr_assert_str_eq(o.defines[0], "FOO");
	cr_assert_str_eq(o.defines[1], "BAR=2");
	cli_options_free(&o);
}

Test(cli, definitions_are_empty_by_default)
{
	char      *argv[] = { "elc", "a.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(2, argv, &o), CLI_OK);
	cr_assert_eq(o.define_count, 0,
	             "with no definitions nothing prunes, which is how adding "
	             "the option changes no existing result (HLR-131)");
	cli_options_free(&o);
}

Test(cli, an_empty_definition_is_a_usage_error)
{
	char      *argv[] = { "elc", "-D", "", "a.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(4, argv, &o), CLI_ERROR);
}

/* Verifies LLR-CLI-24: pruning happens when a file is measured, so a saved
 * record already describes one configuration and cannot be re-cut into
 * another. Rejected rather than ignored, so a user who named a configuration
 * and got a different one is told. */
Test(cli, a_definition_with_regeneration_is_a_usage_error)
{
	char      *argv[] = { "elc", "--from-xml", "r.xml", "-DFOO", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(4, argv, &o), CLI_ERROR);
	/* The definition was recorded before the mode conflict was found, so
	 * the options own an allocation even on this path — which `main` also
	 * releases, a usage error having to exit as leak-clean as a success
	 * (HLR-125). */
	cli_options_free(&o);
}

Test(cli, regeneration_without_a_definition_is_accepted)
{
	char      *argv[] = { "elc", "--from-xml", "r.xml", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(3, argv, &o), CLI_OK);
	cli_options_free(&o);
}

/* ------------------------------------------------------- the linked image */

/* Verifies LLR-CLI-22: the path is recorded and not validated. Whether the
 * file is an image elc can read is the question of the module that owns the
 * failure, and answering it here would put a second copy of that knowledge in
 * the parser. */
Test(cli, an_image_path_is_recorded_unvalidated)
{
	char      *argv[] = { "elc", "--elf", "build/app.elf", "src", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(4, argv, &o), CLI_OK);
	cr_assert_str_eq(o.image_path, "build/app.elf",
	                 "the parser records the path and opens nothing");
	cli_options_free(&o);
}

Test(cli, no_image_is_the_default)
{
	char      *argv[] = { "elc", "a.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(2, argv, &o), CLI_OK);
	cr_assert_null(o.image_path,
	               "with no image nothing is filtered, which is how adding "
	               "the option changes no existing result (HLR-140)");
	cli_options_free(&o);
}

Test(cli, an_empty_image_path_is_a_usage_error)
{
	char      *argv[] = { "elc", "--elf", "", "a.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(4, argv, &o), CLI_ERROR);
}

/* Verifies LLR-CLI-23: the filter is applied when a file is measured, so a
 * saved record already describes one filtered run and cannot be re-cut against
 * another image. The same rule -D gets, and for the same reason. */
Test(cli, an_image_with_regeneration_is_a_usage_error)
{
	char      *argv[] = { "elc", "--from-xml", "r.xml", "--elf", "app.elf",
	                      NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(5, argv, &o), CLI_ERROR);
	cli_options_free(&o);
}

/* Verifies LLR-USG-07: the summary documents the option, including that no
 * image means no filtering and that the argument is an image rather than a
 * source tree. The documentation test checks the manual against this text, so
 * this is where the claim is made. */
Test(cli, the_usage_summary_documents_the_image_option)
{
	char *buffer = NULL;
	size_t size  = 0;
	FILE  *out   = open_memstream(&buffer, &size);

	cr_assert_not_null(out);
	cli_usage(out);
	fclose(out);

	cr_assert_not_null(strstr(buffer, "--elf"));
	cr_assert_not_null(strstr(buffer, "With no --elf nothing is filtered"));
	free(buffer);
}

/* ---------------------------------------- the format a filename names ------
 *
 * Verifies LLR-CLI-26 and LLR-CLI-27. The extension is resolved after the
 * option loop, so `-f` and `-o` may be given in either order and still be
 * compared against one another; both orders are driven here for that reason.
 */

Test(cli, an_output_extension_selects_the_format)
{
	static const struct { const char *path; OutputFormat format; } cases[] = {
		{ "report.txt", FORMAT_TABLE    },
		{ "report.md",  FORMAT_MARKDOWN },
		{ "report.csv", FORMAT_CSV      },
		{ "report.xml", FORMAT_XML      }
	};

	for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
		char      *argv[] = { "elc", "-o", (char *)cases[i].path, "src",
		                      NULL };
		ElcOptions o;

		cr_assert_eq(cli_parse(4, argv, &o), CLI_OK,
		             "%s names a format elc recognises",
		             cases[i].path);
		cr_assert_eq(o.format, cases[i].format,
		             "%s must select its own format with no option",
		             cases[i].path);
		cli_options_free(&o);
	}
}

Test(cli, an_unrecognised_output_extension_is_a_usage_error)
{
	char      *argv[] = { "elc", "-o", "report.json", "src", NULL };
	ElcOptions o;

	/* Defaulting to the table here would write a report.json holding no
	 * JSON, which is the confidently wrong result HLR-148 forbids. */
	cr_assert_eq(cli_parse(4, argv, &o), CLI_ERROR);
	cli_options_free(&o);
}

Test(cli, an_output_path_with_no_extension_is_a_usage_error)
{
	static const char *const paths[] = { "report", ".report", "report.",
	                                     "build.d/report" };

	for (size_t i = 0; i < sizeof paths / sizeof *paths; i++) {
		char      *argv[] = { "elc", "-o", (char *)paths[i], "src",
		                      NULL };
		ElcOptions o;

		cr_assert_eq(cli_parse(4, argv, &o), CLI_ERROR,
		             "'%s' names no format", paths[i]);
		cli_options_free(&o);
	}
}

Test(cli, a_format_option_contradicting_the_filename_is_a_usage_error)
{
	char      *before[] = { "elc", "-f", "csv", "-o", "report.md", "src",
	                        NULL };
	char      *after[]  = { "elc", "-o", "report.md", "-f", "csv", "src",
	                        NULL };
	ElcOptions o;

	/* Either order, because the two are compared after the loop rather
	 * than as they arrive (LLR-CLI-27). */
	cr_assert_eq(cli_parse(6, before, &o), CLI_ERROR);
	cli_options_free(&o);
	cr_assert_eq(cli_parse(6, after, &o), CLI_ERROR);
	cli_options_free(&o);
}

Test(cli, a_format_option_agreeing_with_the_filename_is_accepted)
{
	char      *argv[] = { "elc", "-f", "md", "-o", "report.md", "src",
	                      NULL };
	ElcOptions o;

	/* Nothing is ambiguous about saying a thing twice (HLR-149). */
	cr_assert_eq(cli_parse(6, argv, &o), CLI_OK);
	cr_assert_eq(o.format, FORMAT_MARKDOWN);
	cli_options_free(&o);
}

/* Verifies LLR-CLI-02. Worth its own case now that the format is settled after
 * the option loop rather than as `-f` arrives: the default has to survive a
 * resolution step that runs whether or not anything selected a format. */
Test(cli, the_format_defaults_to_the_table)
{
	char      *argv[] = { "elc", "src", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(2, argv, &o), CLI_OK);
	cr_assert_eq(o.format, FORMAT_TABLE);
	cli_options_free(&o);
}

Test(cli, standard_output_takes_its_format_from_the_option_alone)
{
	char      *argv[] = { "elc", "-f", "csv", "src", NULL };
	ElcOptions o;

	/* Standard output has no filename and so no extension (HLR-149). */
	cr_assert_eq(cli_parse(4, argv, &o), CLI_OK);
	cr_assert_eq(o.format, FORMAT_CSV);
	cr_assert_null(o.output_path);
	cli_options_free(&o);
}

/* Verifies LLR-CLI-28: an output path's extension is a format selection just
 * as explicit as `-f`, differently spelt, so regeneration rejects it for the
 * same reason and accepts the one that names Markdown. */
Test(cli, regenerating_into_a_filename_naming_a_table_is_a_usage_error)
{
	char      *argv[] = { "elc", "--from-xml", "r.xml", "-o", "out.txt",
	                      NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(5, argv, &o), CLI_ERROR);
	cli_options_free(&o);
}

Test(cli, regenerating_into_a_filename_naming_markdown_is_accepted)
{
	char      *argv[] = { "elc", "--from-xml", "r.xml", "-o", "out.md",
	                      NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(5, argv, &o), CLI_OK);
	cr_assert_eq(o.format, FORMAT_MARKDOWN);
	cli_options_free(&o);
}

/* ------------------------------------------------------------ verbosity ----
 *
 * Verifies LLR-CLI-29 and LLR-CLI-30.
 */

Test(cli, the_summary_is_the_default_verbosity)
{
	char      *argv[] = { "elc", "src", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(2, argv, &o), CLI_OK);
	cr_assert(!o.verbose,
	          "a zeroed configuration means the summary (HLR-150)");
	cli_options_free(&o);
}

Test(cli, verbose_is_recorded_in_either_spelling)
{
	char      *shortform[] = { "elc", "-v", "src", NULL };
	char      *longform[]  = { "elc", "--verbose", "src", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(3, shortform, &o), CLI_OK);
	cr_assert(o.verbose);
	cli_options_free(&o);
	cr_assert_eq(cli_parse(3, longform, &o), CLI_OK);
	cr_assert(o.verbose);
	cli_options_free(&o);
}

/* Verifies LLR-CLI-30. Every other option pairing this parser decides is a
 * usage error, so the analogy pulls the wrong way here: there is nothing
 * contradictory about asking a complete format for detail, and the request
 * simply has no effect (HLR-152). */
Test(cli, verbose_with_a_complete_record_format_is_accepted)
{
	char      *xml[] = { "elc", "--verbose", "-f", "xml", "src", NULL };
	char      *csv[] = { "elc", "--verbose", "-f", "csv", "src", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(5, xml, &o), CLI_OK);
	cr_assert(o.verbose);
	cr_assert_eq(o.format, FORMAT_XML);
	cli_options_free(&o);
	cr_assert_eq(cli_parse(5, csv, &o), CLI_OK);
	cr_assert(o.verbose);
	cr_assert_eq(o.format, FORMAT_CSV);
	cli_options_free(&o);
}

/* Verifies LLR-USG-09: the summary is the reference the documentation test
 * checks both delivered documents against, so the verbosity option and the
 * extension rule are claimed here. */
Test(cli, the_usage_summary_documents_verbosity_and_the_extension_rule)
{
	char  *buffer = NULL;
	size_t size   = 0;
	FILE  *out    = open_memstream(&buffer, &size);

	cr_assert_not_null(out);
	cli_usage(out);
	fclose(out);

	cr_assert_not_null(strstr(buffer, "--verbose"));
	cr_assert_not_null(strstr(buffer, "The extension of FILE names the "
	                                  "format"));
	free(buffer);
}

/* `--dsm` records a request for the CSV companion and validates nothing
 * against `--output` here (LLR-CLI-31).
 *
 * The absence of that validation is the requirement rather than an omission:
 * asking for the matrix with the report on standard output is not a usage
 * error, it simply writes no file — the rule `--graphml` already follows.
 * Rejecting it would make `elc --dsm src/` fail where HLR-104 says it should
 * quietly produce no companion.
 */
Test(cli, the_matrix_companion_is_off_unless_requested)
{
	char      *argv[] = { "elc", "a.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(2, argv, &o), CLI_OK);
	cr_assert_not(o.dsm, "a zeroed configuration means the default");
	cli_options_free(&o);
}

Test(cli, the_matrix_companion_is_requested_without_an_output_path)
{
	char      *argv[] = { "elc", "--dsm", "a.c", NULL };
	ElcOptions o;

	cr_assert_eq(cli_parse(3, argv, &o), CLI_OK,
	             "a request with no --output is not a usage error");
	cr_assert(o.dsm);
	cr_assert_null(o.output_path);
	cli_options_free(&o);
}

Test(cli, the_matrix_companion_is_accepted_with_regeneration)
{
	char      *argv[] = { "elc", "--from-xml", "rec.xml", "--dsm",
	                      "-o", "out.md", NULL };
	ElcOptions o;

	/* Unlike --graphml, which is rejected here: a saved record carries the
	 * matrix where it carries no graph, so there is something to write
	 * from (HLR-054, HLR-180). */
	cr_assert_eq(cli_parse(6, argv, &o), CLI_OK);
	cr_assert(o.dsm);
	cr_assert_eq(o.mode, MODE_REGENERATE);
	cli_options_free(&o);
}

/* ------------------------------------------------ the manifest and drawings --
 *
 * Three options with one property between them, and it is the property that
 * matters most about the manifest: `elc` reaches a manifest through `--manifest`
 * and through nothing else. A zeroed `ElcOptions` names no manifest, which is
 * HLR-039's zero-configuration guarantee expressed in the structure rather than
 * remembered in a comment.
 */

Test(cli, no_manifest_is_read_without_the_option)
{
	char      *argv[] = { "elc", "a.c", NULL };
	ElcOptions o;

	/* **Read only when named** (HLR-176). There is nowhere else for a
	 * manifest to come from: not the working directory, not the analysis
	 * target, not an ancestor of either, and no dotfile. The field is NULL
	 * and there is no code that fills it but the option below. */
	cr_assert_eq(cli_parse(2, argv, &o), CLI_OK);
	cr_assert_null(o.manifest_path);
	cr_assert_not(o.write_manifest);
	cr_assert_not(o.purify_dot);
	cli_options_free(&o);
}

Test(cli, the_manifest_path_is_taken_from_the_command_line)
{
	char      *argv[] = { "elc", "--manifest", "team.json", "a.c", NULL };
	ElcOptions o;

	/* Borrowed from argv, like the image path and for the same reason: the
	 * file is read by `purify.c`, which owns the failure, so the parser
	 * stays the module that reads argv rather than becoming one that reads
	 * files (LLR-CLI-22). */
	cr_assert_eq(cli_parse(4, argv, &o), CLI_OK);
	cr_assert_str_eq(o.manifest_path, "team.json");
	cr_assert_eq(o.manifest_path, argv[2]);
	cli_options_free(&o);
}

Test(cli, the_manifest_and_drawing_companions_are_requests)
{
	char      *argv[] = { "elc", "--write-manifest", "--purify-dot",
	                      "-o", "r.md", "a.c", NULL };
	ElcOptions o;

	/* Both are recorded rather than validated against --output here. A
	 * request made with the report on standard output writes nothing
	 * rather than failing, by the rule --graphml already follows: the
	 * companion's name is derived from the report's, and there is then no
	 * name to derive (HLR-104, HLR-119). */
	cr_assert_eq(cli_parse(6, argv, &o), CLI_OK);
	cr_assert(o.write_manifest);
	cr_assert(o.purify_dot);
	cli_options_free(&o);
}

Test(cli, the_recovery_companions_are_refused_in_regeneration_mode)
{
	char      *dot[]  = { "elc", "--from-xml", "r.xml", "--purify-dot",
	                      NULL };
	char      *write[] = { "elc", "--from-xml", "r.xml",
	                       "--write-manifest", NULL };
	char      *read[]  = { "elc", "--from-xml", "r.xml", "--manifest",
	                       "m.json", NULL };
	ElcOptions o;

	/* A record carries what a run concluded and not the graph it concluded
	 * it from, so there is nothing here to classify, to mask, or to draw.
	 * Rejected rather than silently producing nothing, so that a user who
	 * asked for a file and got none is told why (HLR-122, LLR-CLI-15). */
	cr_assert_eq(cli_parse(4, dot, &o), CLI_ERROR);
	cli_options_free(&o);
	cr_assert_eq(cli_parse(4, write, &o), CLI_ERROR);
	cli_options_free(&o);
	cr_assert_eq(cli_parse(5, read, &o), CLI_ERROR);
	cli_options_free(&o);
}
