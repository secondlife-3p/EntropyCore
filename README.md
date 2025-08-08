# EntropyCore

Core utilities and building blocks for the Entropy Engine.

## Components

- **TypeSystem**: Type-safe handle system with generation-based validation
- **Graph**: Directed Acyclic Graph data structures for dependency management
- **Debug**: Debug interfaces, profiling integration, and object registry
- **Logging**: Flexible logging system with multiple sinks

## Building

### Prerequisites

- CMake 3.28 or higher
- C++20 compatible compiler
- vcpkg package manager

### Build Instructions

```bash
# Configure with vcpkg
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[path to vcpkg]/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build build

# Run tests
ctest --test-dir build
```

## Usage

EntropyCore is designed to be used as a dependency for other Entropy projects, however you can use it for your own projects.

```cmake
find_package(EntropyCore REQUIRED)
target_link_libraries(YourTarget PRIVATE EntropyCore::Core)
```

### Work Contract System

The work contract system provides a lock-free framework for managing parallel work execution. It consists of two main components:

- **WorkContractGroup**: Lock-free work contract pool with scheduling primitives
- **WorkContractHandle**: Type-safe handle for managing work contract lifecycle

The WorkContractGroup provides the core work scheduling primitives for Entropy. Credit goes to Michael A. Maniscalco for producing the original implementation that this is inspired from. You can find his implementation here: https://github.com/buildingcpp/work_contract. You can create your own thread pools or executors to consume work from these groups.

#### Basic Usage

Basic usage of work contracts is as follows.  This example creates a group, adds two contracts to it, schedules them, and executes them all.

```cpp
#include <EntropyCore.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>

using namespace EntropyEngine::Core::Concurrency;

int main() {
    // Create a work contract group
    WorkContractGroup workGroup(1024, "ExampleGroup");
    
    // Create and schedule some work
    auto handle1 = workGroup.createContract([]() {
        std::cout << "Task 1 executing...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    });
    
    auto handle2 = workGroup.createContract([]() {
        std::cout << "Task 2 executing...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    });
    
    // Schedule the work for execution
    handle1.schedule();
    handle2.schedule();
    
    // Execute all work (single-threaded)
    workGroup.executeAll();
    
    std::cout << "All work completed\n";
    
    return 0;
}
```

`executeAll()` isn't the only way you can execute work contracts.  This is really a convenience helper that does the following:

```cpp
WorkContractHandle handle = workGroup.selectForExecution(0);
if (!handle.valid()) {
    break;  // No more scheduled contracts
}

// Use the existing executeContract method for consistency
workGroup.executeContract(handle);
workGroup.completeExecution(handle);
```

You can use this to write your own concurrency methods.

### Work Service

The WorkService is a thread pool service that executes work from multiple WorkContractGroups. It provides a complete execution infrastructure with pluggable scheduling strategies.

#### Basic WorkService Usage

```cpp
#include <EntropyCore.h>
#include <iostream>

using namespace EntropyEngine::Core::Concurrency;

int main() {
    // Create a work service
    WorkService::Config config;
    config.threadCount = 4;
    WorkService service(config);
    
    // Create a work contract group
    WorkContractGroup group(512);
    service.addWorkContractGroup(&group);
    
    // Start the service
    service.start();
    
    // Create and schedule work
    auto handle = group.createContract([]() {
        std::cout << "Hello from worker thread!" << std::endl;
    });
    handle.schedule();
    
    // Wait for work to complete
    group.wait();
    
    // Stop the service
    service.stop();
    
    return 0;
}
```


The WorkService uses schedulers to decide which work groups to execute from. The default scheduler is AdaptiveRankingScheduler, but you can provide your own:

```cpp
// Create a custom scheduler
IWorkScheduler::Config schedulerConfig;
auto roundRobinScheduler = std::make_unique<RoundRobinScheduler>(schedulerConfig);

// Pass it to the service
WorkService service(config, std::move(roundRobinScheduler));
```

You can also add multiple work groups to the same service:

```cpp
// Add multiple work groups
WorkContractGroup group1(512);
WorkContractGroup group2(512);
service.addWorkContractGroup(&group1);
service.addWorkContractGroup(&group2);

// Work from both groups will be executed by the same worker threads
```

## License

This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

Copyright (c) 2025 Jonathan "Geenz" Goodman