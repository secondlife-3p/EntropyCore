#include <catch2/catch_all.hpp>
#include "../src/Concurrency/WorkGraph.h"
#include "../src/Concurrency/WorkContractGroup.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <iostream>

using namespace EntropyEngine::Core::Concurrency;
using namespace std::chrono_literals;

TEST_CASE("WorkGraph isolated hang test", "[isolated]") {
    
    SECTION("Many root nodes with limited capacity") {
        std::cout << "\n=== Testing many root nodes with limited capacity ===\n";
        
        WorkContractGroup group(4);  // Only 4 slots
        
        WorkGraphConfig config;
        config.enableEvents = true;
        config.enableStateManager = true;
        config.enableAdvancedScheduling = true;
        config.maxDeferredNodes = 50;
        config.enableDebugLogging = true;
        
        WorkGraph graph(&group, config);
        
        std::atomic<int> executed{0};
        
        // Add 10 independent nodes (all will be roots)
        std::cout << "Adding 10 independent nodes\n";
        for (int i = 0; i < 10; i++) {
            graph.addNode([&executed, i]() { 
                std::cout << "Node " << i << " executing\n";
                executed++;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                std::cout << "Node " << i << " complete\n";
            }, "node_" + std::to_string(i));
        }
        
        std::cout << "Calling execute()\n";
        graph.execute();
        
        std::cout << "Starting execution loop\n";
        int iterations = 0;
        while (!graph.isComplete() && iterations < 100) {
            std::cout << "Iteration " << iterations << ", executed: " << executed.load() << "\n";
            group.executeAllBackgroundWork();
            
            // Process deferred nodes
            graph.processDeferredNodes();
            
            std::this_thread::sleep_for(1ms);
            iterations++;
        }
        
        std::cout << "Execution loop done, executed: " << executed.load() << "\n";
        
        if (!graph.isComplete()) {
            std::cout << "Graph NOT complete after " << iterations << " iterations!\n";
            std::cout << "Pending: " << graph.getPendingCount() << "\n";
        }
        
        // Try wait with timeout using condition variable
        std::cout << "Calling wait()\n";
        auto result = graph.wait();
        std::cout << "Wait returned! Completed: " << result.completedCount << "\n";
        
        REQUIRE(executed == 10);
    }
}