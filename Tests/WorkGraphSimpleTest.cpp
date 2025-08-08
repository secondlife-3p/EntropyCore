#include <catch2/catch_all.hpp>
#include "../src/Concurrency/WorkGraph.h"
#include "../src/Concurrency/WorkContractGroup.h"
#include <atomic>
#include <vector>
#include <iostream>

using namespace EntropyEngine::Core::Concurrency;

TEST_CASE("WorkGraph simple hang investigation", "[simple]") {
    
    SECTION("Graph destroyed with pending work") {
        std::cout << "\n=== Starting Graph destroyed with pending work test ===\n";
        
        WorkContractGroup group(1); // Only 1 slot
        
        {
            WorkGraphConfig config;
            config.enableDebugLogging = true;
            
            WorkGraph graph(&group, config);
            
            // Add multiple nodes but don't execute them all
            auto n1 = graph.addNode([]() { 
                std::cout << "Node1 executing\n";
            }, "n1");
            
            auto n2 = graph.addNode([]() { 
                std::cout << "Node2 executing\n";
            }, "n2");
            
            auto n3 = graph.addNode([]() { 
                std::cout << "Node3 executing\n";
            }, "n3");
            
            graph.addDependency(n1, n2);
            graph.addDependency(n2, n3);
            
            graph.execute();
            
            // Execute only one round - should execute n1 only
            group.executeAllBackgroundWork();
            
            std::cout << "Pending nodes: " << graph.getPendingCount() << "\n";
            
            // Try calling wait() with pending work
            std::cout << "Calling wait() with pending work...\n";
            auto result = graph.wait();
            std::cout << "Wait returned! Completed: " << result.completedCount << "\n";
            
            std::cout << "Graph going out of scope\n";
        }
        
        std::cout << "Graph destroyed\n";
        std::cout << "=== Graph destroyed with pending work test complete ===\n\n";
    }
    
    SECTION("Two graphs sequential") {
        std::cout << "\n=== Starting Two graphs sequential test ===\n";
        
        // First graph
        {
            std::cout << "Creating first graph\n";
            WorkContractGroup group(4);
            
            WorkGraphConfig config;
            config.enableDebugLogging = true;
            
            WorkGraph graph(&group, config);
            std::atomic<int> counter{0};
            
            auto n1 = graph.addNode([&counter]() { 
                std::cout << "Graph1 Node1 executing\n";
                counter++; 
            }, "n1");
            
            graph.execute();
            
            // Drive execution
            while (!graph.isComplete()) {
                group.executeAllBackgroundWork();
            }
            
            std::cout << "First graph complete, counter=" << counter << "\n";
            REQUIRE(counter == 1);
            std::cout << "First graph object going out of scope\n";
        }
        
        // Second graph
        {
            std::cout << "\nCreating second graph\n";
            WorkContractGroup group(4);
            
            WorkGraphConfig config;
            config.enableDebugLogging = true;
            
            WorkGraph graph(&group, config);
            std::atomic<int> counter{0};
            
            auto n1 = graph.addNode([&counter]() { 
                std::cout << "Graph2 Node1 executing\n";
                counter++; 
            }, "n1");
            
            graph.execute();
            
            // Drive execution
            while (!graph.isComplete()) {
                group.executeAllBackgroundWork();
            }
            
            std::cout << "Second graph complete, counter=" << counter << "\n";
            REQUIRE(counter == 1);
            std::cout << "Second graph object going out of scope\n";
        }
        
        std::cout << "=== Two graphs sequential test complete ===\n\n";
    }
    
    SECTION("Two graphs with same WorkContractGroup") {
        std::cout << "\n=== Starting Two graphs same group test ===\n";
        
        WorkContractGroup group(4);
        std::atomic<int> totalCounter{0};
        
        // First graph
        {
            std::cout << "Creating first graph\n";
            WorkGraphConfig config;
            config.enableDebugLogging = true;
            
            WorkGraph graph(&group, config);
            
            auto n1 = graph.addNode([&totalCounter]() { 
                std::cout << "Graph1 Node1 executing\n";
                totalCounter++; 
            }, "n1");
            
            graph.execute();
            
            // Drive execution
            while (!graph.isComplete()) {
                group.executeAllBackgroundWork();
            }
            
            std::cout << "First graph complete, totalCounter=" << totalCounter << "\n";
            REQUIRE(totalCounter == 1);
            std::cout << "First graph object going out of scope\n";
        }
        
        // Second graph using SAME group
        {
            std::cout << "\nCreating second graph with same group\n";
            WorkGraphConfig config;
            config.enableDebugLogging = true;
            
            WorkGraph graph(&group, config);
            
            auto n1 = graph.addNode([&totalCounter]() { 
                std::cout << "Graph2 Node1 executing\n";
                totalCounter++; 
            }, "n1");
            
            graph.execute();
            
            // Drive execution
            while (!graph.isComplete()) {
                group.executeAllBackgroundWork();
            }
            
            std::cout << "Second graph complete, totalCounter=" << totalCounter << "\n";
            REQUIRE(totalCounter == 2);
            std::cout << "Second graph object going out of scope\n";
        }
        
        std::cout << "=== Two graphs same group test complete ===\n\n";
    }
}