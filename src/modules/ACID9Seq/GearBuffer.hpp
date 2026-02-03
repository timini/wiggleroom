#pragma once

#include "rack.hpp"
#include <vector>
#include <random>
#include <cstdint>
#include <algorithm>

namespace WiggleRoom {

/**
 * GearBuffer - A rotating step buffer with variable length
 *
 * Used for the three gears in the ACID-9 Interference Engine:
 * - Gear A: Pitch values (16 steps, 0-24 semitones)
 * - Gear B: Pitch offsets (variable length, -12 to +12 semitones)
 * - Gear C: Gate on/off (Euclidean pattern)
 */
class GearBuffer {
public:
    static constexpr int MAX_STEPS = 16;

    enum class DataType {
        PITCH,      // 0-24 semitones (Gear A)
        OFFSET,     // -12 to +12 semitones (Gear B)
        GATE        // On/Off (Gear C)
    };

    GearBuffer(int length = 16, DataType type = DataType::PITCH)
        : length_(std::min(std::max(length, 1), MAX_STEPS))
        , type_(type)
        , position_(0)
    {
        data_.resize(MAX_STEPS, 0);
        initializeDefault();
    }

    // Advance the playhead by one step, wrapping at length
    void advance() {
        position_ = (position_ + 1) % length_;
    }

    // Reset playhead to step 0
    void reset() {
        position_ = 0;
    }

    // Set playhead to specific position
    void setPosition(int pos) {
        position_ = pos % length_;
    }

    // Get current playhead position
    int getPosition() const {
        return position_;
    }

    // Get the length of this gear
    int getLength() const {
        return length_;
    }

    // Set the length (preserves existing data, initializes new steps)
    void setLength(int len) {
        int oldLength = length_;
        length_ = std::min(std::max(len, 1), MAX_STEPS);

        // Initialize any newly accessible steps
        if (length_ > oldLength) {
            for (int i = oldLength; i < length_; i++) {
                data_[i] = getDefaultValue();
            }
        }

        // Wrap position if it exceeds new length
        if (position_ >= length_) {
            position_ = position_ % length_;
        }
    }

    // Get value at current position
    int getValue() const {
        return data_[position_];
    }

    // Get value at specific step
    int getValueAt(int step) const {
        return data_[step % length_];
    }

    // Set value at specific step
    void setValueAt(int step, int value) {
        data_[step % length_] = clampValue(value);
    }

    // Get pitch value (for Gear A)
    int getPitch() const {
        return data_[position_];
    }

    // Get offset value (for Gear B)
    int getOffset() const {
        return data_[position_];
    }

    // Get gate value (for Gear C)
    bool getGate() const {
        return data_[position_] != 0;
    }

    // Randomize all values
    void randomize(std::mt19937& rng) {
        for (int i = 0; i < length_; i++) {
            data_[i] = generateRandomValue(rng);
        }
    }

    // Set from Euclidean pattern (for Gear C)
    void setEuclidean(int hits, int length) {
        length_ = std::min(std::max(length, 1), MAX_STEPS);
        hits = std::min(std::max(hits, 0), length_);

        // Bjorklund's algorithm for Euclidean rhythm generation
        std::vector<std::vector<int>> pattern;

        // Initialize with hits and rests
        for (int i = 0; i < hits; i++) {
            pattern.push_back({1});
        }
        for (int i = 0; i < length_ - hits; i++) {
            pattern.push_back({0});
        }

        // Distribute rests among hits
        while (pattern.size() > 1) {
            size_t hitCount = 0;
            size_t restCount = 0;

            // Count hits (patterns starting with 1) and rests
            for (const auto& p : pattern) {
                if (p[0] == 1) hitCount++;
                else restCount++;
            }

            if (restCount == 0) break;

            // Distribute: append one rest pattern to each hit pattern
            size_t minCount = std::min(hitCount, restCount);
            for (size_t i = 0; i < minCount; i++) {
                // Append the last pattern to the i-th pattern
                auto& target = pattern[i];
                const auto& source = pattern.back();
                target.insert(target.end(), source.begin(), source.end());
                pattern.pop_back();
            }

            // Check if we can't distribute further
            if (minCount == 0 || hitCount <= 1) break;
        }

        // Flatten to data array
        int idx = 0;
        for (const auto& p : pattern) {
            for (int val : p) {
                if (idx < MAX_STEPS) {
                    data_[idx++] = val;
                }
            }
        }

        // Fill remaining with zeros if needed
        while (idx < MAX_STEPS) {
            data_[idx++] = 0;
        }

        // Wrap position if needed
        if (position_ >= length_) {
            position_ = position_ % length_;
        }
    }

    // Get pointer to data for visualization
    const int* getData() const {
        return data_.data();
    }

    // Serialize for JSON
    json_t* toJson() const {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "length", json_integer(length_));
        json_object_set_new(rootJ, "position", json_integer(position_));

        json_t* dataJ = json_array();
        for (int i = 0; i < MAX_STEPS; i++) {
            json_array_append_new(dataJ, json_integer(data_[i]));
        }
        json_object_set_new(rootJ, "data", dataJ);

        return rootJ;
    }

    // Deserialize from JSON
    void fromJson(json_t* rootJ) {
        json_t* lengthJ = json_object_get(rootJ, "length");
        if (lengthJ) {
            length_ = std::min(std::max((int)json_integer_value(lengthJ), 1), MAX_STEPS);
        }

        json_t* positionJ = json_object_get(rootJ, "position");
        if (positionJ) {
            position_ = (int)json_integer_value(positionJ) % length_;
        }

        json_t* dataJ = json_object_get(rootJ, "data");
        if (dataJ && json_is_array(dataJ)) {
            for (int i = 0; i < MAX_STEPS && i < (int)json_array_size(dataJ); i++) {
                data_[i] = (int)json_integer_value(json_array_get(dataJ, i));
            }
        }
    }

private:
    int length_;
    DataType type_;
    int position_;
    std::vector<int> data_;

    int getDefaultValue() const {
        switch (type_) {
            case DataType::PITCH:
                return 12;  // Middle of range (C4 equivalent)
            case DataType::OFFSET:
                return 0;   // No offset
            case DataType::GATE:
                return 1;   // Gate on by default
            default:
                return 0;
        }
    }

    void initializeDefault() {
        for (int i = 0; i < MAX_STEPS; i++) {
            data_[i] = getDefaultValue();
        }
    }

    int clampValue(int value) const {
        switch (type_) {
            case DataType::PITCH:
                return std::min(std::max(value, 0), 24);
            case DataType::OFFSET:
                return std::min(std::max(value, -12), 12);
            case DataType::GATE:
                return value != 0 ? 1 : 0;
            default:
                return value;
        }
    }

    int generateRandomValue(std::mt19937& rng) const {
        switch (type_) {
            case DataType::PITCH: {
                // Favor common scale degrees (pentatonic-ish distribution)
                std::uniform_int_distribution<int> dist(0, 24);
                return dist(rng);
            }
            case DataType::OFFSET: {
                // Favor smaller offsets
                std::normal_distribution<float> dist(0.0f, 4.0f);
                int val = static_cast<int>(std::round(dist(rng)));
                return std::min(std::max(val, -12), 12);
            }
            case DataType::GATE: {
                std::uniform_int_distribution<int> dist(0, 1);
                return dist(rng);
            }
            default:
                return 0;
        }
    }
};

} // namespace WiggleRoom
