# vikinDB-Engine

A custom-built database engine implementation from first principles, featuring a high-performance storage layer with buffer pool management, disk I/O optimization, and LRU page replacement policies.

## Overview

vikinDB is a multi-phase database engine project designed to implement core database concepts including:
- **Phase 1**: Storage Layer (✅ Complete)
- **Phase 2**: B+ Tree Indexing (Planned)
- **Phase 3**: SQL Parser (Planned)
- **Phase 4**: Query Execution (Planned)

## Architecture

### Phase 1: Storage Layer ✅

The foundation of the database engine, implementing efficient disk and memory management.

#### Components

**Page Manager** (`include/page.hpp`)
- Fixed-size pages (16KB) with structured layouts
- Configurable page types (DATA, INDEX, OVERFLOW, FREE)
- Slot-based record management for variable-length data
- Automatic free space tracking

**Disk Manager** (`include/disk_manager.hpp`)
- Direct file I/O for persistent storage
- Page allocation and deallocation
- Efficient sequential read/write operations
- Multi-threaded safety with mutex locking

**Buffer Pool Manager** (`include/buffer_pool_manager.hpp`)
- Fixed-size in-memory frame pool
- Intelligent page caching strategy
- Automatic dirty page flushing
- Statistics and monitoring

**LRU Replacer** (`include/buffer_pool_manager.hpp`)
- Least-Recently-Used page eviction policy
- O(1) frame pinning/unpinning operations
- Efficient eviction tracking with doubly-linked list and hash map

## Getting Started

### Prerequisites

- C++17 compatible compiler (g++, clang, etc.)
- Standard C++ library

### Building

**Option 1: Direct Compilation**
```bash
g++ -std=c++17 main/test1.cpp -o test_phase1
```

**Option 2: With Optimizations**
```bash
g++ -std=c++17 -O2 -Wall -Wextra main/test1.cpp -o test_phase1
```

### Running Tests

```bash
./test_phase1
```

#### Expected Output
```
══════════════════════════════════════════
  MyDB Phase 1 – Storage Layer Test Suite
══════════════════════════════════════════

[ TEST ] Page struct size is exactly PAGE_SIZE
  ✓  sizeof(Page) == 16384 (expected 16384)
...
══════════════════════════════════════════
  Results:  40 passed,  0 failed
══════════════════════════════════════════
```

## Test Coverage

The test suite validates all storage layer components:

- **Page Layout** (3 tests)
  - Struct sizing and initialization
  - Record insertion and retrieval
  - Free space tracking

- **Disk Manager** (4 tests)
  - File creation and persistence
  - Multi-page allocation
  - Page read/write round-trip integrity

- **LRU Replacer** (3 tests)
  - FIFO eviction order
  - Pin/unpin logic
  - Evictable frame counting

- **Buffer Pool Manager** (30+ tests)
  - Page allocation and caching
  - Dirty page tracking and flushing
  - LRU eviction with pool overflow
  - Statistics reporting

## Project Structure

```
vikindb/
├── CMakeLists.txt                  # CMake build configuration
├── README.md                        # This file
├── include/
│   ├── page.hpp                    # Page structure and slot management
│   ├── disk_manager.hpp            # Disk I/O operations
│   └── buffer_pool_manager.hpp     # Buffer pool and LRU replacer
└── main/
    └── test1.cpp                   # Phase 1 test suite
```

## Key Design Decisions

### Page Layout
- **Header**: 48 bytes containing metadata (page_id, checksum, slot count, LSN, etc.)
- **Data Region**: 16,336 bytes for records and slot directory
- **Slot Directory**: Variable-sized, grows from start of data region
- **Records**: Allocated from end of data region, growing backward

### Memory Management
- **Fixed Pool**: Pre-allocated frames prevent external fragmentation
- **LRU Eviction**: Prioritizes least-used pages for removal
- **Dirty Tracking**: Minimizes unnecessary disk writes

### Thread Safety
- Mutex-protected I/O operations in DiskManager
- Safe concurrent access patterns for buffer pool

## Performance Characteristics

| Operation | Time Complexity | Notes |
|-----------|-----------------|-------|
| Insert Record | O(1) | Amortized, assuming free space |
| Get Record | O(1) | Direct slot array lookup |
| Delete Record | O(1) | Slot invalidation |
| Pin Frame | O(1) | Hash map + list operation |
| Evict Frame | O(1) | LRU head removal |
| Read Page | O(1) | Sequential disk access |
| Write Page | O(1) | Sequential disk access |

## Configuration

Key compile-time constants in [include/page.hpp](include/page.hpp):

```cpp
static constexpr uint32_t PAGE_SIZE = 16384;           // 16 KB pages
static constexpr uint32_t PAGE_DATA_SIZE = 16336;      // Available data space
static constexpr uint32_t INVALID_PAGE_ID = UINT32_MAX;
```

## Planned Phases

### Phase 2: B+ Tree Indexing
- Multi-level index structure
- Range queries and sorted scans
- Insertion and deletion with rebalancing

### Phase 3: SQL Parser
- SQL tokenization and parsing
- Query tree construction
- Expression evaluation

### Phase 4: Query Execution
- Query optimizer
- Execution engine
- Join algorithms (nested loop, hash join, etc.)

## Contributing

This is an educational project demonstrating database engine fundamentals. The codebase prioritizes clarity and correctness over production optimizations.

## License

Educational use only.

## Author

vikhy atgarg