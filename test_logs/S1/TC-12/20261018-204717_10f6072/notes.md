# TC-12 run 20261018-204717_10f6072

Expected values come from tools/gen_vectors.py (typed from spec Appendix A and cross-checked with Python hmac), never from the code under test. Production wrapper uses mbedtls (host system library) on the host; a portable SHA-256 oracle in test/host cross-checks 2000 random keys/messages. Device mbedtls (esp32 core 3.0.7) is a different build of the same API: only the S4 bench run proves it.

Working tree dirty at run time: no
Host: Windows 11, Python 3.14.8
