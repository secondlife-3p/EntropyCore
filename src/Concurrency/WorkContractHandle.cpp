/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 Jonathan "Geenz" Goodman
 * This file is part of the Entropy Core project.
 */

#include "WorkContractHandle.h"
#include "WorkContractGroup.h"

namespace EntropyEngine {
namespace Core {
namespace Concurrency {

    ScheduleResult WorkContractHandle::schedule() {
        if (!getGroup()) return ScheduleResult::Invalid;
        return getGroup()->scheduleContract(*this);
    }
    
    ScheduleResult WorkContractHandle::unschedule() {
        if (!getGroup()) return ScheduleResult::Invalid;
        return getGroup()->unscheduleContract(*this);
    }
    
    bool WorkContractHandle::valid() const {
        return getGroup() && getGroup()->isValidHandle(*this);
    }
    
    void WorkContractHandle::release() {
        if (getGroup()) {
            getGroup()->releaseContract(*this);
        }
    }
    
    bool WorkContractHandle::isScheduled() const {
        if (!getGroup()) return false;
        
        ContractState state = getGroup()->getContractState(*this);
        return state == ContractState::Scheduled;
    }
    
    bool WorkContractHandle::isExecuting() const {
        if (!getGroup()) return false;
        
        ContractState state = getGroup()->getContractState(*this);
        return state == ContractState::Executing;
    }

} // namespace Concurrency
} // namespace Core
} // namespace EntropyEngine