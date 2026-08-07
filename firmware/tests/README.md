# Host unit tests

This directory contains the host-test configuration for Ceedling 1.1.3 with
Unity 2.7.1 and CMock 2.7.0. `Gemfile.lock` pins the Ruby dependencies, while
production firmware continues to use the CMake configuration from the parent
directory.

Install the pinned Ruby dependencies once:

```powershell
bundle install
```

Then run all host tests from this directory:

```powershell
.\run_tests.cmd test:all
```

Generated files are written to `tests/build/` and are ignored by Git.

Test-only replacements for Pico SDK headers live in `test/support/`. They
must expose only the platform functionality required by the module under
test; production code is always compiled against the real Pico SDK.
