#ifndef DYNAMIC_RENDER_DISTANCE_HPP
#define DYNAMIC_RENDER_DISTANCE_HPP

#include <glm/glm.hpp>
#include <algorithm>

class DynamicRenderDistance {
public:

    /**
     * Constructor
     * @param minR Minimum render distance (in chunk radius)
     * @param maxR Maximum render distance (in chunk radius)
     * @param lowFPS FPS threshold to start reducing distance
     * @param highFPS FPS threshold to start increasing distance
     */
    DynamicRenderDistance(const int minR, const int maxR, const int lowFPS = 50, const int highFPS = 90)
        : currentRadius(16),
          minRadius(minR),
          maxRadius(maxR),
          fpsThresholdLow(static_cast<float>(lowFPS)),
          fpsThresholdHigh(static_cast<float>(highFPS)),
          frameCounter(0),
          updateInterval(10) // Update every 10 frames
    {
    }

    /**
     * Update the render distance based on current FPS
     * @param currentFPS Current frames per second
     * @return New render distance (in chunk radius)
     */
    int update(const float currentFPS)
    {
        frameCounter++;

        // Only update every N frames to smooth out FPS spikes
        if (frameCounter < updateInterval)
            return currentRadius;

        frameCounter = 0;

        // If FPS is too low, reduce render distance
        if (currentFPS < fpsThresholdLow)
        {
            int newRadius = currentRadius - 1;
            newRadius = std::max(newRadius, minRadius);

            if (newRadius != currentRadius)
            {
                currentRadius = newRadius;
                return currentRadius;
            }
        }
        // If FPS is good, and we have headroom, increase render distance
        else if (currentFPS > fpsThresholdHigh && currentRadius < maxRadius)
        {
            int newRadius = currentRadius + 1;
            newRadius = std::min(newRadius, maxRadius);

            if (newRadius != currentRadius)
            {
                currentRadius = newRadius;
                return currentRadius;
            }
        }

        return currentRadius;
    }

    /**
     * Force set the render distance (e.g., for testing)
     */
    void setRadius(const int radius)
    {
        currentRadius = std::clamp(radius, minRadius, maxRadius);
    }

    /**
     * Get current render distance
     */
    [[nodiscard]] int getRadius() const { return currentRadius; }

    /**
     * Get minimum render distance
     */
    [[nodiscard]] int getMinRadius() const { return minRadius; }

    /**
     * Get maximum render distance
     */
    [[nodiscard]] int getMaxRadius() const { return maxRadius; }

    /**
     * Configure FPS thresholds
     * @param low FPS threshold to start reducing distance
     * @param high FPS threshold to start increasing distance
     */
    void setFPSThresholds(const float low, const float high)
    {
        fpsThresholdLow = low;
        fpsThresholdHigh = high;
    }

    /**
     * Configure update frequency
     * @param interval Update every N frames
     */
    void setUpdateInterval(const int interval)
    {
        updateInterval = std::max(1, interval);
    }

private:
    int currentRadius;
    int minRadius;
    int maxRadius;

    float fpsThresholdLow;
    float fpsThresholdHigh;

    int frameCounter;
    int updateInterval;
};

#endif // DYNAMIC_RENDER_DISTANCE_HPP