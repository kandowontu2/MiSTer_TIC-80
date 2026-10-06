# Conditional package-pin HDMI timing. Run on a copied fitted project only.
# The overlay is in memory: no SDC export, fitter run or hardware writes.
# These Rev. B receiver numbers assume zero board skew and no extra receiver
# clock delay. They do not qualify an actual board or every dynamic PLL mode.
set evidence [lindex $argv 0]
if {![file isdirectory $evidence]} {error "Fresh evidence directory required"}
set table [file join $evidence paths.tsv]
if {[file exists $table]} {error "Refusing to overwrite timing evidence"}
set f [open $table {WRONLY CREAT EXCL}]
puts $f [join {model temperature branch check endpoint launch_clock capture_clock period inverted waveform slack relationship launch_time latch_time arrival required} "\t"]

proc named_clock {name} {
    foreach_in_collection c [get_clocks *] {
        if {[get_clock_info -name $c] eq $name} {return $c}
    }
    error "Required fitted clock absent: $name"
}

set masters [list \
    [list hdmi {pll_hdmi|pll_hdmi_inst|altera_pll_i|cyclonev_pll|counter[0].output_counter|divclk}] \
    [list direct {pll_audio|pll_audio_inst|altera_pll_i|general[0].gpll~PLL_OUTPUT_COUNTER|divclk}]]
set names {HDMI_TX_HS HDMI_TX_VS HDMI_TX_DE}
for {set n 0} {$n < 24} {incr n} {lappend names [format {HDMI_TX_D[%d]} $n]}

project_open TIC80
foreach model {slow fast} {
    foreach temperature {-40 100} {
        create_timing_netlist -model $model -temperature $temperature -voltage 1100
        read_sdc
        set ports [get_ports {HDMI_TX_D[*] HDMI_TX_HS HDMI_TX_VS HDMI_TX_DE}]
        if {[get_collection_size $ports] != 27} {error "Expected exactly 27 HDMI video outputs"}
        foreach pair $masters {
            lassign $pair branch name
            set master [named_clock $name]
            set source [get_clock_info -targets $master]
            if {[get_collection_size $source] != 1} {error "Ambiguous clock source: $name"}
            # datain_h=0, datain_l=1: forwarded rising edge is internal falling.
            set forwarded tm_hdmi_forwarded_$branch
            create_generated_clock -name $forwarded -add -master_clock $name \
                -source $source -divide_by 1 -invert [get_ports HDMI_TX_CLK]
            set_output_delay -clock $forwarded -max 1.8 -add_delay $ports
            set_output_delay -clock $forwarded -min -1.3 -add_delay $ports
        }
        derive_clock_uncertainty
        update_timing_netlist
        report_clocks -file [file join $evidence clocks-$model-$temperature.rpt]
        foreach pair $masters {
            lassign $pair branch name
            set master [named_clock $name]
            set forwarded tm_hdmi_forwarded_$branch
            set capture [named_clock $forwarded]
            foreach check {setup hold} {
                report_timing -$check -from_clock [get_clocks $name] -to_clock [get_clocks $forwarded] \
                    -to $ports -npaths 27 -nworst 1 -detail full_path \
                    -file [file join $evidence $branch-$check-$model-$temperature.rpt]
                foreach_in_collection port $ports {
                    set paths [get_timing_paths -$check -from_clock [get_clocks $name] -to_clock [get_clocks $forwarded] \
                        -to $port -npaths 1 -detail full_path]
                    if {[get_collection_size $paths] != 1} {
                        error "Missing $branch $check path to [get_node_info -name $port]"
                    }
                    foreach_in_collection path $paths {
                        set row [list $model $temperature $branch $check \
                            [get_node_info -name [get_path_info -to $path]] \
                            [get_clock_info -name [get_path_info -from_clock $path]] \
                            [get_clock_info -name [get_path_info -to_clock $path]] \
                            [get_clock_info -period $capture] \
                            [get_clock_info -is_inverted $capture] \
                            [get_clock_info -waveform $capture]]
                        foreach field {slack clock_relationship launch_time latch_time arrival_time required_time} {
                            lappend row [get_path_info -$field $path]
                        }
                        puts $f [join $row "\t"]
                    }
                }
            }
        }
        flush $f
        delete_timing_netlist
    }
}
close $f
project_close -dont_export_assignments
puts "CONDITIONAL_HDMI_OUTPUT_REPORTS_COMPLETE"
