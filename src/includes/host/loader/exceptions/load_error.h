#pragma once
#include <stdexcept>

namespace ntmac::loader {

class LoadError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/**
 * DllMain (DLL_PROCESS_ATTACH) が FALSE を返した
 */
class DllInitError : public LoadError {
public:
    using LoadError::LoadError;
};

}
