/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 Jonathan "Geenz" Goodman
 * This file is part of the Entropy Core project.
 */

/**
 * @file AcyclicNodeHandle.h
 * @brief Safe handles for DAG nodes with generation-based validation
 * 
 * This file provides AcyclicNodeHandle for safe references to nodes in DirectedAcyclicGraph.
 * Uses generation counting to detect stale handles when memory is reused.
 */

#pragma once
#include "../TypeSystem/GenericHandle.h"

namespace EntropyEngine {
    namespace Core {
        namespace Graph {
            template<class T>
            class DirectedAcyclicGraph;
            
            /**
             * @brief Tag for compile-time handle type safety
             */
            struct NodeTag {};

            /**
             * @brief Safe handle to nodes in a DirectedAcyclicGraph
             *
             * Provides safe node access through index and generation counting. Handles
             * automatically become invalid when their target nodes are deleted, preventing
             * use-after-free errors. Always verify with `isValid()` before use.
             *
             * @tparam T The type of data stored within the nodes of the graph
             */
            template<class T>
            class AcyclicNodeHandle : public TypeSystem::TypedHandle<NodeTag, DirectedAcyclicGraph<T>> {
                using BaseHandle = TypeSystem::TypedHandle<NodeTag, DirectedAcyclicGraph<T>>;
                
                template<typename U>
                friend class DirectedAcyclicGraph;

            public:
                /**
                 * @brief Constructs an AcyclicNodeHandle with specific graph, index, and generation
                 * 
                 * Used internally by DirectedAcyclicGraph. The generation acts as a version
                 * number that increments when slots are reused.
                 * 
                 * @param graph The graph that owns this node
                 * @param index The slot index where the node resides
                 * @param generation The generation count for validity checking
                 */
                AcyclicNodeHandle(DirectedAcyclicGraph<T>* graph, uint32_t index, uint32_t generation):
                    BaseHandle(graph, index, generation) {};
                /**
                 * @brief Constructs an invalid AcyclicNodeHandle
                 *
                 * Creates a handle that doesn't reference any valid node. Useful for default
                 * initialization, "not found" results, and placeholders.
                 */
                AcyclicNodeHandle() : BaseHandle() {}

                /**
                 * @brief Adds a new child node to the graph
                 *
                 * Creates a new node and establishes an edge from this node to the child.
                 * The graph enforces acyclic constraints and throws if a cycle would form.
                 *
                 * @param data The data to store in the new child node (moved)
                 * @return Handle to the newly created child node
                 * @throws std::invalid_argument If adding the edge would create a cycle
                 *
                 * @code
                 * // Building a task dependency graph
                 * auto compileTask = graph.addNode(Task{"Compile", Priority::High});
                 * auto linkTask = compileTask.addChild(Task{"Link", Priority::Medium});
                 * auto packageTask = linkTask.addChild(Task{"Package", Priority::Low});
                 * 
                 * // Graph structure: Compile -> Link -> Package
                 * // Each task depends on its parent completing first
                 * @endcode
                 */
                AcyclicNodeHandle<T> addChild(T data);

                /**
                 * @brief Gets mutable pointer to node data
                 *
                 * Returns nullptr if handle is invalid. Thread safety is the caller's
                 * responsibility when modifying data from multiple threads.
                 *
                 * @return Mutable pointer to the node's data, or nullptr if invalid
                 *
                 * @code
                 * // Safe pattern for modifying node data
                 * if (auto* nodeData = taskHandle.getData()) {
                 *     // Handle is valid - safe to modify
                 *     nodeData->status = TaskStatus::Running;
                 *     nodeData->startTime = std::chrono::steady_clock::now();
                 *     LOG_INFO("Started task: {}", nodeData->name);
                 * } else {
                 *     // Handle is invalid - node was deleted
                 *     LOG_WARN("Attempted to access deleted task node");
                 * }
                 * @endcode
                 */
                T* getData();

                /**
                 * @brief Gets const pointer to node data
                 *
                 * Provides read-only access. Suitable for const contexts and when
                 * modification protection is needed. Returns nullptr if invalid.
                 *
                 * @return Const pointer to the node's data, or nullptr if invalid
                 *
                 * @code
                 * // Const-correct task information display
                 * void printTaskInfo(const AcyclicNodeHandle<Task>& taskHandle) {
                 *     if (const auto* task = taskHandle.getData()) {
                 *         std::cout << "Task: " << task->name 
                 *                   << " Priority: " << task->priority
                 *                   << " Status: " << task->status << std::endl;
                 *     } else {
                 *         std::cout << "[Invalid task handle]" << std::endl;
                 *     }
                 * }
                 * @endcode
                 */
                const T* getData() const;
            };
        }
    }
}

