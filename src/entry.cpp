#include <exception>
#include <iostream>

#include "opencdc/opencdc.h"

int main(int argc, const char* argv[]) {
    try {
        return opencdc::run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Internal error: " << e.what() << "\n";
        return static_cast<int>(opencdc::ExitCode::INTERNAL_ERROR);
    } catch (...) {
        std::cerr << "Internal error: unknown exception\n";
        return static_cast<int>(opencdc::ExitCode::INTERNAL_ERROR);
    }
}
