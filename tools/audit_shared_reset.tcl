# Check the actual fitted fanout, including paths hidden by clock groups.
project_open TIC80
foreach corner {{slow -40} {slow 100} {fast -40} {fast 100}} {
    lassign $corner model temperature
    create_timing_netlist -model $model -temperature $temperature -voltage 1100
    read_sdc
    update_timing_netlist
    foreach item {
        {horizontal {*|tic80_video_top:video|reset_sys[3]*} {*|tic80_horizontal_wheel:horizontal_input|*} 60 {emu|pll|pll_inst|altera_pll_i|general[0].gpll~PLL_OUTPUT_COUNTER|divclk}}
        {pixel {*|tic80_video_top:video|reset_vid[1]*} {*|tic80_pixel_enable:pixel_enable|phase*} 18 {pll_audio|pll_audio_inst|altera_pll_i|general[0].gpll~PLL_OUTPUT_COUNTER|divclk}}
    } {
        lassign $item label source_pattern target_pattern minimum clock_name
        set source [get_registers $source_pattern]
        set target [get_registers $target_pattern]
        set clock [get_clocks $clock_name]
        set width [get_collection_size $target]
        if {[get_collection_size $source] < 1 || $width < $minimum || [get_collection_size $clock] != 1} {
            error "Missing fitted shared reset source/consumer/clock for $label"
        }
        set limit [expr {[get_clock_info -period $clock] * 0.9}]
        lassign [report_path -from $source -to $target -npaths 0 -pairs_only -summary -file [format {../shared-reset-%s-%s-%s.rpt} $model $temperature $label]] count delay
        if {$count != $width || $delay <= 0 || $delay >= $limit} {
            error "Shared reset $label: paths=$count consumers=$width delay=$delay bound=$limit"
        }
        set raw [get_registers {reset_req}]
        if {[get_collection_size $raw] != 1} { error "Missing unique platform reset source" }
        lassign [report_path -from $raw -to $target -npaths 0 -pairs_only -summary -file [format {../shared-reset-raw-%s-%s-%s.rpt} $model $temperature $label]] raw_count raw_delay
        if {$raw_count != 0} { error "Raw reset still reaches $label consumers: $raw_count paths" }
        puts "SHARED_RESET_PASS model=$model temperature=$temperature block=$label consumers=$width paths=$count max_delay_ns=$delay bound_ns=$limit raw_paths=$raw_count"
    }
    delete_timing_netlist
}
project_close
