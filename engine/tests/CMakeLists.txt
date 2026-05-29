# Test framework choice TBD (GoogleTest or Catch2). For now, a smoke test
# that just runs the CLI and checks the exit code.

add_test(NAME cli_runs COMMAND equitycalc --version)
set_tests_properties(cli_runs PROPERTIES
    PASS_REGULAR_EXPRESSION "equitycalc"
)
