from __future__ import print_function
from builtins import object
import threading


class Watchdog(object):
    def __init__(self, queues_to_close, timeout=20):
        self.timeout = timeout
        self._queues_to_close = queues_to_close
        self._t = None

    def do_expire(self):
        for queue_to_close in self._queues_to_close:
            queue_to_close.close()

    def _expire(self):
        print("\nWatchdog expire")
        self.do_expire()

    def start(self):
        if self.timeout is not None:
            self._t = threading.Timer(self.timeout, self._expire)
            self._t.start()
        else:
            self._t = None

    def stop(self):
        if self._t is not None:
            self._t.cancel()

    def refresh(self):
        self.stop()
        self.start()
