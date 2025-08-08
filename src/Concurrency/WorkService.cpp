//
// Created by Geenz on 7/7/25.
//

#include "WorkService.h"
#include <cmath>
#include <algorithm>
#include <limits>

#include "WorkContractGroup.h"
#include "AdaptiveRankingScheduler.h"

namespace EntropyEngine {
namespace Core {
namespace Concurrency {
    thread_local size_t WorkService::stSoftFailureCount = 0;
    thread_local size_t WorkService::stThreadId = 0;
    thread_local std::atomic<uint64_t>* WorkService::stLocalGeneration = nullptr;
    thread_local std::atomic<uint64_t>* WorkService::stLocalEpoch = nullptr;

    WorkService::WorkService(Config config, std::unique_ptr<IWorkScheduler> scheduler)
        : _config(config)
        , _workContractGroups(new std::vector<WorkContractGroup*>()) {

        // Always clamp to a range of 1 to hardware concurrency.
        if (_config.threadCount == 0) {
            _config.threadCount = std::thread::hardware_concurrency();
        }
        _config.threadCount = std::clamp(_config.threadCount, (uint32_t)1, std::thread::hardware_concurrency());

        // Update scheduler config with thread count
        _config.schedulerConfig.threadCount = _config.threadCount;

        // Create scheduler if not provided
        if (!scheduler) {
            _scheduler = std::make_unique<AdaptiveRankingScheduler>(_config.schedulerConfig);
        } else {
            _scheduler = std::move(scheduler);
        }
    }

    WorkService::~WorkService() {
        stop();
        clear();
        _workContractGroupCount = 0;

        // Clean up the current groups vector
        delete _workContractGroups.load();

        // Clean up any retired vectors
        for (const auto& retired : _retiredVectors) {
            delete retired.vector;
        }

        // Clean up thread generation trackers
        for (auto* gen : _threadGenerations) {
            delete gen;
        }
        for (auto* epoch : _threadEpochs) {
            delete epoch;
        }
    }

    void WorkService::start() {
        if (_running) {
            return; // Already running
        }

        // Pre-allocate generation and epoch trackers for all threads
        {
            std::lock_guard<std::mutex> lock(_threadRegistryMutex);
            // Clean up any existing trackers before clearing
            for (auto* gen : _threadGenerations) {
                delete gen;
            }
            for (auto* epoch : _threadEpochs) {
                delete epoch;
            }
            _threadGenerations.clear();
            _threadGenerations.reserve(_config.threadCount);
            _threadEpochs.clear();
            _threadEpochs.reserve(_config.threadCount);
            for (uint32_t i = 0; i < _config.threadCount; i++) {
                _threadGenerations.push_back(new std::atomic<uint64_t>(0));
                _threadEpochs.push_back(new std::atomic<uint64_t>(0));
            }
        }

        for (uint32_t i = 0; i < _config.threadCount; i++) {
            _threads.emplace_back([this, threadId = i](const std::stop_token& stoken) {
                stThreadId = threadId;
                // Register this thread's generation and epoch trackers
                {
                    std::lock_guard<std::mutex> lock(_threadRegistryMutex);
                    stLocalGeneration = _threadGenerations[threadId];
                    stLocalEpoch = _threadEpochs[threadId];
                }
                executeWork(stoken);
                // Clear thread-local pointers on exit
                stLocalGeneration = nullptr;
                stLocalEpoch = nullptr;
            });
        }

        _running = true;
    }

    void WorkService::requestStop() {
        for (auto &thread : _threads) {
            thread.request_stop();
        }

        // Wake up any threads waiting on the condition variable
        _workAvailable = true;
        _workAvailableCV.notify_all();
    }

    void WorkService::waitForStop() {
        for (auto &thread : _threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }

        _threads.clear();
        _running = false;

        // Reset thread-local state after all threads have stopped
        resetThreadLocalState();
    }

    void WorkService::stop() {
        requestStop();
        waitForStop();
    }

    bool WorkService::isRunning() const {
        return _running;
    }

