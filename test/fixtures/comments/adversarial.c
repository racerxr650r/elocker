/* adversarial.c — comment syntax that defeats textual matching.
 * Hand-counted in README.md beside this file.
 */
const char *block_opener_in_string(void)
{
	return "/* this opens nothing */";
}

const char *line_opener_in_string(void)
{
	return "// nor does this";
}

int quote_in_comment(void)
{
	/* a comment containing " an unbalanced quote
	   and // inline syntax
	   and /* what looks like a nested opener
	*/
	int n = 1;      /* trailing: the line still counts */
	// a whole-line comment
	return n;
}
