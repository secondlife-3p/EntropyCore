#include <catch2/catch_all.hpp>
#include "../src/Concurrency/WorkGraph.h"
#include "../src/Concurrency/WorkContractGroup.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>

using namespace EntropyEngine::Core::Concurrency;
using namespace std::chrono_literals;

TEST_CASE("WorkGraph memory efficiency isolated", "[WorkGraph][Memory]") {
    WorkContractGroup group(16);
    
    // Create many minimal graphs
    std::vector<std::unique_ptr<WorkGraph>> graphs;
    std::atomic<int> totalNodesExecuted{0};
    
    WorkGraphConfig config;
    config.enableEvents = false;
    config.enableStateManager = false;
    config.enableAdvancedScheduling = false;
    config.expectedNodeCount = 3; // Small graphs
    config.maxDeferredNodes = 1000; // Allow plenty of deferred nodes
    
    for (int i = 0; i < 100; i++) {
        graphs.push_back(std::make_unique<WorkGraph>(&group, config));
        
        auto& graph = *graphs.back();
        auto n1 = graph.addNode([&totalNodesExecuted]() { totalNodesExecuted++; }, "n1");
        auto n2 = graph.addNode([&totalNodesExecuted]() { totalNodesExecuted++; }, "n2");
        auto n3 = graph.addNode([&totalNodesExecuted]() { totalNodesExecuted++; }, "n3");
        
        graph.addDependency(n1, n2);
        graph.addDependency(n2, n3);
    }
    
    // Execute all graphs
    for (size_t i = 0; i < graphs.size(); i++) {
        graphs[i]->execute();
    }
    
    // Drive execution until all graphs complete
    bool allComplete = false;
    int iterations = 0;
    while (!allComplete && iterations < 1000) {
        // Process any scheduled work
        group.executeAllBackgroundWork();
        
        // Process deferred nodes for all graphs
        for (auto& graph : graphs) {
            graph->processDeferredNodes();
        }
        
        // Check if all graphs are complete
        allComplete = true;
        int incompleteCount = 0;
        for (auto& graph : graphs) {
            if (!graph->isComplete()) {
                allComplete = false;
                incompleteCount++;
            }
        }
        
        if (iterations % 100 == 0) {
            INFO("Iteration " << iterations << ", incomplete graphs: " << incompleteCount << ", nodes executed: " << totalNodesExecuted.load());
        }
        
        iterations++;
        
        // Small delay to prevent busy-waiting
        if (!allComplete) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    
    // Now verify all completed
    for (size_t i = 0; i < graphs.size(); i++) {
        REQUIRE(graphs[i]->isComplete());
    }
    
    // This demonstrates we can have many lightweight graphs
    REQUIRE(graphs.size() == 100);
    REQUIRE(totalNodesExecuted.load() == 300);
}