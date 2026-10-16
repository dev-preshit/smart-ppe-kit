# S1 handover: shared core

## Repo layout (proposed, as built)

    common/                     Arduino library "ppe_common" (library.properties + src/). Link it, see common/README.md
      src/ppe_proto.h/.cpp        wire format (the one copy), encode/decode
      src/ppe_crypto.h/.cpp       HMAC-SHA256 wrapper (mbedtls), constant-time compare
      src/ppe_filter.h/.cpp       hub receive filter steps 1-6, counter rule step 7
      src/ppe_counter.h/.cpp      node counter + reserved block behind PpeCounterStore
      src/ppe_log.h/.cpp          logging macros
      src/ppe_test_key.h          public Appendix A key, test builds only
      src/secrets.example.h       placeholder only
    test/host/                  Makefile (make test), mini harness, portable SHA-256 oracle, tests
    tools/                      gen_vectors.py (SC-03), verify_packets.py (SC-02), secret_scan.sh/.py (SC-10),
                                check_traceability.py (SC-09), schema/uplink.schema.json (SC-14), build_check.py,
                                s1_run.py (writes test_logs), mutation_check.py, check_env.py, build_targets.txt, tests/
    test_logs/                  S1/<TC>/<run>/ and summary.csv in the test-plan format
    docs/bugs/                  BUGLOG.md, BUG-001.md
    .github/workflows/ci.yml    host job, secrets job, manual firmware-build job (TC-78)

Not built (see conflict report C-05): SC-01 `provision.py`, SC-06 `logger.py`.

## Apply to your repository

The tree is new files only, so it does not replace any of your documents or code.

1. `git switch develop && git pull && git switch -c feature/s1-shared-core`
2. Copy `common/`, `test/`, `tools/`, `test_logs/`, `.github/`, and the new files in `docs/` (`bugs/`, `s1_*.md`) into the repo root.
   Do **not** replace your `.gitignore`: append `.gitignore.s1-additions` to it (`type .gitignore.s1-additions >> .gitignore` in cmd, or open and paste).
3. Delete the two spike copies of `ppe_proto.h` (in `node_test` and `hub_test`), change their includes to `#include <ppe_common.h>`,
   and link `common/` (see `common/README.md`).
4. Fill `tools/build_targets.txt` with the spike sketch paths.
5. Commit in the story order below (or one commit per story).

Suggested commits: (1) `ppe_proto` (2) `ppe_crypto` (3) `ppe_filter` (4) `ppe_counter` (5) key handling (6) uplink schema
(7) `ppe_log` (8) host tests and tools (9) CI and docs. The sandbox branch has them in this order. Crypto comes before the filter because the filter calls it.

## Run it on Windows

    python tools\check_env.py                                   (what is installed, what each gap blocks)
    make -C test/host test                                       (MSYS2 UCRT64 shell, or WSL)
    python -m unittest discover -s tools/tests -p "test_*.py"
    python tools\s1_run.py --key-file <real key file> --build --ide 2.3.10 --tester "<your name>" --docs docs --mutation

MSYS2 UCRT64 packages: `pacman -S make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-mbedtls python`. The real key file stays outside the repo.
`s1_run.py` is the only thing that writes `test_logs/`; with the key file and `--build` TC-42 and TC-78 can reach PASS.
The log's `sha` is the commit under test, so commit the code first, run, then commit `test_logs/`.

## Evidence summary and limits

- Authoring sandbox: Linux, g++ 13.3, Python 3.12, mbedtls 2.28.8. No arduino-cli, no ESP toolchain, no hardware.
- Nothing here has been compiled for the ESP32 or run on a device. The library compiles with `-Wall -Wextra -Wpedantic -Werror` on the host only.
- CI is syntax-checked (YAML parses) but has not run on GitHub. Actions are pinned by tag, not by commit SHA.
