#ifndef VOXEL_HPP
#define VOXEL_HPP

#include <cstdint>

inline uint32_t packVoxelData(uint8_t isActive, uint8_t r, uint8_t g, uint8_t b, uint8_t blockType) {
    uint32_t data = 0;
    data |= (isActive & 0x1) << 31;        // Store isActive in the highest bit
    data |= (blockType & 0x7) << 24;       // Store blockType in the next 3 bits
    data |= (r & 0xFF) << 16;              // Store red in the next 8 bits
    data |= (g & 0xFF) << 8;               // Store green in the next 8 bits
    data |= (b & 0xFF);                    // Store blue in the lowest 8 bits
    return data;
}

inline void unpackVoxelData(uint32_t data, uint8_t& isActive, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& blockType) {
    isActive = (data >> 31) & 0x1;
    blockType = (data >> 24) & 0x7;
    r = (data >> 16) & 0xFF;
    g = (data >> 8) & 0xFF;
    b = data & 0xFF;
}

#endif