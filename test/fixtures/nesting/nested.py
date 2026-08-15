"""nested.py — statements and decision points attributed to the innermost
*named* function. Hand-counted in README.md beside this file.
"""


def outer(seed):
    total = seed

    def inner(x):
        if x > 0:
            return x * 2
        return 0

    double = lambda x: x * 2 if x > 0 else 0

    if total > 0 and total < 100:
        total = inner(total) + double(total)
    return total
