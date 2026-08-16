/* literal.c — branches a written literal excludes, and the ones it does not.
 *
 * Hand-counted; see ../README.md. Line numbers are load-bearing.
 */

int excluded(void)              /* 6  */
{                               /* 7  */
	if (0) {                /* 8  */
		return 1;       /* 9  */
	}                       /* 10 — the consequence spans 8-10 */
	if (1) {                /* 11 */
		return 2;       /* 12 */
	} else {                /* 13 — the alternative spans 13-15 */
		return 3;       /* 14 */
	}                       /* 15 */
}                               /* 16 */

int loops(int c)                /* 18 */
{                               /* 19 */
	while (0) {             /* 20 */
		c++;            /* 21 */
	}                       /* 22 — the body spans 20-22 */
	do {                    /* 23 — LIVE: a do-while body runs once */
		c++;            /* 24 */
	} while (0);            /* 25 */
	return c;               /* 26 */
}                               /* 27 */

int needs_data_flow(void)       /* 29 */
{                               /* 30 */
	int x = 0;              /* 31 */
	if (x) {                /* 32 — LIVE: deciding this needs data flow */
		return 1;       /* 33 */
	}                       /* 34 */
	const int zero = 0;     /* 35 */
	if (zero) {             /* 36 — LIVE: same, however evident */
		return 2;       /* 37 */
	}                       /* 38 */
	if (0x0) {              /* 39 — LIVE: not a decimal zero, so undecided */
		return 3;       /* 40 */
	}                       /* 41 */
	return 0;               /* 42 */
}                               /* 43 */
