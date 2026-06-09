#pragma once

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace otn {

inline std::string precisionEncode(const std::string &value, size_t precision)
{
    double fval = std::stod(value);
    int64_t ival = std::llround(fval * std::pow(10, precision));
    return std::to_string(ival);
}

inline std::string precisionDecode(const std::string &value, size_t precision)
{
    double fval = std::stod(value) / std::pow(10, precision);
    std::ostringstream oss;
    oss << std::fixed
        << std::setprecision(static_cast<int>(precision))
        << fval;
    return oss.str();
}

} // namespace otn
