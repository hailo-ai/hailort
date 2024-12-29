#!/usr/bin/env python
__all__ = ['default_logger', ]

import os
import sys
import logging


def _create_logger(level="INFO", logger_path="./pyhailort.log", fmt=None, console=True):
    """
    Creates a logging object and returns it
    """
    logger_name = "".join(os.path.basename(logger_path).split(".")[:-1])
    logger = logging.getLogger(logger_name)
    logger.setLevel(level)

    # create the logging file handler
    fh = logging.FileHandler(logger_path)
    if fmt is None:
        fmt = "%(asctime)s - %(levelname)s - %(filename)s:%(lineno)s - %(message)s"
    formatter = logging.Formatter(fmt)
    fh.setFormatter(formatter)
    if console:
        console_fh = logging.StreamHandler(sys.stdout)
        logger.addHandler(console_fh)
    logger.addHandler(fh)
    return logger


_g_logger = None


def default_logger():
    global _g_logger
    if _g_logger is None:
        _g_logger = _create_logger()
    return _g_logger
