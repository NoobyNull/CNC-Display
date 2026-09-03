#include <unity.h>
#include <string.h>
#include "model/filelist.h"

void setUp() { filelist_reset(); }
void tearDown() {}

// The exact reply shape from FluidNC v4.0.3, captured from the live Corgi on
// 2026-08-25 against $Files/ListGCode=/sd.
static const char* REPLY =
    "{\"files\":[{\"name\":\"Merged Toolpath 1.tap\",\"size\":\"82360\"},"
    "{\"name\":\"bracket.nc\",\"size\":\"14022\"},"
    "{\"name\":\"spoilboard surface.gcode\",\"size\":\"5310\"}],\"path\":\"/sd\"}";

// The bug this pins: the scan started at the first '{', which is the OUTER
// object. Its first "name" is entry zero's, so every listing showed its first
// file twice — visible on the Run page as a duplicated row.
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

// Controller flash holds no G-code, so this is the honest reply for /localfs —
// and it must produce an empty list, not a phantom entry from the outer object.
void test_empty_listing_yields_no_entries() {
    filelist_feed_line("{\"files\":[],\"path\":\"/localfs\"}");
    TEST_ASSERT_EQUAL(0, filelist_count());
}

void test_directories_are_flagged() {
    filelist_feed_line("{\"files\":[{\"name\":\"jobs\",\"size\":\"-1\"},"
                       "{\"name\":\"a.nc\",\"size\":\"10\"}],\"path\":\"/sd\"}");
    TEST_ASSERT_EQUAL(2, filelist_count());
    TEST_ASSERT_TRUE(filelist_at(0)->is_dir);
    TEST_ASSERT_FALSE(filelist_at(1)->is_dir);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_first_entry_is_not_duplicated);
    RUN_TEST(test_sizes_and_names_survive_spaces);
    RUN_TEST(test_empty_listing_yields_no_entries);
    RUN_TEST(test_directories_are_flagged);
    return UNITY_END();
}
