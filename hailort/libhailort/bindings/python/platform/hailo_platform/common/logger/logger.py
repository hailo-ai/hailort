#!/usr/bin/env python
__all__ = ['default_logger', 'set_default_logger_level', 'create_custom_logger', 'create_server_logger', 'SERVER_LOG_PATH']

import os
import sys
import logging

import verboselogs

SERVER_LOG_DIR = "/tmp/"
SERVER_LOG_PATH = os.path.join(SERVER_LOG_DIR, "hailo_server{}.log")


def _create_logger(level="INFO", logger_path="./pyhailort.log", fmt=None, console=True):
    """
    Creates a logging object and returns it
    """
    logger_name = "".join(os.path.basename(logger_path).split(".")[:-1])
    logger = verboselogs.VerboseLogger(logger_name)
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


def create_server_logger(port, console=False):
    global _g_logger
    if not os.path.exists(SERVER_LOG_DIR):
        os.mkdir(SERVER_LOG_DIR)
    _g_logger = _create_logger("INFO", SERVER_LOG_PATH.format(port), console=console)
    _g_logger.verbose("Created the log, pid:{}".format(os.getpid()))


def default_logger():
    global _g_logger
    if _g_logger is None:
        _g_logger = _create_logger()
    return _g_logger


def create_custom_logger(log_path, fmt=None, console=False):
    """Returns a logger to the specified path, creating it if needed."""
    return _create_logger(logger_path=log_path, fmt=fmt, console=console)


def set_default_logger_level(level):
    """
    Set log level of the default log.
    Should only be called from initialization code to avoid log level collisions.
    """
    if level == "ERROR":
        _g_logger.verbose("Logger verbosity has been decreased to {}s only ".format(level.lower()))
    else:
        _g_logger.verbose("Logger verbosity is: {}".format(level))
    _g_logger.setLevel(level)
