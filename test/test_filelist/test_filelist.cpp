#include <unity.h>
#include <string.h>
#include <stdio.h>
#include "model/filelist.h"

// filelist.cpp calls send_linef() to issue the request; the reply path is what
// is under test.
void send_linef(const char* /*fmt*/, ...) {}
#include "../../src/model/filelist.cpp"

void setUp() { filelist_reset(); }
void tearDown() {}

// The reply as FluidNC actually sends it: split across lines, with the break
// landing INSIDE an object. Captured from a live v4 controller.
void test_document_split_mid_object() {
    filelist_feed_line("{\"files\":[{\"name\":\"bracket-roughing.nc\",\"size\":\"51\"},"
                       "{\"name\":\"bracket-finish.nc\",\"size\":\"55\"},{\"name");
    filelist_feed_line("\":\"surfacing-pass.nc\",\"size\":\"60\"}],\"path\":\"/sd\"}");

    TEST_ASSERT_EQUAL_size_t(3, filelist_count());
    TEST_ASSERT_EQUAL_STRING("bracket-roughing.nc", filelist_at(0)->name);
    TEST_ASSERT_EQUAL_STRING("bracket-finish.nc",   filelist_at(1)->name);
    TEST_ASSERT_EQUAL_STRING("surfacing-pass.nc",   filelist_at(2)->name);
    TEST_ASSERT_EQUAL_UINT32(51, filelist_at(0)->size);
}

// The outer {"files":[ object used to match the first entry's "name" through an
// unbounded strstr, so every listing duplicated its first file.
void test_outer_object_is_not_a_file() {
    filelist_feed_line("{\"files\":[{\"name\":\"a.nc\",\"size\":\"10\"}],\"path\":\"/sd\"}");
    TEST_ASSERT_EQUAL_size_t(1, filelist_count());
    TEST_ASSERT_EQUAL_STRING("a.nc", filelist_at(0)->name);
}

void test_empty_listing() {
    filelist_feed_line("{\"files\":[],\"path\":\"/sd\"}");
    TEST_ASSERT_EQUAL_size_t(0, filelist_count());
}

// Directories come back with a negative size.
void test_directory_flagged() {
    filelist_feed_line("{\"files\":[{\"name\":\"macros\",\"size\":\"-1\"}],\"path\":\"/sd\"}");
    TEST_ASSERT_EQUAL_size_t(1, filelist_count());
    TEST_ASSERT_TRUE(filelist_at(0)->is_dir);
}

// A status report arriving mid-listing must not be mistaken for content.
void test_unrelated_lines_ignored() {
    filelist_feed_line("<Idle|MPos:0.000,0.000,0.000>");
    filelist_feed_line("ok");
    TEST_ASSERT_EQUAL_size_t(0, filelist_count());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_document_split_mid_object);
    RUN_TEST(test_outer_object_is_not_a_file);
    RUN_TEST(test_empty_listing);
    RUN_TEST(test_directory_flagged);
    RUN_TEST(test_unrelated_lines_ignored);
    return UNITY_END();
}
