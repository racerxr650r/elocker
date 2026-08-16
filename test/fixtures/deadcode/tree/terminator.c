/* terminator.c — the sibling walk, and the label that must survive it.
 *
 * Hand-counted; see ../README.md. Line numbers are load-bearing.
 */

int after_return(void)          /* 6  */
{                               /* 7  */
	return 0;               /* 8  — the terminator */
	int n = 1;              /* 9  — dead */
	n++;                    /* 10 — dead */
done:                           /* 11 — LIVE: a goto can land here */
	return 3;               /* 12 — live, and reached only by that goto */
}                               /* 13 */

int after_break(int c)          /* 15 */
{                               /* 16 */
	while (c) {             /* 17 */
		break;          /* 18 — the terminator */
		c--;            /* 19 — dead */
	}                       /* 20 */
	return c;               /* 21 — live: a different block */
}                               /* 22 */

int after_continue(int c)       /* 24 */
{                               /* 25 */
	while (c) {             /* 26 */
		continue;       /* 27 — the terminator */
		c--;            /* 28 — dead */
	}                       /* 29 */
	return c;               /* 30 — live */
}                               /* 31 */

int switch_arms(int c)          /* 33 */
{                               /* 34 */
	switch (c) {            /* 35 */
	case 1:                 /* 36 */
		return 1;       /* 37 — terminator, but its arm contains it */
	case 2:                 /* 38 — LIVE: the next arm is not a sibling */
		return 2;       /* 39 — live */
	}                       /* 40 */
	return 0;               /* 41 — live */
}                               /* 42 */

int two_terminators(void)       /* 44 */
{                               /* 45 */
	return 1;               /* 46 */
	return 2;               /* 47 — dead, and itself a terminator */
	int n = 3;              /* 48 — dead, reached from both */
	return n;               /* 49 — dead */
}                               /* 50 */
