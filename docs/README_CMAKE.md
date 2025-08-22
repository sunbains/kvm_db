# Building KVM Probe

## Requirements

- **CMake**: 3.25 or later
- **C++ Compiler**: 
  - GCC 11+ (recommended: GCC 14+ for std::print)
  - Clang 13+ (recommended: Clang 18+ for std::print)  
  - MSVC 19.30+ (VS 2022)
- **System**: Linux with KVM headers
- **Libraries**: Standard library only

## Quick Start

### Using CMake Presets (Recommended)

```bash
# Configure and build development version
cmake --preset=dev
cmake --build --preset=dev

# Configure and build release version  
cmake --preset=release
cmake --build --preset=release

# Run the probe
./build/kvm_db

