# Post-fit topology and second-stage timing audit. Run from the fitted stage.
# Gray/control source-to-first-stage routing is checked separately by
# audit_audio_cdc.tcl. Do not replace that audit with this one.
set output [lindex $quartus(args) 0]
if {$output eq ""} {set output ..}
# Use a fresh quartus_sta process per corner. Quartus 17 loses wildcard
# toggle-rate assignments after delete_timing_netlist/recreation within one
# process; the validator requires explicit rates in every emitted report.
set model [lindex $quartus(args) 1]
set temperature [lindex $quartus(args) 2]
if {[lsearch -exact {slow fast} $model] < 0 || [lsearch -exact {-40 100} $temperature] < 0} {
    error "Specify output directory, corner model (slow/fast), and temperature (-40/100)"
}
file mkdir $output
project_open TIC80
set specs {
    {read_pointer {*|tic80_audio:audio|read_sync1*} read_sync1 read_sync2 8}
    {write_pointer {*|tic80_audio:audio|write_sync1*} write_sync1 write_sync2 8}
    {played {*|tic80_audio:audio|played_sync1*} played_sync1 played_sync2 30}
    {samples {*|tic80_audio:audio|samples_sync1*} samples_sync1 samples_sync2 32}
    {underruns {*|tic80_audio:audio|underruns_sync1*} underruns_sync1 underruns_sync2 32}
    {prefill {*|tic80_audio:audio|prefill_sync1} prefill_sync1 prefill_sync2 1}
    {vblank {*|tic80_video_top:video|vblank_sync[0]} {vblank_sync[0]} {vblank_sync[1]} 1}
    {consumed {*|tic80_video_top:video|consumed_sync[0]} {consumed_sync[0]} {consumed_sync[1]} 1}
    {frame {*|tic80_scanout:scanout|toggle_sync[0]} {toggle_sync[0]} {toggle_sync[1]} 1}
    {bank {*|tic80_scanout:scanout|bank_sync[0]} {bank_sync[0]} {bank_sync[1]} 1}
    {valid {*|tic80_scanout:scanout|valid_sync[0]} {valid_sync[0]} {valid_sync[1]} 1}
}
foreach corner [list [list $model $temperature]] {
    lassign $corner model temperature
    create_timing_netlist -model $model -temperature $temperature -voltage 1100
    read_sdc
    update_timing_netlist
    set tag "$model-$temperature"
    set f [open "$output/core-sync-$tag.tsv" w]
    puts $f "label\tfirst_stage\tsecond_stage\tsetup_slack_ns\thold_slack_ns"
    set count 0
    foreach spec $specs {
        lassign $spec label pattern first_name second_name width
        set first [get_registers $pattern]
        if {[get_collection_size $first] != $width} {error "Wrong first-stage width for $label"}
        foreach_in_collection reg $first {
            set name [get_node_info -name $reg]
            set target [string map [list $first_name $second_name] $name]
            set single [get_registers $name]
            set fanouts [get_fanouts $single]
            if {[get_collection_size $fanouts] != 1} {error "First-stage fanout bypass/duplication at $name"}
            foreach_in_collection fanout $fanouts {
                if {[get_node_info -name $fanout] ne $target} {error "First stage $name reaches an unexpected endpoint"}
            }
            set second [get_registers $target]
            if {[get_collection_size $second] != 1} {error "Missing second stage $target"}
            set slacks {}
            foreach mode {setup hold} {
                set paths [get_timing_paths -$mode -from $single -to $second -npaths 1]
                if {[get_collection_size $paths] != 1} {error "Missing timed $mode synchronizer stage path at $name"}
                foreach_in_collection path $paths {
                    set launch [get_clock_info -name [get_path_info -from_clock $path]]
                    set latch [get_clock_info -name [get_path_info -to_clock $path]]
                    if {$launch ne $latch} {error "Synchronizer stages do not share a clock at $name"}
                    set slack [get_path_info -slack $path]
                    if {$slack <= 0} {error "Synchronizer stage $mode violation at $name"}
                    lappend slacks $slack
                }
            }
            puts $f "$label\t$name\t$target\t[join $slacks \t]"
            incr count
        }
    }
    close $f
    if {$count != 116} {error "Incomplete custom synchronizer audit"}
    report_metastability -nchains 10000 -file "$output/core-sync-$tag-metastability.rpt"
    puts "CORE_SYNC_PASS model=$model temperature=$temperature first_stages=$count exclusive_second_stage_fanout=1"
    delete_timing_netlist
}
project_close
