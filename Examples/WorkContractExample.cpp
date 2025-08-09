//
// Created by Geenz on 8/8/25.
//


#include "WorkContractExample.h"
#include <iostream>
#include "../src/Concurrency/WorkContractGroup.h"
#include "../src/Concurrency/WorkService.h"
using namespace EntropyEngine;
using namespace Core;
using namespace Concurrency;

int main() {
    WorkService service{WorkService::Config()};
    service.start();
    WorkContractGroup group(1000);

    for (int i = 0; i < 1000; i++) {
        group.createContract([=]() {
            printf("WorkContractGroup createContract %i\n", i);
        }).schedule();
    }
    service.addWorkContractGroup(&group);
    group.wait();

    service.stop();
    return 0;
}
