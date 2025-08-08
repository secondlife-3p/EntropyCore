#include <catch2/catch_all.hpp>
#include "../src/Concurrency/WorkGraph.h"
#include "../src/Concurrency/WorkContractGroup.h"
#include <atomic>
#include <vector>
#include <memory>

using namespace EntropyEngine::Core::Concurrency;

TEST_CASE("WorkGraph vector destruction", "[vector]") {
    
    SECTION("Vector of unique_ptr graphs") {
        WorkContractGroup group(16);
        
        // Create vector of graphs
        std::vector<std::unique_ptr<WorkGraph>> graphs;
        std::atomic<int> totalNodesExecuted{0};
        
        WorkGraphConfig config;
        config.enableDebugLogging = true;
        
        INFO("Creating 2 graphs");
        for (int i = 0; i < 2; i++) {
            graphs.push_back(std::make_unique<WorkGraph>(&group, config));
            
            auto& graph = *graphs.back();
            auto n1 = graph.addNode([&totalNodesExecuted]() { 
                totalNodesExecuted++; 
            }, "n1");
            auto n2 = graph.addNode([&totalNodesExecuted]() { 
                totalNodesExecuted++; 
            }, "n2");
            auto n3 = graph.addNode([&totalNodesExecuted]() { 
                totalNodesExecuted++; 
            }, "n3");
            
            graph.addDependency(n1, n2);
            graph.addDependency(n2, n3);
        }
        
        INFO("Executing all graphs");
        // Execute all graphs
        for (size_t i = 0; i < graphs.size(); i++) {
            graphs[i]->execute();
        }
        
        INFO("Driving execution");
        // Drive execution until all graphs complete
        bool allComplete = false;
        int iterations = 0;
        while (!allComplete && iterations < 10) {
            // Process any scheduled work
            group.executeAllBackgroundWork();
            
            // Check if all graphs are complete
            allComplete = true;
            for (auto& graph : graphs) {
                if (!graph->isComplete()) {
                    allComplete = false;
                    break;
                }
            }
            
            iterations++;
            INFO("Iteration " << iterations << ", allComplete: " << allComplete);
        }
        
        INFO("Verifying completion");
        // Now verify all completed
        for (size_t i = 0; i < graphs.size(); i++) {
            REQUIRE(graphs[i]->isComplete());
        }
        
        REQUIRE(totalNodesExecuted.load() == 6);
        
        INFO("Test section ending - graphs vector will be destroyed");
    }
    
    INFO("Test complete");
}