    void WorkService::clear() {
        // Atomically replace the current groups with a new empty list.
        auto* oldGroups = _workContractGroups.exchange(new std::vector<WorkContractGroup*>());
        retireVector(oldGroups);
        _workContractGroupCount = 0;
        _groupsGeneration.fetch_add(1, std::memory_order_relaxed);

        // Notify scheduler
        _scheduler->notifyGroupsChanged({});
        _scheduler->reset();

        // Reclaim any vectors that are safe to delete
        {
            std::lock_guard<std::mutex> lock(_retireMutex);
            reclaimRetiredVectors();
        }
    }

    WorkService::GroupOperationStatus WorkService::addWorkContractGroup(WorkContractGroup* contractGroup) {
        while (true) {
            const std::vector<WorkContractGroup*>* currentGroups = _workContractGroups.load(std::memory_order_acquire);

            // Check for existence in the current list to prevent duplicates.
            if (std::find(currentGroups->begin(), currentGroups->end(), contractGroup) != currentGroups->end()) {
                return GroupOperationStatus::Exists;
            }

            // Create a new vector with the added group.
            auto* newGroups = new std::vector<WorkContractGroup*>(*currentGroups);
            newGroups->push_back(contractGroup);

            // Try to atomically update the pointer.
            if (_workContractGroups.compare_exchange_weak(currentGroups, newGroups,
                                                          std::memory_order_release,
                                                          std::memory_order_acquire)) {
                // Success - retire the old vector for safe deletion
                retireVector(currentGroups);

                _workContractGroupCount.fetch_add(1, std::memory_order_relaxed);
                _groupsGeneration.fetch_add(1, std::memory_order_relaxed);

                // Notify scheduler of group change
                _scheduler->notifyGroupsChanged(*newGroups);

                // Set ourselves as the concurrency provider for this group
                contractGroup->setConcurrencyProvider(this);

                return GroupOperationStatus::Added;
            }
            // If CAS failed, another thread modified the groups - clean up and retry
            delete newGroups;
        }
    }

    WorkService::GroupOperationStatus WorkService::removeWorkContractGroup(WorkContractGroup* contractGroup) {
        while (true) {
            const std::vector<WorkContractGroup*>* currentGroups = _workContractGroups.load(std::memory_order_acquire);
            auto it = std::find(currentGroups->begin(), currentGroups->end(), contractGroup);

            if (it == currentGroups->end()) {
                return GroupOperationStatus::NotFound;
            }

            // Create a new vector without the removed group.
            auto* newGroups = new std::vector<WorkContractGroup*>();
            newGroups->reserve(currentGroups->size() - 1);
            for (const auto& group : *currentGroups) {
                if (group != contractGroup) {
                    newGroups->push_back(group);
                }
            }

            // Try to atomically update the pointer.
            if (_workContractGroups.compare_exchange_weak(currentGroups, newGroups,
                                                          std::memory_order_release,
                                                          std::memory_order_acquire)) {
                // Success - retire the old vector for safe deletion
                retireVector(currentGroups);

                _workContractGroupCount.fetch_sub(1, std::memory_order_relaxed);
                _groupsGeneration.fetch_add(1, std::memory_order_relaxed);

                // Notify scheduler of group change
                _scheduler->notifyGroupsChanged(*newGroups);

                // Clear the concurrency provider for this group
                contractGroup->setConcurrencyProvider(nullptr);

                return GroupOperationStatus::Removed;
            }
            // If CAS failed, another thread modified the groups - clean up and retry
            delete newGroups;
        }
    }

    size_t WorkService::getWorkContractGroupCount() const {
        return _workContractGroupCount;
    }

    size_t WorkService::getThreadCount() const {
        return _config.threadCount;
    }

    size_t WorkService::getSoftFailureCount() const {
        return _config.maxSoftFailureCount;
    }

    size_t WorkService::setSoftFailureCount(size_t softFailureCount) {
        if (softFailureCount != _config.maxSoftFailureCount) {
            _config.maxSoftFailureCount = softFailureCount;
        }
        return _config.maxSoftFailureCount;
    }

    size_t WorkService::getFailureSleepTime() const {
        return _config.failureSleepTime;
    }

    size_t WorkService::setFailureSleepTime(size_t failureSleepTime) {
        if (failureSleepTime != _config.failureSleepTime) {
            _config.failureSleepTime = failureSleepTime;
        }

        return _config.failureSleepTime;
    }

