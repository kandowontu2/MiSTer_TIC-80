# Post-fit physical path check; run from build/fpga. Clock-group false paths
# exclude these crossings from setup/hold reports. report_path still examines
# their routed propagation delay. Includes the bundled video/control and
# monotonic audio-prefill handshakes. Not a complete CDC/MTBF qualification.
project_open TIC80
set buses {
    {write_pointer {write_gray write_binary} write_sync1 8}
    {read_pointer {read_gray read_binary} read_sync1 8}
    {played {played_gray} played_sync1 30}
    {slots {samples_gray} samples_sync1 32}
    {underruns {underruns_gray} underruns_sync1 32}
}
set controls {
    {frame {tic80_ddr_video:reader|frame_toggle} {tic80_scanout:scanout|toggle_sync[0]}}
    {bank {tic80_ddr_video:reader|frame_bank} {tic80_scanout:scanout|bank_sync[0]}}
    {valid {tic80_video_control:control|front_valid} {tic80_scanout:scanout|valid_sync[0]}}
    {vblank {tic80_scanout:scanout|vblank_toggle} {tic80_video_top:video|vblank_sync[0]}}
    {consumed {tic80_scanout:scanout|consumed_toggle} {tic80_video_top:video|consumed_sync[0]}}
    {prefill {tic80_audio:audio|prefill_ready} {tic80_audio:audio|prefill_sync1}}
}
foreach corner {{slow -40} {slow 100} {fast -40} {fast 100}} {
    lassign $corner model temperature
    create_timing_netlist -model $model -temperature $temperature -voltage 1100
    read_sdc
    update_timing_netlist
    set sys_clock [get_clocks {emu|pll|pll_inst|altera_pll_i|general[0].gpll~PLL_OUTPUT_COUNTER|divclk}]
    if {[get_collection_size $sys_clock] != 1} { error "Expected exactly one TIC-80 DDR clock" }
    set limit [expr {[get_clock_info -period $sys_clock] * 0.9}]
    foreach bus $buses {
        lassign $bus label sources destination width
        set from_patterns {}
        foreach source $sources { lappend from_patterns [format {*|tic80_audio:audio|%s*} $source] }
        set target [get_registers [format {*|tic80_audio:audio|%s*} $destination]]
        if {[get_collection_size $target] != $width} { error "Wrong first-stage width for $label" }
        # The Fitter can merge Gray MSBs with the equivalent binary MSB.
        # Include both source names, then require one physical path per bit.
        set from [get_registers $from_patterns]
        set filename [format {../audio-cdc-%s-%s-%s.rpt} $model $temperature $label]
        set paths [report_path -from $from -to $target -npaths 0 -pairs_only -summary -file $filename]
        lassign $paths count delay
        if {$count != $width || $delay <= 0 || $delay >= $limit} {
            error "Audio CDC $label: count=$count expected=$width delay=$delay limit=$limit"
        }
        puts "AUDIO_CDC_PASS model=$model temperature=$temperature bus=$label bits=$count max_delay_ns=$delay bound_ns=$limit"
    }
    foreach control $controls {
        lassign $control label source destination
        # Physical synthesis may duplicate an equivalent registered driver.
        # Include its duplicates, but still require exactly one actual data
        # path into the single first-stage register below.
        set from [get_registers [format {*|%s*} $source]]
        set target [get_registers [format {*|%s} $destination]]
        if {[get_collection_size $from] < 1 || [get_collection_size $target] != 1} {
            error "Missing source or wrong first-stage register count for $label"
        }
        set filename [format {../control-cdc-%s-%s-%s.rpt} $model $temperature $label]
        set paths [report_path -from $from -to $target -npaths 0 -pairs_only -summary -file $filename]
        lassign $paths count delay
        if {$count != 1 || $delay <= 0 || $delay >= $limit} {
            error "Control CDC $label: count=$count delay=$delay limit=$limit"
        }
        puts "CONTROL_CDC_PASS model=$model temperature=$temperature crossing=$label max_delay_ns=$delay bound_ns=$limit"
    }
    delete_timing_netlist
}
project_close
