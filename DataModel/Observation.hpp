#pragma once

#include<vector>
#include<chrono>

#include"Measurement.hpp"

struct Observation{
    std::chrono::steady_clock::time_point timestamp;
    std::vector<Measurement> measurements;
};