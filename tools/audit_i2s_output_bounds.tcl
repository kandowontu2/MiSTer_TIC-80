# Package-pin delay inventory, on a copied fitted project only. No fitter or
# exported constraint changes. The Python analyzer states clock/skew assumptions.
set evidence [lindex $argv 0]
if {![file isdirectory $evidence]} {error "Fresh evidence directory missing"}
set periods [file join $evidence clock-periods.tsv]
if {[file exists $periods]} {error "Refusing to overwrite prior clock evidence"}
set f [open $periods {WRONLY CREAT EXCL}]
puts $f "model\ttemperature\tclock\tperiod_ns"
set name {pll_audio|pll_audio_inst|altera_pll_i|general[0].gpll~PLL_OUTPUT_COUNTER|divclk}
project_open TIC80
foreach model {slow fast} {
    foreach temperature {-40 100} {
        set report [file join $evidence rawio-$model-$temperature.rpt]
        if {[file exists $report]} {error "Refusing to overwrite prior pin-delay evidence"}
        create_timing_netlist -model $model -temperature $temperature -voltage 1100
        read_sdc
        update_timing_netlist
        set found 0
        foreach_in_collection clock [get_clocks *] {
            if {[get_clock_info -name $clock] eq $name} {
                incr found
                puts $f "$model\t$temperature\t$name\t[get_clock_info -period $clock]"
            }
        }
        if {$found != 1} {error "Expected one exact serializer clock"}
        report_datasheet -expand_bus -file $report
        delete_timing_netlist
    }
}
close $f
project_close -dont_export_assignments
puts "I2S_PACKAGE_PIN_BOUND_INVENTORY_COMPLETE"
