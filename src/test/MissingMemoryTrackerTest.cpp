#include <gtest/gtest.h>
#include "JIT/JITRuntime.h"
#include "Prebuilt/Utils.h"
#include <glog/logging.h>

class MissingMemoryTrackerTest : public ::testing::Test {
protected:
    void SetUp() override {
        LOG(INFO) << "Setting up MissingMemoryTrackerTest";
        // Clear any previous missing memory entries
        Runtime::MissingMemoryTracker::ClearMissingMemory();
    }

    void TearDown() override {
        LOG(INFO) << "Tearing down MissingMemoryTrackerTest";
        Runtime::MissingMemoryTracker::ClearMissingMemory();
    }
};

TEST_F(MissingMemoryTrackerTest, TestMemoryAlignment) {
    // Test memory access in the middle of a page
    const uint64_t UNALIGNED_ADDR = 0x1fff;  // Not page-aligned address
    const uint8_t ACCESS_SIZE = 1;
    
    LOG(INFO) << "Testing memory alignment with unaligned address 0x" << std::hex << UNALIGNED_ADDR;
    Runtime::MissingMemoryTracker::AddMissingMemory(UNALIGNED_ADDR, ACCESS_SIZE);

    // Get missing memory entries
    const auto& missing_memory = Runtime::MissingMemoryTracker::GetMissingMemory();
    
    // Should only have one entry
    ASSERT_EQ(missing_memory.size(), 1);
    
    // Verify the entry is page-aligned but keeps original access size
    uint64_t expected_aligned_addr = UNALIGNED_ADDR & ~(PREBUILT_MEMORY_CELL_SIZE - 1);
    ASSERT_EQ(missing_memory[0].first, expected_aligned_addr);
    ASSERT_EQ(missing_memory[0].second, ACCESS_SIZE);
    
    LOG(INFO) << "Verified memory was aligned from 0x" << std::hex << UNALIGNED_ADDR 
              << " to 0x" << expected_aligned_addr;
}

TEST_F(MissingMemoryTrackerTest, TestPageBoundaryCrossing) {
    // Test memory access that crosses a page boundary
    const uint64_t PAGE_SIZE = PREBUILT_MEMORY_CELL_SIZE;
    const uint64_t BASE_PAGE = 0x1000;  // Start of a page
    const uint64_t NEAR_END_ADDR = BASE_PAGE + PAGE_SIZE - 1;  // 2 bytes before page end
    const uint8_t CROSSING_SIZE = 2;  // Access size that will cross the boundary
    
    LOG(INFO) << "Testing page boundary crossing at address 0x" << std::hex << NEAR_END_ADDR;
    Runtime::MissingMemoryTracker::AddMissingMemory(NEAR_END_ADDR, CROSSING_SIZE);

    // Get missing memory entries
    const auto& missing_memory = Runtime::MissingMemoryTracker::GetMissingMemory();
    
    // Should have two entries for two pages
    ASSERT_EQ(missing_memory.size(), 2);
    
    // Verify first page with original access size
    ASSERT_EQ(missing_memory[0].first, BASE_PAGE);
    ASSERT_EQ(missing_memory[0].second, CROSSING_SIZE);
    LOG(INFO) << "Verified first page at 0x" << std::hex << missing_memory[0].first;
    
    // Verify second page with original access size
    ASSERT_EQ(missing_memory[1].first, BASE_PAGE + PAGE_SIZE);
    ASSERT_EQ(missing_memory[1].second, CROSSING_SIZE);
    LOG(INFO) << "Verified second page at 0x" << std::hex << missing_memory[1].first;
}

TEST_F(MissingMemoryTrackerTest, TestPageBoundaryTouch) {
    // Test memory access that exactly touches the page boundary
    const uint64_t PAGE_SIZE = PREBUILT_MEMORY_CELL_SIZE;
    const uint64_t BASE_PAGE = 0x1000;
    const uint64_t BOUNDARY_ADDR = BASE_PAGE + PAGE_SIZE - 4;  // 4 bytes before page end
    const uint8_t TOUCH_SIZE = 8;  // Access size that will exactly touch the boundary
    
    LOG(INFO) << "Testing page boundary touch at address 0x" << std::hex << BOUNDARY_ADDR;
    Runtime::MissingMemoryTracker::AddMissingMemory(BOUNDARY_ADDR, TOUCH_SIZE);

    // Get missing memory entries
    const auto& missing_memory = Runtime::MissingMemoryTracker::GetMissingMemory();
    
    // Should have two entries because we touch the boundary
    ASSERT_EQ(missing_memory.size(), 2);
    
    // Verify first page with original access size
    ASSERT_EQ(missing_memory[0].first, BASE_PAGE);
    ASSERT_EQ(missing_memory[0].second, TOUCH_SIZE);
    LOG(INFO) << "Verified first page at 0x" << std::hex << missing_memory[0].first;
    
    // Verify second page with original access size
    ASSERT_EQ(missing_memory[1].first, BASE_PAGE + PAGE_SIZE);
    ASSERT_EQ(missing_memory[1].second, TOUCH_SIZE);
    LOG(INFO) << "Verified second page at 0x" << std::hex << missing_memory[1].first;
} 