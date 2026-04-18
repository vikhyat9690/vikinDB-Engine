#include "../include/page.hpp"
#include "../include/disk_manager.hpp"
#include "../include/buffer_pool_manager.hpp"

#include <iostream>
#include <cassert>
#include <cstring>
#include <filesystem>

// ─── tiny test harness ────────────────────────────────────────────────────────
static int passed = 0, failed = 0;

#define TEST(name)   std::cout << "\n[ TEST ] " << name << "\n"
#define OK(cond, msg) \
    do { \
        if (cond) { std::cout << "  ✓  " << msg << "\n"; ++passed; } \
        else      { std::cout << "  ✗  " << msg << "  ← FAILED\n"; ++failed; } \
    } while(0)

// ─── helpers ──────────────────────────────────────────────────────────────────
static const std::string DB_FILE = "/tmp/test_mydb.db";

void cleanup() {
    std::filesystem::remove(DB_FILE);
}

// ─────────────────────────────────────────────────────────────────────────────
//  1.  Page struct tests
// ─────────────────────────────────────────────────────────────────────────────
void test_page_layout() {
    TEST("Page struct size is exactly PAGE_SIZE");
    OK(sizeof(Page) == PAGE_SIZE,
       "sizeof(Page) == " + std::to_string(sizeof(Page)) + " (expected 16384)");

    TEST("Page init");
    Page p;
    p.init(7, PageType::DATA);
    OK(p.header.page_id   == 7,               "page_id set to 7");
    OK(p.header.page_type == PageType::DATA,   "page_type DATA");
    OK(p.header.num_slots == 0,                "num_slots starts at 0");
    OK(p.free_space()     == PAGE_DATA_SIZE,   "full data region is free"); 
}

void test_page_records() {
    TEST("Insert and retrieve records");
    Page p;
    p.init(0);

    // Insert record 1
    const char* r1     = "Hello, Storage!";
    uint16_t    r1_len = static_cast<uint16_t>(std::strlen(r1) + 1);
    int slot1 = p.insert_record(reinterpret_cast<const uint8_t*>(r1), r1_len);
    OK(slot1 == 0, "first insert returns slot 0");

    // Insert record 2
    const char* r2     = "Page internals are fun.";
    uint16_t    r2_len = static_cast<uint16_t>(std::strlen(r2) + 1);
    int slot2 = p.insert_record(reinterpret_cast<const uint8_t*>(r2), r2_len);
    OK(slot2 == 1, "second insert returns slot 1");

    OK(p.header.num_slots == 2, "num_slots == 2 after two inserts");

    uint16_t out_len = 0;
    const uint8_t* got1 = p.get_record(0, out_len);
    OK(got1 != nullptr,                           "get_record(0) not null");
    OK(std::strcmp(reinterpret_cast<const char*>(got1), r1) == 0,
       "get_record(0) content matches");

    const uint8_t* got2 = p.get_record(1, out_len);
    OK(std::strcmp(reinterpret_cast<const char*>(got2), r2) == 0,
       "get_record(1) content matches");

    TEST("Delete a record");
    OK(p.delete_record(0),                        "delete_record(0) returns true");
    const uint8_t* after_del = p.get_record(0, out_len);
    OK(after_del == nullptr,                       "deleted slot returns nullptr");

    TEST("Free space tracking");
    uint16_t used = r1_len + r2_len + 2 * sizeof(Slot);
    uint16_t expected_free = PAGE_DATA_SIZE - r1_len - r2_len - 2 * sizeof(Slot);
    OK(p.free_space() == expected_free,
       "free_space() == " + std::to_string(p.free_space()) +
       " (expected " + std::to_string(expected_free) + ")");
}