    void WorkService::executeWork(const std::stop_token& token) {
        WorkContractGroup* lastExecutedGroup = nullptr;

        while (!token.stop_requested()) {
            // Update our local generation and epoch to the current global values
            uint64_t currentGen = _groupsGeneration.load(std::memory_order_acquire);
            uint64_t currentEpoch = _globalEpoch.load(std::memory_order_acquire);
            if (stLocalGeneration) {
                stLocalGeneration->store(currentGen, std::memory_order_release);
            }
            if (stLocalEpoch) {
                stLocalEpoch->store(currentEpoch, std::memory_order_release);
            }

            // Get current snapshot of groups
            const std::vector<WorkContractGroup*>* groupsSnapshot = _workContractGroups.load(std::memory_order_acquire);
            if (!groupsSnapshot || groupsSnapshot->empty()) {
                // Wait on condition variable instead of sleeping
                std::unique_lock<std::mutex> lock(_workAvailableMutex);
                _workAvailableCV.wait_for(lock, std::chrono::milliseconds(1), [this, &token]() {
                    return _workAvailable.load() || token.stop_requested();
                });
                continue;
            }

            // Create scheduling context
            IWorkScheduler::SchedulingContext context{
                stThreadId,
                stSoftFailureCount,
                lastExecutedGroup
            };

            // Ask scheduler for next group
            auto scheduleResult = _scheduler->selectNextGroup(*groupsSnapshot, context);

            if (scheduleResult.group) {
                // Skip stopped/paused groups
                if (scheduleResult.group->isStopping()) {
                    // Group is paused, try another one
                    stSoftFailureCount++;
                    continue;
                }

                // Try to get work from the selected group
                // Double-check the group isn't stopping right before we use it
                if (scheduleResult.group->isStopping()) {
                    stSoftFailureCount++;
                    continue;
                }

                auto contract = scheduleResult.group->selectForExecution();
                if (contract.valid()) {
                    // Check stop token again before executing work to prevent deadlocks during shutdown
                    if (token.stop_requested()) {
                        // Mark contract as completed even though we didn't execute it
                        // This properly transitions it from Executing to Free state
                        scheduleResult.group->completeExecution(contract);
                        break;
                    }

                    // Execute the work
                    scheduleResult.group->executeContract(contract);
                    scheduleResult.group->completeExecution(contract);

                    // Notify scheduler of successful execution
                    _scheduler->notifyWorkExecuted(scheduleResult.group, stThreadId);

                    // Update tracking
                    lastExecutedGroup = scheduleResult.group;
                    stSoftFailureCount = 0;
                    continue;
                }
            }

            // No work found
            if (scheduleResult.shouldSleep || stSoftFailureCount >= _config.maxSoftFailureCount) {
                // Use condition variable for efficient waiting
                std::unique_lock<std::mutex> lock(_workAvailableMutex);
                _workAvailable = false;
                _workAvailableCV.wait_for(lock, std::chrono::milliseconds(10), [this, &token]() {
                    return _workAvailable.load() || token.stop_requested();
                });
                stSoftFailureCount = 0;
            } else {
                stSoftFailureCount++;
                std::this_thread::yield();
            }
        }
    }


    void WorkService::retireVector(const std::vector<WorkContractGroup*>* vec) {
        if (!vec) return;

        uint64_t currentGen = _groupsGeneration.load(std::memory_order_acquire);

        std::lock_guard<std::mutex> lock(_retireMutex);
        _retiredVectors.push_back({vec, currentGen});

        // Opportunistically try to reclaim while we have the lock
        // This prevents unbounded growth of the retire list
        reclaimRetiredVectors();
    }

    void WorkService::notifyWorkAvailable(WorkContractGroup* group) {
        // We don't need to track which group has work, just that work is available
        _workAvailable = true;
        _workAvailableCV.notify_one();
    }

