#include <catch2/catch_all.hpp>
#include "../src/Concurrency/WorkGraph.h"
#include "../src/Concurrency/WorkContractGroup.h"
#include <atomic>
#include <iostream>

using namespace EntropyEngine::Core::Concurrency;

TEST_CASE("WorkGraph debugging", "[debug]") {
    std::cout << "Starting WorkGraph debug test\n";
    
    WorkContractGroup group(4);
    
    WorkGraphConfig config;
    config.enableEvents = false;
    config.enableStateManager = false;
    config.enableAdvancedScheduling = false;
    config.enableDebugLogging = true;
    
    WorkGraph graph(&group, config);
    
    std::atomic<int> counter{0};
    
    std::cout << "Adding nodes\n";
    auto n1 = graph.addNode([&counter]() { 
        std::cout << "Node 1 executing\n";
        counter++; 
    }, "n1");
    
    auto n2 = graph.addNode([&counter]() { 
        std::cout << "Node 2 executing\n";
        counter++; 
    }, "n2");
    
    auto n3 = graph.addNode([&counter]() { 
        std::cout << "Node 3 executing\n";
        counter++; 
    }, "n3");
    
    graph.addDependency(n1, n2);
    graph.addDependency(n2, n3);
    
    std::cout << "Executing graph\n";
    graph.execute();
    
    std::cout << "Starting execution loop\n";
    // Drive execution synchronously
    int iterations = 0;
    while (!graph.isComplete() && iterations < 10) {
        std::cout << "Iteration " << iterations << ", pending: " << graph.getPendingCount() << "\n";
        group.executeAllBackgroundWork();
        iterations++;
    }
    
    std::cout << "Final counter: " << counter << ", pending: " << graph.getPendingCount() << "\n";
    
    REQUIRE(counter == 3);
    REQUIRE(graph.isComplete());
}