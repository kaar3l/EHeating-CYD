#pragma once
#include <stdbool.h>

void relay_init(void);
void relay_set1(bool on);
void relay_set2(bool on);
void relay_evaluate(void);  // called periodically; applies control logic
