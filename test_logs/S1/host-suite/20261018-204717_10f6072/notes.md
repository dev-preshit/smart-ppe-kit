# Host suite 20261018-204717_10f6072

All host tests with the TC each supports. Rows for TC-11, 17, 22, 23, 24, 32, 73, 75 are early host evidence for later sprints (S3, S4, S6): they are NOT passes of those TCs.

```
SUMMARY	tests=30	failed=0	checks=23426
tag uses wrong bytes                     KILLED (6 failing tests)
ct_equal compares 1 byte                 KILLED (5 failing tests)
block extension not persisted            KILLED (6 failing tests)
cold boot does not advance ceiling       KILLED (3 failing tests)
counter limit not enforced               KILLED (2 failing tests)
state checked before HMAC                KILLED (2 failing tests)
version checked after HMAC (removed)     KILLED (3 failing tests)
duplicate accepted                       KILLED (2 failing tests)
counter big-endian                       KILLED (3 failing tests)
hmac verify always true                  KILLED (4 failing tests)
mutants=10 survivors=0

```
