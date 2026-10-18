# Host suite 20261018-200608_5062d2d

All host tests with the TC each supports. Rows for TC-11, 17, 22, 23, 24, 32, 73, 75 are early host evidence for later sprints (S3, S4, S6): they are NOT passes of those TCs.

```

tag uses wrong bytes                     BUILD ERROR (counts as not killed by tests)
ct_equal compares 1 byte                 BUILD ERROR (counts as not killed by tests)
block extension not persisted            BUILD ERROR (counts as not killed by tests)
cold boot does not advance ceiling       BUILD ERROR (counts as not killed by tests)
counter limit not enforced               BUILD ERROR (counts as not killed by tests)
state checked before HMAC                BUILD ERROR (counts as not killed by tests)
version checked after HMAC (removed)     BUILD ERROR (counts as not killed by tests)
duplicate accepted                       BUILD ERROR (counts as not killed by tests)
counter big-endian                       BUILD ERROR (counts as not killed by tests)
hmac verify always true                  BUILD ERROR (counts as not killed by tests)
mutants=10 survivors=10

```
