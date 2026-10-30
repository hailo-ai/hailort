# RT10 — ready for manual contribution review

Only the two-file status-latch patch belongs to the proposed source commit.
The fixture is an accompanying portable reproducer, not an integration into
the unavailable upstream test suite. It simulates dependencies and is not a
genuine SDK object or hardware-integration test. No private kernel report,
proprietary documentation, runtime binary or remote access data is included.

Requirements: Python 3, g++ with C++14, ASan/UBSan, original HailoRT 4.24.0 headers
and source. Create a new fixed checkout, apply the source change,
then run:

```bash
python3 -B reproduce.py --baseline /path/to/original-hailort \
  --fixed /path/to/rt10-hailort --output /path/to/new-results-directory
```

The output directory must not exist. Expected baseline/fixed results:
minimal 0/2 -> 2/2, core 4/254 -> 254/254, extra 1/17 -> 17/17,
review 7/11 -> 11/11. The review group covers the extracted public shutdown
and destructor, all declared status values, and atomic publication before
joining the shutdown owner. It does not destroy an object used by another thread. These groups overlap
in intent and must not be summed into a coverage claim. Individual red baseline
runs retain exit 1; the runner accepts only the recorded expected differences.

The public fixture deliberately excludes separate wider-lifecycle tests and
findings. Those remain in the internal dossier. Official native test adaptation,
maintainer acceptance of the busy return and supported toolchains remain review
items. No automatic publication or new firmware semantics are proposed.
