#pragma once
#include "page.hpp"

#include <string>
#include <fstream>
#include <stdexcept>
#include <cstdint>
#include <mutex>

using namespace std;

/**
 * Disk Manager
 * 
 * Responsibilities:
 * 
 * - Open/create a db file on disk.
 * - Read a page from disk into a caller-supplied Page buffer.
 * - Write a Page buffer back to its position in the file.
 * - Allocate new pages by extending the file.
 */

class DiskManager {
public:
    
    // Constructor / Destructor

    explicit DiskManager(const string& db_file_path)
        : file_path_(db_file_path), num_pages_(0)
    {
        // Open in read-write binary mode; create if not present.
        file_.open(db_file_path,
        ios::in | ios::out | ios::binary);

        if(!file_.is_open()) {
            //File doesn't exist yet - create it.
            file_.open(db_file_path, 
            ios::in | ios::out | ios::binary | ios::trunc);
        }

        if(!file_.is_open()) {
            throw runtime_error("DiskManager: cannot open file: " + db_file_path);
        }

        // Determined here how many pages already exists.
        file_.seekg(0, ios::end);
        streamsize file_size = file_.tellg();
        num_pages_ = static_cast<uint32_t>(file_size / PAGE_SIZE);
    }

    ~DiskManager() {
        if(file_.is_open()){
            file_.flush();
            file_.close();
        }
    }

    DiskManager(const DiskManager&) = delete;
    DiskManager& operator = (const DiskManager&) = delete;

    /**
     * Core I/O
    **/

    /** 
     * Read `page_id` from the disk into `*page`.
     * Throw runtime_error if page_id is out of range or I/O fails.
    **/
    void read_page(uint32_t page_id, Page* page) {
        lock_guard<mutex> lk(mutex_);

        if(page_id >= num_pages_) {
            throw runtime_error(
                "DiskManager::read_page - page_id " +
                to_string(page_id) + " out of range (num_pages =" +
                to_string(num_pages_) + ")"
            );
        }

        streamoff offset = static_cast<streamoff>(page_id) * PAGE_SIZE;
        file_.seekg(offset, ios::beg);

        if(!file_.read(reinterpret_cast<char*>(page), PAGE_SIZE)) {
            throw runtime_error(
                "Disk Manager::read_page - I/O error reading page " +
                to_string(page_id)
            );
        }
    }

    /**
     * Write `*page` to its position on disk (page->header.page_id must be set).
     * The file is extended automatically if the page is new.
     */
    void write_page(uint32_t page_id, const Page* page) {
        std::lock_guard<std::mutex> lk(mutex_);

        std::streamoff offset = static_cast<std::streamoff>(page_id) * PAGE_SIZE;
        file_.seekp(offset, std::ios::beg);

        if (!file_.write(reinterpret_cast<const char*>(page), PAGE_SIZE)) {
            throw std::runtime_error(
                "DiskManager::write_page – I/O error writing page " +
                std::to_string(page_id));
        }
        file_.flush();   // ensure durability (will be replaced by WAL in Phase 5)

        if (page_id >= num_pages_) {
            num_pages_ = page_id + 1;
        }
    }

    /**
     * Allocate a brand-new page: extend the file by one PAGE_SIZE block,
     * initialise the Page struct, and return the new page_id.
     *
     * The caller must eventually call write_page() to persist the page.
     */
    uint32_t allocate_page(Page* page, PageType type = PageType::DATA) {
        std::lock_guard<std::mutex> lk(mutex_);

        uint32_t new_id = num_pages_;
        page->init(new_id, type);

        // Extend the file by writing an empty page.
        std::streamoff offset = static_cast<std::streamoff>(new_id) * PAGE_SIZE;
        file_.seekp(offset, std::ios::beg);
        file_.write(reinterpret_cast<const char*>(page), PAGE_SIZE);
        file_.flush();

        num_pages_++;
        return new_id;
    }

    // ── Metadata ──────────────────────────────────────────────────────────────

    uint32_t    num_pages()  const { return num_pages_; }
    std::string file_path()  const { return file_path_; }

    /** Force all OS buffers to disk (fsync equivalent via close+reopen trick). */
    void sync() {
        std::lock_guard<std::mutex> lk(mutex_);
        file_.flush();
    }

private:
    string file_path_;
    fstream file_;
    uint32_t num_pages_;
    mutex mutex_;
};