    void WorkService::notifyGroupDestroyed(WorkContractGroup* group) {
        // When a group is destroyed, we should remove it from our list
        // This is important to prevent accessing a destroyed group
        removeWorkContractGroup(group);

        // Only wait for epoch synchronization if the service is running
        // If stopped, threads won't update their epochs and we'd deadlock
        if (_running) {
            // Increment the global epoch to signal that groups have changed
            uint64_t newEpoch = _globalEpoch.fetch_add(1, std::memory_order_acq_rel) + 1;

            // Wait for all threads to observe the new epoch
            // This ensures no thread is holding a pointer to the destroyed group
            bool allThreadsUpdated = false;
            while (!allThreadsUpdated) {
                allThreadsUpdated = true;

                std::lock_guard<std::mutex> lock(_threadRegistryMutex);
                for (const auto* threadEpoch : _threadEpochs) {
                    if (threadEpoch) {
                        uint64_t localEpoch = threadEpoch->load(std::memory_order_acquire);
                        if (localEpoch < newEpoch) {
                            allThreadsUpdated = false;
                            break;
                        }
                    }
                }

                if (!allThreadsUpdated) {
                    // Give threads a chance to update their epoch
                    std::this_thread::yield();
                }
            }
        }
    }

    void WorkService::reclaimRetiredVectors() {
        // Note: This should be called with _retireMutex already held
        // We can only safely delete vectors from generations that all threads have passed

        // Find the minimum generation across all worker threads
        uint64_t minGeneration = std::numeric_limits<uint64_t>::max();

        {
            std::lock_guard<std::mutex> lock(_threadRegistryMutex);
            for (const auto* threadGen : _threadGenerations) {
                if (threadGen) {
                    uint64_t gen = threadGen->load(std::memory_order_acquire);
                    minGeneration = std::min(minGeneration, gen);
                }
            }
        }

        // If no threads are registered or all have very high generations, be conservative
        if (minGeneration == std::numeric_limits<uint64_t>::max() || minGeneration == 0) {
            return;
        }

        // Remove and delete vectors that all threads have moved past
        auto it = std::remove_if(_retiredVectors.begin(), _retiredVectors.end(),
                                [minGeneration](const RetiredVector& retired) {
                                    // Safe to delete if all threads have seen a generation
                                    // newer than when this was retired
                                    if (retired.retiredGeneration < minGeneration) {
                                        delete retired.vector;
                                        return true;
                                    }
                                    return false;
                                });

        _retiredVectors.erase(it, _retiredVectors.end());
    }

    void WorkService::resetThreadLocalState() {
        // This only resets the thread-local state in the calling thread,
        // not in the worker threads. The worker threads reset their own
        // state when they exit in the lambda function in start().
        stSoftFailureCount = 0;
        stThreadId = 0;
        stLocalGeneration = nullptr;
        stLocalEpoch = nullptr;
    }
    
    WorkService::MainThreadWorkResult WorkService::executeMainThreadWork(size_t maxContracts) {
        MainThreadWorkResult result{0, 0, false};
        
        // Get current snapshot of groups
        auto* groups = _workContractGroups.load(std::memory_order_acquire);
        if (!groups) {
            return result;
        }
        
        size_t remaining = maxContracts;
        
        // Execute work from each group that has main thread work
        for (auto* group : *groups) {
            if (group && group->hasMainThreadWork()) {
                result.groupsWithWork++;
                size_t executed = group->executeMainThreadWork(remaining);
                result.contractsExecuted += executed;
                remaining -= executed;
                
                // Stop if we've hit our limit
                if (remaining == 0) {
                    result.moreWorkAvailable = true;
                    break;
                }
            }
        }
        
        // Check if there's more work available
        if (remaining > 0) {
            for (auto* group : *groups) {
                if (group && group->hasMainThreadWork()) {
                    result.moreWorkAvailable = true;
                    break;
                }
            }
        }
        
        return result;
    }
    
    size_t WorkService::executeMainThreadWork(WorkContractGroup* group, size_t maxContracts) {
        if (!group) {
            return 0;
        }
        
        return group->executeMainThreadWork(maxContracts);
    }
    
    bool WorkService::hasMainThreadWork() const {
        auto* groups = _workContractGroups.load(std::memory_order_acquire);
        if (!groups) {
            return false;
        }
        
        for (auto* group : *groups) {
            if (group && group->hasMainThreadWork()) {
                return true;
            }
        }
        
        return false;
    }

} // Concurrency
} // Core
} // EntropyEngine