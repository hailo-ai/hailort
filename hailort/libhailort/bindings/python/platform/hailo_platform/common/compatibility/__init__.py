#!/usr/bin/env python
import six
from io import IOBase

# Based on: https://portingguide.readthedocs.io/en/latest/builtins.html#removed-file
try:
    # Python 2
    file_types = (file, IOBase,)
except NameError:
    # "file" isn't a built-in type in Python 3
    file_types = (IOBase,)

# Exporting types and functions from six
string_types = six.string_types
integer_types = six.integer_types
class_types = six.class_types
text_type = six.text_type
binary_type = six.binary_type

def ensure_binary(s, encoding='utf-8', errors='strict'):
    return six.ensure_binary(s, encoding=encoding, errors=errors)

def ensure_str(s, encoding='utf-8', errors='strict'):
    return six.ensure_str(s, encoding=encoding, errors=errors)

def ensure_text(s, encoding='utf-8', errors='strict'):
    return six.ensure_text(s, encoding=encoding, errors=errors)