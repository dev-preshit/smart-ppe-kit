# TC-42 run 20261017-215811_2747572

TC-42 pass rule has two clauses: (1) 0 occurrences of the REAL key in all commits, (2) firmware builds with no key in source. Clause 1 needs your key file (kept outside the repo); clause 2 needs arduino-cli with the pinned toolchain. Run: python tools/s1_run.py --key-file <path> --build --ide 2.3.10

Working tree dirty at run time: no
Host: Windows 11, Python 3.14.8
