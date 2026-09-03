#pragma once
// Host-side stand-ins for the few controller-facing symbols the model layer
// links against. Shared by every native test: PlatformIO links all of
// build_src_filter into each test binary, so a test that never touches caps.cpp
// still has to satisfy its milliseconds().
//
// A folder under test/ whose name does not begin with "test_" is compiled into
// every test, which is what makes one copy enough.

// Lines the code under test sent to the controller, most recent last.
int         stub_sent_count();
const char* stub_sent(int i);
void        stub_sent_reset();

// Drives milliseconds() so timeout behaviour is testable without sleeping.
void stub_set_millis(int ms);
