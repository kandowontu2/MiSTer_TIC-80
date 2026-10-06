# Run from build/fpga. Confirm the actual fitted reset handoff registers/routes.
project_open TIC80
create_timing_netlist -model slow -temperature -40 -voltage 1100
read_sdc
update_timing_netlist
set clock [get_clocks {emu|pll|pll_inst|altera_pll_i|general[0].gpll~PLL_OUTPUT_COUNTER|divclk}]
if {[get_collection_size $clock] != 1} { error "Missing DDR clock" }
set period [get_clock_info -period $clock]
for {set bit 0} {$bit < 4} {incr bit} {
    set nodes [get_registers [format {*|tic80_video_top:video|reset_sys[%d]*} $bit]]
    if {[get_collection_size $nodes] < 1} { error "Missing fitted core reset stage $bit" }
    foreach_in_collection node $nodes { puts "RESET_CORE_NODE stage=$bit name=[get_register_info -name $node]" }
    if {$bit > 0} {
        set previous [get_registers [format {*|tic80_video_top:video|reset_sys[%d]*} [expr {$bit-1}]]]
        lassign [report_path -from $previous -to $nodes -npaths 0 -pairs_only -summary -file [format {../ddr-reset-stage-%d.rpt} $bit]] count delay
        if {$count < [get_collection_size $nodes] || $delay <= 0 || $delay >= $period * 0.9} {
            error "Invalid reset pipeline stage $bit: paths=$count delay=$delay period=$period"
        }
        puts "RESET_PIPELINE_PASS stage=$bit paths=$count delay_ns=$delay period_ns=$period"
    }
}
set ram0 [get_registers {*|ram1_reset_0*}]
set ram1 [get_registers {*|ram1_reset_1*}]
if {[get_collection_size $ram0] != 1 || [get_collection_size $ram1] != 1} { error "Changed platform RAM1 reset synchronization" }
lassign [report_path -from $ram0 -to $ram1 -npaths 0 -pairs_only -summary -file ../ddr-reset-platform.rpt] count delay
if {$count != 1 || $delay <= 0 || $delay >= $period * 0.9} { error "Invalid platform reset route" }
puts "RESET_PLATFORM_PASS stages=2 paths=$count delay_ns=$delay"
report_timing -setup -from [get_registers {*|tic80_video_top:video|reset_sys*}] -to [get_registers {*|tic80_video_top:video|reset_sys*}] -npaths 20 -detail full_path -file ../ddr-reset-pipeline-timing.rpt
report_timing -setup -from $ram0 -to $ram1 -npaths 2 -detail full_path -file ../ddr-reset-platform-timing.rpt
delete_timing_netlist
project_close
