# A language whose module supplies no conditionals.scm.
#
# Python has no conditional compilation, and the contract says so by omission
# rather than by an empty file: the required six are unchanged, and a module
# without the optional seventh simply has none.


def only_function():
    return 1
