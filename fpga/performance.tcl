# Performance settings from the pinned MiSTer PICO-8 project. Keep multicorner
# timing analysis enabled; do not suppress a cold-corner violation.
set_global_assignment -name ALLOW_POWER_UP_DONT_CARE ON
set_global_assignment -name QII_AUTO_PACKED_REGISTERS "SPARSE AUTO"
set_global_assignment -name ROUTER_LCELL_INSERTION_AND_LOGIC_DUPLICATION ON
set_global_assignment -name PHYSICAL_SYNTHESIS_COMBO_LOGIC ON
set_global_assignment -name PHYSICAL_SYNTHESIS_REGISTER_DUPLICATION ON
set_global_assignment -name PHYSICAL_SYNTHESIS_REGISTER_RETIMING ON
set_global_assignment -name OPTIMIZATION_TECHNIQUE SPEED
set_global_assignment -name MUX_RESTRUCTURE ON
set_global_assignment -name REMOVE_REDUNDANT_LOGIC_CELLS ON
set_global_assignment -name AUTO_DELAY_CHAINS_FOR_HIGH_FANOUT_INPUT_PINS ON
set_global_assignment -name PHYSICAL_SYNTHESIS_COMBO_LOGIC_FOR_AREA ON
set_global_assignment -name ADV_NETLIST_OPT_SYNTH_WYSIWYG_REMAP ON
set_global_assignment -name SYNTH_GATED_CLOCK_CONVERSION ON
set_global_assignment -name PRE_MAPPING_RESYNTHESIS ON
set_global_assignment -name ROUTER_CLOCKING_TOPOLOGY_ANALYSIS ON
set_global_assignment -name ECO_OPTIMIZE_TIMING ON
set_global_assignment -name PERIPHERY_TO_CORE_PLACEMENT_AND_ROUTING_OPTIMIZATION ON
set_global_assignment -name PHYSICAL_SYNTHESIS_ASYNCHRONOUS_SIGNAL_PIPELINING ON
set_global_assignment -name ALM_REGISTER_PACKING_EFFORT LOW
set_global_assignment -name OPTIMIZE_POWER_DURING_SYNTHESIS OFF
set_global_assignment -name FITTER_AGGRESSIVE_ROUTABILITY_OPTIMIZATION ALWAYS

# The rejected explicit-synchronizer fit routes the scaler vertical counter's
# terminal comparison through a slow LAB synchronous-clear input. Let the
# compiler implement this counter's clear/load logic in its data path instead;
# no timing exception or HDL behavior changes. Recheck the actual fitted path.
for {set bit 0} {$bit < 12} {incr bit} {
    set_instance_assignment -name ALLOW_SYNCH_CTRL_USAGE OFF -to "ascal|o_vcpt_pre3\[$bit\]"
}