// ─────────────────────────────────────────────────────────────────────────────
//  2.  DiskManager tests
// ─────────────────────────────────────────────────────────────────────────────
void test_disk_manager() {
    cleanup();

    TEST("DiskManager: create new file");
    DiskManager dm(DB_FILE);
    OK(dm.num_pages() == 0, "new DB file has 0 pages");

    TEST("DiskManager: allocate and write a page");
    Page write_page;
    uint32_t pid = dm.allocate_page(&write_page, PageType::DATA);
    OK(pid == 0, "first allocated page has id 0");

    const char* payload = "Disk persistence test";
    uint16_t plen = static_cast<uint16_t>(std::strlen(payload) + 1);
    write_page.insert_record(reinterpret_cast<const uint8_t*>(payload), plen);
    dm.write_page(pid, &write_page);
    OK(dm.num_pages() == 1, "num_pages == 1 after allocation");

    TEST("DiskManager: read back the page");
    Page read_page;
    dm.read_page(pid, &read_page);
    OK(read_page.header.page_id == 0, "page_id reads back as 0");

    uint16_t rlen = 0;
    const uint8_t* rec = read_page.get_record(0, rlen);
    OK(rec != nullptr, "record is present after read-back");
    OK(std::strcmp(reinterpret_cast<const char*>(rec), payload) == 0,
       "record content survives disk round-trip");

    TEST("DiskManager: allocate multiple pages");
    Page p2, p3;
    uint32_t pid2 = dm.allocate_page(&p2);
    uint32_t pid3 = dm.allocate_page(&p3);
    OK(pid2 == 1, "second page_id == 1");
    OK(pid3 == 2, "third  page_id == 2");
    OK(dm.num_pages() == 3, "num_pages == 3");

    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────────
//  3.  LRU Replacer tests
// ─────────────────────────────────────────────────────────────────────────────
void test_lru_replacer() {
    TEST("LRU Replacer: basic eviction order");
    LRUReplacer lru(5);

    lru.unpin(1);
    lru.unpin(2);
    lru.unpin(3);
    OK(lru.evictable_count() == 3, "3 evictable frames");

    frame_id_t v = lru.evict();
    OK(v == 1, "first evicted = 1 (LRU)");
    v = lru.evict();
    OK(v == 2, "second evicted = 2");

    TEST("LRU Replacer: pin removes from evictable set");
    lru.unpin(4);
    lru.unpin(5);
    lru.pin(3);   // 3 was evictable — pin it back
    // evictable now: 4, 5  (3 was re-pinned, already removed)
    OK(lru.evictable_count() == 2, "2 evictable after pin(3)");

    v = lru.evict();
    OK(v == 4, "next eviction = 4 (3 was pinned)");
}

// ─────────────────────────────────────────────────────────────────────────────
//  4.  BufferPoolManager tests
// ─────────────────────────────────────────────────────────────────────────────
void test_buffer_pool_manager() {
    cleanup();

    DiskManager          dm(DB_FILE);
    BufferPoolManager    bpm(4, dm);   // tiny 4-frame pool for testing

    TEST("BPM: new_page allocates a page");
    uint32_t pid0;
    Page* p0 = bpm.new_page(&pid0);
    OK(p0 != nullptr,  "new_page returns non-null");
    OK(pid0 == 0,      "first page_id == 0");
    OK(bpm.cached_pages() == 1, "1 page cached");

    TEST("BPM: write to page, unpin, fetch back");
    const char* msg = "Buffer pool round-trip";
    uint16_t mlen   = static_cast<uint16_t>(std::strlen(msg) + 1);
    p0->insert_record(reinterpret_cast<const uint8_t*>(msg), mlen);
    bpm.unpin_page(pid0, /*dirty=*/true);

    Page* p0b = bpm.fetch_page(pid0);
    OK(p0b != nullptr, "fetch_page returns non-null");
    uint16_t rlen = 0;
    const uint8_t* rec = p0b->get_record(0, rlen);
    OK(rec != nullptr, "record found after fetch");
    OK(std::strcmp(reinterpret_cast<const char*>(rec), msg) == 0,
       "record content correct after BPM round-trip");
    bpm.unpin_page(pid0, false);

    TEST("BPM: LRU eviction when pool is full");
    uint32_t pids[4];
    Page* pages[4];
    // Fill the pool (pid0 already there but unpinned)
    for (int i = 0; i < 3; ++i) {
        pages[i] = bpm.new_page(&pids[i]);
        OK(pages[i] != nullptr, "new_page #" + std::to_string(i+1) + " succeeds");
    }
    // All 4 frames occupied.  Unpin them so eviction can happen.
    for (int i = 0; i < 3; ++i) bpm.unpin_page(pids[i], false);

    // Requesting one more page must evict LRU (pid0 was unpinned first).
    Page* extra = bpm.new_page(&pids[3]);
    OK(extra != nullptr, "5th new_page succeeds (evicted LRU frame)");
    bpm.unpin_page(pids[3], false);

    TEST("BPM: flush_all writes dirty pages");
    bpm.flush_all();
    OK(true, "flush_all completed without exception");

    TEST("BPM: stats output");
    std::cout << "  " << bpm.stats() << "\n";
    OK(true, "stats() returned a string");

    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────────
//  main
// ─────────────────────────────────────────────────────────────────────────────
int main() {
    std::cout << "══════════════════════════════════════════\n";
    std::cout << "  MyDB Phase 1 – Storage Layer Test Suite\n";
    std::cout << "══════════════════════════════════════════\n";

    test_page_layout();
    test_page_records();
    test_disk_manager();
    test_lru_replacer();
    test_buffer_pool_manager();

    std::cout << "\n══════════════════════════════════════════\n";
    std::cout << "  Results:  " << passed << " passed,  " << failed << " failed\n";
    std::cout << "══════════════════════════════════════════\n";

    return failed == 0 ? 0 : 1;
}