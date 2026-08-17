#!/usr/bin/env python3
"""Demo CGI script: a deliberately slow script to show the 504 timeout."""

import sys
import time

print("Content-Type: text/plain")
print("")
print("this should never be reached", end="", flush=True)
sys.stdout.flush()
time.sleep(60)
