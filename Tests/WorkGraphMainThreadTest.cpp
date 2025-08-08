/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 Jonathan "Geenz" Goodman
 * This file is part of the Entropy Core project.
 */

#include <catch2/catch_test_macros.hpp>
#include "../src/Concurrency/WorkGraph.h"
#include "../src/Concurrency/WorkService.h"
#include <atomic>
#include <thread>
#include <vector>

using namespace EntropyEngine::Core::Concurrency;

TEST_CASE("WorkGraph main thread execution", "[workgraph][mainthread]") {
    SECTION("Basic main thread node execution") {
        WorkContractGroup group(10);
        WorkGraph graph(&group);
        
        std::atomic<bool> mainThreadExecuted{false};
        std::atomic<bool> anyThreadExecuted{false};
        
        // Create a regular node
        auto regularNode = graph.addNode([&anyThreadExecuted]() {
            anyThreadExecuted = true;
        }, "regular-node");
        
        // Create a main thread node
        auto mainThreadNode = graph.addNode([&mainThreadExecuted]() {
            mainThreadExecuted = true;
        }, "main-thread-node", nullptr, ExecutionType::MainThread);
        
        // Execute the graph
        graph.execute();
        
        // Execute regular work
        group.executeAllBackgroundWork();
        REQUIRE(anyThreadExecuted == true);
        REQUIRE(mainThreadExecuted == false);
        
        // Execute main thread work
        size_t count = group.executeAllMainThreadWork();
        REQUIRE(count == 1);
        REQUIRE(mainThreadExecuted == true);
        
        // Wait for completion
        auto result = graph.wait();
        REQUIRE(result.allCompleted == true);
    }
    
    SECTION("Mixed dependencies - main thread depends on regular") {
        WorkContractGroup group(10);
        WorkGraph graph(&group);
        
        std::atomic<int> executionOrder{0};
        int regularOrder = 0;
        int mainThreadOrder = 0;
        
        // Create a regular node
        auto regularNode = graph.addNode([&executionOrder, &regularOrder]() {
            regularOrder = ++executionOrder;
        }, "regular-node");
        
        // Create a main thread node that depends on the regular node
        auto mainThreadNode = graph.addNode([&executionOrder, &mainThreadOrder]() {
            mainThreadOrder = ++executionOrder;
        }, "main-thread-node", nullptr, ExecutionType::MainThread);
        
        graph.addDependency(regularNode, mainThreadNode);
        
        // Execute the graph
        graph.execute();
        
        // Try to execute main thread work first - shouldn't execute anything
        size_t count = group.executeAllMainThreadWork();
        REQUIRE(count == 0);
        REQUIRE(mainThreadOrder == 0);
        
        // Execute regular work
        group.executeAllBackgroundWork();
        REQUIRE(regularOrder == 1);
        
        // Now main thread work should be ready
        count = group.executeAllMainThreadWork();
        REQUIRE(count == 1);
        REQUIRE(mainThreadOrder == 2);
        
        // Wait for completion
        auto result = graph.wait();
        REQUIRE(result.allCompleted == true);
    }
    
    SECTION("Mixed dependencies - regular depends on main thread") {
        WorkContractGroup group(10);
        WorkGraph graph(&group);
        
        std::atomic<int> executionOrder{0};
        int mainThreadOrder = 0;
        int regularOrder = 0;
        
        // Create a main thread node
        auto mainThreadNode = graph.addNode([&executionOrder, &mainThreadOrder]() {
            mainThreadOrder = ++executionOrder;
        }, "main-thread-node", nullptr, ExecutionType::MainThread);
        
        // Create a regular node that depends on the main thread node
        auto regularNode = graph.addNode([&executionOrder, &regularOrder]() {
            regularOrder = ++executionOrder;
        }, "regular-node");
        
        graph.addDependency(mainThreadNode, regularNode);
        
        // Execute the graph
        graph.execute();
        
        // Try to execute regular work first - shouldn't execute anything
        group.executeAllBackgroundWork();
        REQUIRE(regularOrder == 0);
        
        // Execute main thread work
        size_t count = group.executeAllMainThreadWork();
        REQUIRE(count == 1);
        REQUIRE(mainThreadOrder == 1);
        
        // Now regular work should be ready
        group.executeAllBackgroundWork();
        REQUIRE(regularOrder == 2);
        
        // Wait for completion
        auto result = graph.wait();
        REQUIRE(result.allCompleted == true);
    }
    
    SECTION("Complex mixed execution pipeline") {
        WorkContractGroup group(20);
        WorkGraph graph(&group);
        
        std::vector<int> executionOrder;
        std::mutex orderMutex;
        
        // Create a complex pipeline:
        // load1, load2 (any thread) -> process (main thread) -> save1, save2 (any thread)
        
        auto load1 = graph.addNode([&executionOrder, &orderMutex]() {
            std::lock_guard<std::mutex> lock(orderMutex);
            executionOrder.push_back(1);
        }, "load1");
        
        auto load2 = graph.addNode([&executionOrder, &orderMutex]() {
            std::lock_guard<std::mutex> lock(orderMutex);
            executionOrder.push_back(2);
        }, "load2");
        
        auto process = graph.addNode([&executionOrder, &orderMutex]() {
            std::lock_guard<std::mutex> lock(orderMutex);
            executionOrder.push_back(3);
        }, "process", nullptr, ExecutionType::MainThread);
        
        auto save1 = graph.addNode([&executionOrder, &orderMutex]() {
            std::lock_guard<std::mutex> lock(orderMutex);
            executionOrder.push_back(4);
        }, "save1");
        
        auto save2 = graph.addNode([&executionOrder, &orderMutex]() {
            std::lock_guard<std::mutex> lock(orderMutex);
            executionOrder.push_back(5);
        }, "save2");
        
        // Set up dependencies
        graph.addDependency(load1, process);
        graph.addDependency(load2, process);
        graph.addDependency(process, save1);
        graph.addDependency(process, save2);
        
        // Execute the graph
        graph.execute();
        
        // Execute loads
        group.executeAllBackgroundWork();
        REQUIRE(executionOrder.size() == 2);
        REQUIRE((executionOrder[0] == 1 || executionOrder[0] == 2));
        REQUIRE((executionOrder[1] == 1 || executionOrder[1] == 2));
        REQUIRE(executionOrder[0] != executionOrder[1]);
        
        // Execute process on main thread
        size_t count = group.executeAllMainThreadWork();
        REQUIRE(count == 1);
        REQUIRE(executionOrder.size() == 3);
        REQUIRE(executionOrder[2] == 3);
        
        // Execute saves
        group.executeAllBackgroundWork();
        REQUIRE(executionOrder.size() == 5);
        REQUIRE((executionOrder[3] == 4 || executionOrder[3] == 5));
        REQUIRE((executionOrder[4] == 4 || executionOrder[4] == 5));
        REQUIRE(executionOrder[3] != executionOrder[4]);
        
        // Wait for completion
        auto result = graph.wait();
        REQUIRE(result.allCompleted == true);
    }
    
    SECTION("WorkService integration with mixed execution") {
        WorkService::Config config;
        config.threadCount = 2;
        WorkService service(config);
        
        WorkContractGroup group(10);
        service.addWorkContractGroup(&group);
        
        WorkGraph graph(&group);
        
        std::atomic<int> regularCount{0};
        std::atomic<int> mainThreadCount{0};
        
        // Create mixed nodes
        for (int i = 0; i < 5; ++i) {
            graph.addNode([&regularCount]() {
                regularCount++;
            }, "regular-" + std::to_string(i));
            
            graph.addNode([&mainThreadCount]() {
                mainThreadCount++;
            }, "main-" + std::to_string(i), nullptr, ExecutionType::MainThread);
        }
        
        // Execute the graph
        graph.execute();
        
        // Start the service to process regular work
        service.start();
        
        // Let worker threads process regular work
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        // Check that regular work is done but main thread work is pending
        REQUIRE(regularCount == 5);
        REQUIRE(mainThreadCount == 0);
        REQUIRE(service.hasMainThreadWork() == true);
        
        // Execute main thread work
        auto result = service.executeMainThreadWork();
        REQUIRE(result.contractsExecuted == 5);
        REQUIRE(mainThreadCount == 5);
        
        // Stop the service
        service.stop();
        
        // Wait for completion
        auto graphResult = graph.wait();
        REQUIRE(graphResult.allCompleted == true);
        
        service.removeWorkContractGroup(&group);
    }
    
    SECTION("Continuation with main thread execution") {
        WorkContractGroup group(10);
        WorkGraph graph(&group);
        
        std::atomic<bool> part1Done{false};
        std::atomic<bool> part2Done{false};
        std::atomic<bool> mergedOnMainThread{false};
        
        // Create two parallel tasks
        auto part1 = graph.addNode([&part1Done]() {
            part1Done = true;
        }, "part1");
        
        auto part2 = graph.addNode([&part2Done]() {
            part2Done = true;
        }, "part2");
        
        // Create a main thread continuation that depends on both
        auto merge = graph.addContinuation(
            {part1, part2},
            [&mergedOnMainThread, &part1Done, &part2Done]() {
                // Both parts should be done before merge runs
                REQUIRE(part1Done == true);
                REQUIRE(part2Done == true);
                mergedOnMainThread = true;
            },
            "merge",
            ExecutionType::MainThread
        );
        
        // Execute the graph
        graph.execute();
        
        // Execute parallel parts
        group.executeAllBackgroundWork();
        REQUIRE(part1Done == true);
        REQUIRE(part2Done == true);
        REQUIRE(mergedOnMainThread == false);
        
        // Execute main thread merge
        size_t count = group.executeAllMainThreadWork();
        REQUIRE(count == 1);
        REQUIRE(mergedOnMainThread == true);
        
        // Wait for completion
        auto result = graph.wait();
        REQUIRE(result.allCompleted == true);
    }
}