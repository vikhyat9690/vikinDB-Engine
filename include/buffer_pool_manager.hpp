#pragma once
#include "page.hpp"
#include "disk_manager.hpp"

#include <cstdint>
#include <unordered_map>
#include <list>
#include <vector>
#include <mutex>
#include <stdexcept>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
//  BufferPoolManager  (BPM)
//
//  The BPM sits between the query engine and the disk.
//  It keeps a fixed number of Page frames in RAM.
//
//  Key concepts:
//
//   frame_id   – index into the in-memory pool array (0 … pool_size-1)
//   page_id    – logical page number on disk
//   pin_count  – how many threads/operators are currently using this page
//   dirty flag – page was modified and must be flushed before eviction
//
//  Eviction policy: LRU  (Least Recently Used)
//   • A page is evictable only when pin_count == 0.
//   • We track eviction order with a doubly-linked list + hash map (O(1) ops).
//
//  Typical call sequence:
//
//    Page* p = bpm.fetch_page(42);   // bring page 42 into a frame
//    // ... read/write p->data ...
//    bpm.unpin_page(42, true);       // release; dirty=true → will be flushed
//    bpm.flush_page(42);             // optional explicit flush
// ─────────────────────────────────────────────────────────────────────────────

using frame_id_t = int32_t;
static constexpr frame_id_t INVALID_FRAME_ID = -1;

// ── Per-frame metadata ────────────────────────────────────────────────────────
struct FrameMeta {
    uint32_t  page_id   = INVALID_PAGE_ID;
    int32_t   pin_count = 0;
    bool      dirty     = false;
};

// ── LRU Replacer ─────────────────────────────────────────────────────────────
//
//  Tracks which frames are currently *evictable* (pin_count == 0).
//  The front of the list is the LRU candidate (evict first).
//
class LRUReplacer {
public:
    explicit LRUReplacer(size_t capacity) : capacity_(capacity) {}

    // Mark frame as recently used (move to back / remove from evictable set).
    void pin(frame_id_t fid) {
        auto it = pos_.find(fid);
        if (it != pos_.end()) {
            order_.erase(it->second);
            pos_.erase(it);
        }
    }

    // Frame is now evictable (add to front of LRU list).
    void unpin(frame_id_t fid) {
        if (pos_.count(fid)) return;   // already evictable
        order_.push_back(fid);
        pos_[fid] = std::prev(order_.end());
    }

    // Pick the LRU victim.  Returns INVALID_FRAME_ID if none available.
    frame_id_t evict() {
        if (order_.empty()) return INVALID_FRAME_ID;
        frame_id_t victim = order_.front();
        order_.pop_front();
        pos_.erase(victim);
        return victim;
    }

    size_t evictable_count() const { return order_.size(); }

private:
    size_t                                        capacity_;
    std::list<frame_id_t>                         order_;   // front = LRU
    std::unordered_map<frame_id_t,
        std::list<frame_id_t>::iterator>          pos_;
};

// ── BufferPoolManager ─────────────────────────────────────────────────────────
class BufferPoolManager {
public:
    /**
     * @param pool_size   number of in-memory page frames
     * @param disk        owning DiskManager (must outlive BPM)
     */
    BufferPoolManager(size_t pool_size, DiskManager& disk)
        : pool_size_(pool_size),
          disk_(disk),
          replacer_(pool_size),
          pool_(pool_size),
          meta_(pool_size)
    {
        // All frames start on the free list.
        for (frame_id_t i = 0; i < static_cast<frame_id_t>(pool_size); ++i) {
            free_frames_.push_back(i);
        }
    }

    // ── Public API ────────────────────────────────────────────────────────────

    /**
     * fetch_page: bring `page_id` into a frame and return a pointer.
     *  • If the page is already cached, just pin it and return.
     *  • Otherwise find a free/evictable frame, possibly flush dirty page,
     *    then read from disk.
     * Returns nullptr if no frame is available.
     */
    Page* fetch_page(uint32_t page_id) {
        std::lock_guard<std::mutex> lk(mutex_);

        // Cache hit?
        auto it = page_table_.find(page_id);
        if (it != page_table_.end()) {
            frame_id_t fid = it->second;
            meta_[fid].pin_count++;
            replacer_.pin(fid);          // no longer evictable while pinned
            return &pool_[fid];
        }

        // Need a frame.
        frame_id_t fid = get_free_frame();
        if (fid == INVALID_FRAME_ID) return nullptr;

        // Load page from disk.
        disk_.read_page(page_id, &pool_[fid]);
        page_table_[page_id] = fid;
        meta_[fid] = { page_id, 1, false };
        replacer_.pin(fid);

        return &pool_[fid];
    }

