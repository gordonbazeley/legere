#!/usr/bin/env python3
"""Turn captured `pebble logs` output into the shake-log CSV.

Stopgap for the official Pebble app not offering a Settings webview for
sideloaded apps yet (see watchpebble.c). The watch already logs one
finished-hour row per hour in the exact CSV shape; this just pulls those
lines out of a log capture.

Usage:
    pebble logs --phone <ip> | tee watch.log     # leave running for a day+
    python3 tools/pebble-log-to-csv.py watch.log > shake-log.csv
"""
import re
import sys

ROW = re.compile(r'row (\d{2}/\d{2}/\d{4},\d{2},(?:yes|no),\d+)')


def main(path):
    print('date,hour,quiet_hour,shakes')
    with open(path) as f:
        for line in f:
            m = ROW.search(line)
            if m:
                print(m.group(1))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else '/dev/stdin')
