#include "Vscaler_divider_equivalence.h"
#include "verilated.h"
#include <cstdint>
#include <cstdio>
int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    Vscaler_divider_equivalence model;
    uint64_t comparisons = 0;
    for (unsigned accumulator = 0; accumulator < 16384; ++accumulator) {
        for (unsigned size = 0; size < 4096; ++size) {
            model.accumulator = accumulator;
            model.size = size;
            model.eval();
            if (model.old_remainder != model.new_remainder ||
                model.old_direction != model.new_direction) {
                std::fprintf(stderr,"DIVIDER_MISMATCH accumulator=%u size=%u remainder=%x/%x direction=%x/%x\n",
                    accumulator,size,model.old_remainder,model.new_remainder,model.old_direction,model.new_direction);
                return 1;
            }
            ++comparisons;
        }
    }
    model.final();
    std::printf("SCALER_DIVIDER_EQUIVALENCE_PASS input_pairs=%llu accumulator_values=16384 size_values=4096 remainder_bits=21 direction_bits=12\n",
                static_cast<unsigned long long>(comparisons));
}
