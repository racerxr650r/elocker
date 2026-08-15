"""categories.py — one instance of each ELOC category, and one of each
exclusion. Hand-counted in README.md beside this file.
"""
import sys

LIMIT = 3


def categories(n):
    total = 0

    for i in range(LIMIT):
        if i == n:
            total += i
        elif i > n:
            break
        else:
            continue

    while total > LIMIT:
        total -= 1

    with open("/dev/null") as handle:
        handle.write("")

    try:
        assert total >= 0
        if total == 0:
            raise ValueError(total)
    except ValueError:
        total = 0
    finally:
        del n

    match total:
        case 0:
            total = 1

    print(total, file=sys.stderr)
    return total


class Unused:
    pass
