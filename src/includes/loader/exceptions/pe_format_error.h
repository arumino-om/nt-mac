#pragma once
#include <stdexcept>

namespace ntmac::loader {

class PeFormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

}
