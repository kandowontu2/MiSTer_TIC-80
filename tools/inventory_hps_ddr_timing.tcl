# Read-only fitted-model investigation. This does not remove timing arcs or
# waive a clock crossing. Run from the exact fitted candidate directory.
set output [lindex $quartus(args) 0]
file mkdir $output
project_open TIC80
foreach corner {{slow -40} {slow 100} {fast -40} {fast 100}} {
    lassign $corner model temperature
    create_timing_netlist -model $model -temperature $temperature -voltage 1100
    read_sdc
    update_timing_netlist
    set tag "$model-$temperature"
    set sources {}
    set f [open "$output/$tag-selected-model-edges.tsv" w]
    puts $f "register\tclock_pin\toutput_pin"
    foreach_in_collection node [get_registers {*|f2sdram~FF_*}] {
        set clocks {}
        foreach edge [get_node_info -clock_edges $node] {
            lappend clocks [get_node_info -name [get_edge_info -src $edge]]
        }
        foreach edge [get_node_info -fanout_edges $node] {
            set pin [get_node_info -name [get_edge_info -dst $edge]]
            # Existing 128/64/64 port configuration: custom ram1 uses FIFO2,
            # command port1. Preserve the other model arcs in the netlist.
            set selected 0
            if {$clocks eq "sysmem|fpga_interfaces|f2sdram|rd_clk_2" &&
                ([string first {sysmem|fpga_interfaces|f2sdram|rd_data_2[} $pin] == 0 || [string match {*|rd_valid_2} $pin])} {
                set selected 1
            }
            if {[string match {*|cmd_ready_1} $pin] &&
                ($clocks eq "sysmem|fpga_interfaces|f2sdram|cmd_port_clk_1" ||
                 $clocks eq "sysmem|fpga_interfaces|f2sdram|wr_clk_2")} {
                set selected 1
            }
            if {$selected} {
                lappend sources [get_node_info -name $node]
                puts $f "[get_node_info -name $node]\t$clocks\t$pin"
            }
        }
    }
    close $f
    set sources [lsort -unique $sources]
    if {[llength $sources] != 67} {error "Expected 64 data plus 3 control model sources, got [llength $sources]"}
    set selected [get_registers $sources]
    set targets [get_registers {emu|video|reader|*}]
    foreach mode {setup hold} {
        set paths [get_timing_paths -$mode -from $selected -to $targets -npaths 0 -nworst 1]
        if {[get_collection_size $paths] < 100} {error "Too few $tag $mode reader endpoints"}
        set f [open "$output/$tag-$mode.tsv" w]
        puts $f "source\tdestination\tlaunch_clock\tlatch_clock\tslack_ns"
        foreach_in_collection path $paths {
            set launch [get_clock_info -name [get_path_info -from_clock $path]]
            set latch [get_clock_info -name [get_path_info -to_clock $path]]
            if {$launch ne $latch || ![string match {emu|pll|*} $launch]} {
                error "Unexpected clock pair in configured DDR model paths"
            }
            set slack [get_path_info -slack $path]
            if {$slack < 0} {error "Configured DDR $tag $mode violation: $slack"}
            puts $f "[get_node_info -name [get_path_info -from $path]]\t[get_node_info -name [get_path_info -to $path]]\t$launch\t$latch\t$slack"
        }
        close $f
        report_timing -$mode -from $selected -to $targets -npaths 5 -detail full_path -file "$output/$tag-$mode-worst.rpt"
        puts "HPS_DDR_MODEL $tag $mode [get_collection_size $paths]"
    }
    delete_timing_netlist
}
project_close
