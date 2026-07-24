#ifndef LITEGO_GO_FIXTURES_H
#define LITEGO_GO_FIXTURES_H

// Rules fixtures shared by the on-device Serial `selftest` and the host
// harness in ../test, so both run the identical checks. Pure C++ for the same
// reason as GoBoard: no Arduino.h, no String, no Print.
#include <stdint.h>

namespace litego {

// Sink for one line of test output. The firmware forwards to Serial, the host
// harness forwards to stdout.
typedef void (*EmitLine)(void *context, const char *line);

struct TestReport {
  EmitLine emit;
  void *context;
  uint16_t passed;
  uint16_t failed;

  void line(const char *text) const {
    if (emit != nullptr) {
      emit(context, text);
    }
  }
};

// Runs every rules fixture, emitting one PASS/FAIL line each plus a summary.
// Returns true when all of them pass.
bool runRulesFixtures(TestReport &report);

}  // namespace litego

#endif
