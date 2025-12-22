/* ************************************************************************** */
/*                                                                            */
/*                                                        ::::::::            */
/*   Voxel.hpp                                          :+:    :+:            */
/*                                                     +:+                    */
/*   By: bfranco <bfranco@student.codam.nl>           +#+                     */
/*                                                   +#+                      */
/*   Created: 2025/12/22 10:47:34 by bfranco       #+#    #+#                 */
/*   Updated: 2025/12/22 10:47:34 by bfranco       ########   odam.nl         */
/*                                                                            */
/* ************************************************************************** */

//
// Created by bruno on 22/12/2025.
//

#ifndef VOXEL_HPP
#define VOXEL_HPP

#include <cstdint>

// Define a voxel as a 32-bit integer
typedef uint32_t		Voxel;

inline uint8_t getBlockType(const Voxel data) { return (data >> 24) & 0x7; }

inline uint8_t isActive(const Voxel data) { return (data >> 31) & 0x1; }

inline uint32_t getColor(const Voxel data) { return (data << 24) >> 24; }

inline Voxel packVoxelData(const uint8_t isActive, const uint8_t r, const uint8_t g, const uint8_t b, const uint8_t blockType) {
    Voxel data = 0;
    data |= (isActive & 0x1) << 31;        // Store isActive in the highest bit
    data |= (blockType & 0x7) << 24;       // Store blockType in the next 3 bits
    data |= (r & 0xFF) << 16;              // Store red in the next 8 bits
    data |= (g & 0xFF) << 8;               // Store green in the next 8 bits
    data |= (b & 0xFF);                    // Store blue in the lowest 8 bits
    return data;
}

inline void unpackVoxelData(const Voxel data, uint8_t& isActive, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& blockType) {
    isActive = (data >> 31) & 0x1;
    blockType = (data >> 24) & 0x7;
    r = (data >> 16) & 0xFF;
    g = (data >> 8) & 0xFF;
    b = data & 0xFF;
}

#endif // VOXEL_HPP