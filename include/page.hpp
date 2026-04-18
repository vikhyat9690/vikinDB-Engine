#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>

/**
 * Constants
 */
static constexpr uint32_t PAGE_SIZE = 16384;
static constexpr uint32_t INVALID_PAGE_ID = UINT32_MAX;

/**
 * Page Headers
 */
enum class PageType : uint16_t {
    INVALID = 0,
    DATA = 1,
    INDEX = 2,
    OVERFLOW = 3,
    FREE = 4,
};

struct Slot
{
    uint16_t offset;
    uint16_t length;
};


/**
 * Page
 */
struct PageHeader {
    uint32_t page_id;
    uint32_t checksum;
    uint16_t num_slots;
    uint16_t free_slots;
    uint16_t free_offset;
    uint32_t lsn;
    PageType page_type;
    uint8_t reserved[8];
};

static constexpr uint32_t PAGE_HEADER_SIZE = sizeof(PageHeader);
static constexpr uint32_t PAGE_DATA_SIZE = PAGE_SIZE - PAGE_HEADER_SIZE;

struct Page {
    PageHeader header;
    uint8_t data[PAGE_DATA_SIZE];

    //Lifecycle
    void init(uint32_t id, PageType type = PageType::DATA) {
        std::memset(this, 0, PAGE_SIZE);
        header.page_id = id;
        header.page_type = type;
        header.num_slots = 0;
        header.free_offset = PAGE_DATA_SIZE;
        header.checksum = 0;
    }

    // Slot directory helpers
    Slot* slot_array() {
        return reinterpret_cast<Slot*>(data);
    }

    const Slot* slot_array() const {
        return reinterpret_cast<const Slot*>(data);
    }

    uint16_t slot_dir_size() const {
        return static_cast<uint16_t>(header.num_slots * sizeof(Slot));
    }

    uint16_t free_space() const {
        return header.free_offset - slot_dir_size();
    }

    // Record Insertion
    int insert_record(const uint8_t* record, uint16_t length) {
        uint16_t needed = length + static_cast<uint16_t>(sizeof(Slot));
        if(free_space() < needed) return -1;

        header.free_offset -= length;
        std::memcpy(data + header.free_offset, record, length);

        int slot_idx = header.num_slots;
        slot_array()[slot_idx] = { header.free_offset, length};
        header.num_slots++;

        return slot_idx;
    }

    // Record retrieval
    const uint8_t* get_record(uint16_t slot_idx, uint16_t& out_length) const {
        if (slot_idx >= header.num_slots) return nullptr;
        const Slot& s = slot_array()[slot_idx];
        if (s.offset == 0 && s.length == 0) return nullptr;
        out_length = s.length;
        return data + s.offset;
    }

    // Record deletion
    bool delete_record(uint16_t slot_idx) {
        if (slot_idx >= header.num_slots) return false;
        slot_array()[slot_idx] = {0,0};
        return true;
    }

    bool is_valid() const {
        return header.page_type != PageType::INVALID &&
                header.page_id != INVALID_PAGE_ID;
    }
};

static_assert(sizeof(Page) == PAGE_SIZE,
    "Page struct must be exactly PAGE_SIZE bytes");