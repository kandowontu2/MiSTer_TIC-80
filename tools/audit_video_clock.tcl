# Require the fitted pixel generator, raster and DAC divider to use the same
# 24.576 MHz PLL output, rather than merely having similar nominal periods.
project_open TIC80
create_timing_netlist -model slow -temperature 100 -voltage 1100
read_sdc
update_timing_netlist
set audio_clock [get_clocks {pll_audio|pll_audio_inst|altera_pll_i|general[0].gpll~PLL_OUTPUT_COUNTER|divclk}]
if {[get_collection_size $audio_clock] != 1} { error "Missing unique playback clock" }
set period [get_clock_info -period $audio_clock]
# Quartus 17 derives an approximate fractional-PLL timing period (40.682 ns
# here), while the fitted PLL requests 24.576 MHz. Allow its <500 ppm timing
# approximation; register membership below still requires the identical clock.
if {abs($period / 40.690104167 - 1.0) > 0.0005} { error "Wrong playback/video clock period: $period" }
set audio_name [get_clock_info -name $audio_clock]
foreach item {
    {pixel_phase {*|tic80_pixel_enable:pixel_enable|phase*} 18}
    {raster_x {*|tic80_scanout:scanout|x*} 9}
    {raster_y {*|tic80_scanout:scanout|y*} 9}
    {dac_divider {*|tic80_audio:audio|sample_div*} 9}
} {
    lassign $item label pattern minimum
    set regs [get_registers $pattern]
    set count [get_collection_size $regs]
    if {$count < $minimum} { error "Missing $label registers: $count" }
    foreach_in_collection reg $regs {
        set name [get_register_info -name $reg]
        # Quartus 17 lacks all_registers -clock and get_clocks -of_objects.
        # Inspect the actual latch clock of a timed data path into each bit.
        set paths [get_timing_paths -setup -to [get_registers $name] -npaths 1]
        if {[get_collection_size $paths] != 1} { error "No timed data path into $name" }
        foreach_in_collection path $paths {
            set latch_name [get_clock_info -name [get_path_info -to_clock $path]]
            if {$latch_name != $audio_name} {
                error "$label is outside the shared playback/video clock: $name uses $latch_name"
            }
        }
    }
    puts "SHARED_CLOCK_PASS block=$label registers=$count period_ns=$period"
}
delete_timing_netlist
project_close
