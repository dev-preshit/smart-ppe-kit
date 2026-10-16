# TC-78 run 20261016-144256_a954e48

Pass rule: clean build of node and hub with exactly the pinned versions. Not demonstrable in the S1 authoring sandbox: no arduino-cli, and the Espressif/Arduino download hosts are not reachable from it. tools/build_check.py is covered by fake-CLI unit tests only; its first real run is on your Windows machine. Fill tools/build_targets.txt with the spike sketch paths first.

Working tree dirty at run time: no
Host: Windows 11, Python 3.14.8