    /**
     * new_page: allocate a brand-new page on disk, load into a frame.
     * Optionally receives the new page_id via out-param.
     * Returns nullptr if no frame available.
     */
    Page* new_page(uint32_t* out_page_id = nullptr,
                   PageType  type        = PageType::DATA)
    {
        std::lock_guard<std::mutex> lk(mutex_);

        frame_id_t fid = get_free_frame();
        if (fid == INVALID_FRAME_ID) return nullptr;

        uint32_t pid = disk_.allocate_page(&pool_[fid], type);
        page_table_[pid] = fid;
        meta_[fid] = { pid, 1, true };   // dirty: not yet flushed
        replacer_.pin(fid);

        if (out_page_id) *out_page_id = pid;
        return &pool_[fid];
    }

    /**
     * unpin_page: signal that a caller is done with page_id.
     * is_dirty=true marks the page for eventual flush before eviction.
     */
    bool unpin_page(uint32_t page_id, bool is_dirty) {
        std::lock_guard<std::mutex> lk(mutex_);

        auto it = page_table_.find(page_id);
        if (it == page_table_.end()) return false;

        frame_id_t fid = it->second;
        if (meta_[fid].pin_count <= 0) return false;

        meta_[fid].pin_count--;
        if (is_dirty) meta_[fid].dirty = true;

        if (meta_[fid].pin_count == 0) {
            replacer_.unpin(fid);   // now eligible for eviction
        }
        return true;
    }

    /**
     * flush_page: write a specific page to disk regardless of dirty flag.
     */
    bool flush_page(uint32_t page_id) {
        std::lock_guard<std::mutex> lk(mutex_);
        return flush_page_locked(page_id);
    }

    /** flush_all: write every dirty page to disk. */
    void flush_all() {
        std::lock_guard<std::mutex> lk(mutex_);
        for (auto& [pid, fid] : page_table_) {
            flush_page_locked(pid);
        }
    }

    /**
     * delete_page: remove page from BPM (must be unpinned).
     * Does NOT erase from disk (that's a higher-level concern).
     */
    bool delete_page(uint32_t page_id) {
        std::lock_guard<std::mutex> lk(mutex_);

        auto it = page_table_.find(page_id);
        if (it == page_table_.end()) return true;  // not cached, nothing to do

        frame_id_t fid = it->second;
        if (meta_[fid].pin_count > 0) return false;  // still in use

        replacer_.pin(fid);   // remove from LRU set
        page_table_.erase(it);
        meta_[fid] = {};
        free_frames_.push_back(fid);
        return true;
    }

    // ── Diagnostics ───────────────────────────────────────────────────────────

    size_t pool_size()       const { return pool_size_; }
    size_t cached_pages()    const { return page_table_.size(); }
    size_t free_frames()     const { return free_frames_.size(); }
    size_t evictable_pages() const { return replacer_.evictable_count(); }

    std::string stats() const {
        return "BPM | pool=" + std::to_string(pool_size_) +
               " cached=" + std::to_string(page_table_.size()) +
               " free_frames=" + std::to_string(free_frames_.size()) +
               " evictable=" + std::to_string(replacer_.evictable_count());
    }

private:
    // ── Internals ─────────────────────────────────────────────────────────────

    /** Must be called with mutex_ held. */
    frame_id_t get_free_frame() {
        if (!free_frames_.empty()) {
            frame_id_t fid = free_frames_.back();
            free_frames_.pop_back();
            return fid;
        }
        // Evict LRU page.
        frame_id_t fid = replacer_.evict();
        if (fid == INVALID_FRAME_ID) return INVALID_FRAME_ID;

        uint32_t old_pid = meta_[fid].page_id;
        if (meta_[fid].dirty) {
            disk_.write_page(old_pid, &pool_[fid]);
        }
        page_table_.erase(old_pid);
        meta_[fid] = {};
        return fid;
    }

    /** Must be called with mutex_ held. */
    bool flush_page_locked(uint32_t page_id) {
        auto it = page_table_.find(page_id);
        if (it == page_table_.end()) return false;
        frame_id_t fid = it->second;
        disk_.write_page(page_id, &pool_[fid]);
        meta_[fid].dirty = false;
        return true;
    }

    // ── Data members ──────────────────────────────────────────────────────────
    size_t                                    pool_size_;
    DiskManager&                              disk_;
    LRUReplacer                               replacer_;

    std::vector<Page>                         pool_;    // the actual frame memory
    std::vector<FrameMeta>                    meta_;

    std::unordered_map<uint32_t, frame_id_t>  page_table_;  // page_id → frame_id
    std::list<frame_id_t>                     free_frames_;

    std::mutex                                mutex_;
};