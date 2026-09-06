#include <unity.h>
#include <string.h>
#include <stdio.h>
#include "model/filelist.h"


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

// The exact reply shape from FluidNC v4.0.3, captured from the live Corgi on
// 2026-08-25 against $Files/ListGCode=/sd.
static const char* REPLY =
    "{\"files\":[{\"name\":\"Merged Toolpath 1.tap\",\"size\":\"82360\"},"
    "{\"name\":\"bracket.nc\",\"size\":\"14022\"},"
    "{\"name\":\"spoilboard surface.gcode\",\"size\":\"5310\"}],\"path\":\"/sd\"}";

void test_first_entry_is_not_duplicated() {
    filelist_feed_line(REPLY);
    TEST_ASSERT_EQUAL(3, filelist_count());
    TEST_ASSERT_EQUAL_STRING("Merged Toolpath 1.tap", filelist_at(0)->name);
    TEST_ASSERT_EQUAL_STRING("bracket.nc",            filelist_at(1)->name);
    TEST_ASSERT_EQUAL_STRING("spoilboard surface.gcode", filelist_at(2)->name);
}

void test_sizes_and_names_survive_spaces() {
    filelist_feed_line(REPLY);
    TEST_ASSERT_EQUAL_UINT32(82360, filelist_at(0)->size);
    TEST_ASSERT_EQUAL_UINT32(14022, filelist_at(1)->size);
    TEST_ASSERT_FALSE(filelist_at(0)->is_dir);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_first_entry_is_not_duplicated);
    RUN_TEST(test_sizes_and_names_survive_spaces);
    RUN_TEST(test_document_split_mid_object);
    RUN_TEST(test_outer_object_is_not_a_file);
    RUN_TEST(test_empty_listing);
    RUN_TEST(test_directory_flagged);
    RUN_TEST(test_unrelated_lines_ignored);
    return UNITY_END();
}
