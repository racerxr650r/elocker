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
#include <stdlib.h>

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
