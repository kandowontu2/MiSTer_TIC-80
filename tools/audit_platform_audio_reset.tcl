# Audit the fitted three-stage release chain. No raw reset cut is waived.
# Run from a fresh fitted project; this is topology/timing, not electrical proof.
proc tm_run_audio_reset_audit {tm_output} {
project_open TIC80
foreach corner {{slow -40} {slow 100} {fast -40} {fast 100}} {
    lassign $corner model temperature
    create_timing_netlist -model $model -temperature $temperature -voltage 1100
    read_sdc
    update_timing_netlist
    set audio_clock [get_clocks {pll_audio|pll_audio_inst|altera_pll_i|general[0].gpll~PLL_OUTPUT_COUNTER|divclk}]
    if {[get_collection_size $audio_clock] != 1} {error "Missing unique audio clock"}
    set audio_name [get_clock_info -name $audio_clock]
    set all_stages [get_registers {*|tic80_audio_reset:reset_sync|release_pipe*}]
    if {[get_collection_size $all_stages] != 3} {error "Audio reset must retain exactly three fitted stages"}
    for {set bit 0} {$bit < 3} {incr bit} {
        set tm_reset_stage($bit) [get_registers [format {*|tic80_audio_reset:reset_sync|release_pipe[%d]} $bit]]
        if {[get_collection_size $tm_reset_stage($bit)] != 1} {error "Missing unique audio reset stage $bit"}
        if {$bit > 0} {
            set previous $tm_reset_stage([expr {$bit - 1}])
            set fans [get_fanouts $previous]
            if {[get_collection_size $fans] != 1} {error "Audio reset first-stage fanout bypass or duplication"}
            foreach_in_collection fan $fans {
                foreach_in_collection target $tm_reset_stage($bit) {
                    if {[get_node_info -name $fan] ne [get_node_info -name $target]} {
                        error "Audio reset stage feeds unexpected endpoint"
                    }
                }
            }
            foreach mode {setup hold} {
                set paths [get_timing_paths -$mode -from $previous -to $tm_reset_stage($bit) -npaths 1]
                if {[get_collection_size $paths] != 1} {error "Missing $mode audio reset stage path"}
                foreach_in_collection path $paths {
                    if {[get_clock_info -name [get_path_info -from_clock $path]] ne $audio_name ||
                        [get_clock_info -name [get_path_info -to_clock $path]] ne $audio_name} {
                        error "Audio reset stages do not share the audio clock"
                    }
                    if {[get_path_info -slack $path] <= 0} {error "Nonpositive audio reset $mode margin"}
                }
                report_timing -$mode -from $previous -to $tm_reset_stage($bit) -npaths 1 -detail full_path \
                    -file [file join $tm_output [format {platform-audio-reset-%s-%s-stage%d-%s.rpt} $model $temperature $bit $mode]]
            }
        }
    }
    # Inventory every destination of the last stage. Synchronous consumers
    # need setup/hold; asynchronous reset pins need recovery/removal instead.
    set consumers [get_fanouts $tm_reset_stage(2)]
    set count [get_collection_size $consumers]
    if {$count < 20} {error "Incomplete fitted audio-reset consumer fanout"}
    set table [open [file join $tm_output [format {platform-audio-reset-%s-%s-consumers.tsv} $model $temperature]] {WRONLY CREAT EXCL}]
    puts $table "sink\tcheck\tlaunch_clock\tlatch_clock\tslack_ns"
    set checked 0
    foreach_in_collection consumer $consumers {
        set name [get_node_info -name $consumer]
        # Keep the fitted object identity. Hierarchical register names include
        # vector brackets and must not be reinterpreted as selector patterns.
        set kind [get_node_info -type $consumer]
        if {$kind ne "reg"} {error "Audio reset reaches a non-register endpoint ($kind): $name"}
        set target $consumer
        set found_modes {}
        foreach mode {setup hold recovery removal} {
            set paths [get_timing_paths -$mode -from $tm_reset_stage(2) -to $target -npaths 1]
            if {[get_collection_size $paths] == 0} {continue}
            foreach_in_collection path $paths {
                if {[get_clock_info -name [get_path_info -from_clock $path]] ne $audio_name ||
                    [get_clock_info -name [get_path_info -to_clock $path]] ne $audio_name} {
                    error "Audio reset consumer lies outside the audio clock: $name"
                }
                if {[get_path_info -slack $path] <= 0} {error "Nonpositive reset $mode margin at $name"}
                lappend found_modes $mode
                puts $table "$name\t$mode\t$audio_name\t$audio_name\t[get_path_info -slack $path]"
            }
        }
        foreach pair {{setup hold} {recovery removal}} {
            lassign $pair first second
            set have_first [expr {[lsearch -exact $found_modes $first] >= 0}]
            set have_second [expr {[lsearch -exact $found_modes $second] >= 0}]
            if {$have_first != $have_second} {error "Audio reset endpoint lacks paired $first/$second checks: $name"}
        }
        if {[llength $found_modes] == 0} {error "Audio reset endpoint has no timed path: $name"}
        incr checked
    }
    if {$checked != $count} {error "Incomplete fitted reset consumer coverage"}
    close $table
    # Raw platform reset sources may drive only the release chain inside
    # audio_out, never a final reset consumer. report_path ignores clock cuts.
    set raw [get_registers {areset}]
    if {[get_collection_size $raw] != 1} {error "Missing unique platform audio-reset request"}
    lassign [report_path -from $raw -to $consumers -npaths 0 -pairs_only -summary \
        -file [file join $tm_output [format {platform-audio-reset-%s-%s-raw.rpt} $model $temperature]]] raw_count raw_delay
    # The pipeline creates a sequential boundary, so direct raw paths vanish.
    if {$raw_count != 0} {error "Raw audio-reset request bypasses synchronized release"}
    puts "PLATFORM_AUDIO_RESET_PASS model=$model temperature=$temperature stages=3 consumers=$checked raw_paths=0"
    delete_timing_netlist
}
project_close
}
set tm_audio_reset_output [lindex $quartus(args) 0]
if {$tm_audio_reset_output eq ""} {set tm_audio_reset_output ..}
tm_run_audio_reset_audit $tm_audio_reset_output
