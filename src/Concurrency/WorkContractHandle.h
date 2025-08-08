/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 Jonathan "Geenz" Goodman
 * This file is part of the Entropy Core project.
 */

/**
 * @file WorkContractHandle.h
 * @brief Type-safe handle for managing work contract lifecycle and scheduling
 * 
 * This file provides a safe interface for scheduling and monitoring work contracts
 * using generation-based validation to prevent use-after-free errors.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include "../TypeSystem/GenericHandle.h"

namespace EntropyEngine {
namespace Core {
namespace Concurrency {

    // Forward declaration
    class WorkContractGroup;
    
    // Tag type for work contract handles
    struct WorkContractTag {};

    /**
     * @brief States that a work contract can be in during its lifecycle
     */
    enum class ContractState : uint32_t {
        Free = 0,       ///< Contract slot is available for allocation
        Allocated = 1,  ///< Contract has been allocated but not scheduled
        Scheduled = 2,  ///< Contract is scheduled and ready for execution
        Executing = 3,  ///< Contract is currently being executed
        Completed = 4   ///< Contract has completed execution
    };

    /**
     * @brief Result of schedule/unschedule operations
     */
    enum class ScheduleResult {
        Scheduled,      ///< Contract is now scheduled (successful schedule operation)
        AlreadyScheduled, ///< Contract was already scheduled (schedule operation failed)
        NotScheduled,   ///< Contract is not scheduled (successful unschedule operation)
        Executing,      ///< Cannot modify - currently executing
        Invalid         ///< Invalid handle provided
    };

    /**
     * @class WorkContractHandle
     * @brief Type-safe handle for scheduling and managing work contracts
     * 
     * WorkContractHandle provides a type-safe interface for managing work contract
     * lifecycle. It enables reliable task scheduling, status monitoring, and lifecycle
     * management while preventing race conditions and use-after-free errors through
     * generation-based validation.
     * 
     * Suitable applications include:
     * - Job systems requiring reliable work scheduling
     * - Task graphs with controlled execution timing
     * - Systems requiring deferred work execution with optional cancellation
     * 
     * The handle implements generation-based validation to accurately report work
     * validity and execution status. This mechanism ensures safe operation without
     * unexpected behavior or memory errors.
     * 
     * Key features:
     * - Streamlined API: schedule(), unschedule(), valid(), release()
     * - Automatic invalidation upon execution start (prevents double-execution)
     * - Generation-based validation prevents use-after-free
     * - Lightweight and copyable for flexible usage
     * 
     * Typical workflow:
     * 1. Create work via WorkContractGroup::createContract()
     * 2. Schedule it with handle.schedule() when ready
     * 3. Optionally unschedule with handle.unschedule() if plans change
     * 4. Handle becomes invalid once execution starts - automatic cleanup
     * 
     * @code
     * WorkContractGroup group(1024);
     * 
     * // Create some work
     * auto handle = group.createContract([]() {
     *     processImportantData();
     * });
     * 
     * // Schedule it when ready
     * if (handle.schedule() == ScheduleResult::Scheduled) {
     *     std::cout << "Work is now scheduled for execution\n";
     * }
     * 
     * // Maybe change your mind?
     * if (handle.unschedule() == ScheduleResult::NotScheduled) {
     *     std::cout << "Cancelled before execution\n";
     * }
     * 
     * // Always safe to check
     * if (handle.valid()) {
     *     // Handle still points to valid work
     * }
     * @endcode
     */
    class WorkContractHandle : public TypeSystem::TypedHandle<WorkContractTag, WorkContractGroup> {
    private:
        using BaseHandle = TypeSystem::TypedHandle<WorkContractTag, WorkContractGroup>;
        
        friend class WorkContractGroup;
        
        /**
         * @brief Private constructor for creating valid handles
         * 
         * Only WorkContractGroup can create valid handles. Always use
         * WorkContractGroup::createContract() to obtain handles.
         * 
         * @param group Pointer to the WorkContractGroup that owns this contract
         * @param index Slot index within the group's contract array
         * @param generation Current generation of the slot for validation
         */
        WorkContractHandle(WorkContractGroup* group, uint32_t index, uint32_t generation)
            : BaseHandle(group, index, generation) {}
            
    public:
        /**
         * @brief Default constructor creates an invalid handle
         * 
         * Creates a handle that doesn't point to any work. Check valid() before use.
         */
        WorkContractHandle() : BaseHandle() {}
        
        /**
         * @brief Schedules this contract for execution
         * 
         * Marks work as ready for execution. Work runs as soon as an executor
         * becomes available.
         * 
         * @return ScheduleResult indicating outcome:
         *         - Scheduled: Successfully added to execution list
         *         - AlreadyScheduled: Already in the execution list
         *         - Executing: Currently running
         *         - Invalid: Handle doesn't point to valid work
         * 
         * @code
         * auto handle = group.createContract([]() { doWork(); });
         * 
         * auto result = handle.schedule();
         * if (result == ScheduleResult::Scheduled) {
         *     // Work is now in line for execution
         * } else if (result == ScheduleResult::AlreadyScheduled) {
         *     // Someone else already scheduled it - that's fine
         * }
         * @endcode
         */
        ScheduleResult schedule();
        
        /**
         * @brief Attempts to remove this contract from the execution list
         * 
         * Cancels scheduled work before execution begins. Already executing work
         * cannot be cancelled. Atomic and lock-free operation.
         * 
         * @return ScheduleResult indicating outcome:
         *         - NotScheduled: Successfully removed
         *         - Executing: Too late - already running
         *         - Invalid: Handle doesn't point to valid work
         * 
         * @code
         * auto handle = group.createContract([]() { expensiveWork(); });
         * handle.schedule();
         * 
         * // Oh wait, we don't need this anymore
         * if (handle.unschedule() == ScheduleResult::NotScheduled) {
         *     std::cout << "Cancelled before execution - saved some CPU time\n";
         * } else {
         *     std::cout << "Too late to cancel - work is running or done\n";
         * }
         * @endcode
         */
        ScheduleResult unschedule();
        
        /**
         * @brief Checks if this handle still points to valid work
         * 
         * Handle becomes invalid when work executes, release() is called, or
         * the owning group is destroyed.
         * 
         * @return true if the handle points to valid, accessible work
         * 
         * @code
         * auto handle = group.createContract([]() { doWork(); });
         * 
         * if (handle.valid()) {
         *     // Safe to schedule, unschedule, or check status
         *     handle.schedule();
         * } else {
         *     // Handle is stale - work is gone or already executed
         *     std::cout << "Work is no longer available\n";
         * }
         * @endcode
         */
        bool valid() const;
        
        /**
         * @brief Immediately frees this work contract and cleans up resources
         * 
         * Removes work from execution lists and invalidates handle. If executing,
         * work completes but handle becomes invalid immediately.
         * 
         * @note Destructive - handle cannot be reused after calling this
         * 
         * @code
         * auto handle = group.createContract([]() { expensiveWork(); });
         * handle.schedule();
         * 
         * // Emergency shutdown - cancel everything
         * handle.release();
         * 
         * // Handle is now invalid
         * assert(!handle.valid());
         * @endcode
         */
        void release();
        
        /**
         * @brief Gets the work contract group that owns this contract
         * 
         * Returns the group that created this handle, useful for creating more
         * work or checking statistics.
         * 
         * @return Pointer to owning WorkContractGroup, or nullptr if invalid
         * 
         * @code
         * auto handle = group.createContract([]() { doWork(); });
         * 
         * WorkContractGroup* ownerGroup = handle.getGroup();
         * if (ownerGroup) {
         *     std::cout << "Group has " << ownerGroup->activeCount() << " active contracts\n";
         * }
         * @endcode
         */
        WorkContractGroup* getGroup() const { return getOwner(); }
        
        /**
         * @brief Checks if this work is currently scheduled for execution
         * 
         * Returns true if work is in execution list waiting to run.
         * 
         * @return true if scheduled and waiting, false otherwise
         * 
         * @code
         * auto handle = group.createContract([]() { doWork(); });
         * handle.schedule();
         * 
         * if (handle.isScheduled()) {
         *     std::cout << "Work is waiting in line for execution\n";
         * }
         * @endcode
         */
        bool isScheduled() const;
        
        /**
         * @brief Checks if this work is currently being executed
         * 
         * Returns true if work is actively running. Cannot be scheduled or
         * unscheduled while executing.
         * 
         * @return true if currently running, false otherwise
         * 
         * @code
         * auto handle = group.createContract([]() { longRunningWork(); });
         * handle.schedule();
         * 
         * // Later, from another thread
         * if (handle.isExecuting()) {
         *     std::cout << "Work is running - can't cancel now\n";
         * }
         * @endcode
         */
        bool isExecuting() const;
    };

} // namespace Concurrency
} // namespace Core
} // namespace EntropyEngine

