"""Exercise real Main SPI/metadata code against pinned hps_io and the cart loader."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def function(code, signature):
    if code.count(signature) != 1:
        raise RuntimeError('Pinned Main function changed: ' + signature)
    begin = code.index(signature)
    brace = code.index('{', begin)
    depth = 1
    end = brace + 1
    while depth:
        depth += (code[end] == '{') - (code[end] == '}')
        end += 1
    return code[begin:end] + '\n'

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--scenario', choices=('normal', 'source-restart', 'cart-restart', 'ack', 'finish', 'horizontal', 'abrupt-restart', 'stock'), default='normal')
parser.add_argument('--keep', type=Path)
parser.add_argument('--legacy-loader', action='store_true')
parser.add_argument('--loader-source', type=Path, help='Use an explicit loader for a regression counterexample')
parser.add_argument('--hold-header', type=Path, help='Use an explicit Main hold helper for a regression counterexample')
args = parser.parse_args()
if args.legacy_loader and args.loader_source:
    parser.error('--legacy-loader and --loader-source are mutually exclusive')
with tempfile.TemporaryDirectory(prefix='tic80-hps-') as temporary:
    work = Path(temporary)
    for path in ('fpga/rtl/tic80_cart_loader.sv', 'fpga/rtl/tic80_horizontal_wheel.sv', 'fpga/rtl/memory_map.svh',
                 'reference/pico8/fpga/sys/hps_io.sv', 'tests/hps_transport_test.cpp',
                 'tools/main_source_packet.h', 'include/tic80_mister/memory_map.h'):
        destination = work / path
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / path, destination)
    if args.legacy_loader:
        baseline = ROOT / 'build/studio-source-fpga-baseline/tic80_cart_loader.sv'
        if hashlib.sha256(baseline.read_bytes()).hexdigest() != '223a594f01af256de3d304adca8e6d44c35a73f1bb2683e6550307ec880b98a1':
            raise RuntimeError('Accepted loader baseline hash mismatch')
        shutil.copyfile(baseline, work / 'fpga/rtl/tic80_cart_loader.sv')
    if args.loader_source:
        shutil.copyfile(args.loader_source, work / 'fpga/rtl/tic80_cart_loader.sv')
    # Quartus prunes PS2DIV=0 before resolving references to generate-local
    # PS/2 regs. Verilator resolves their names first and rejects implicit
    # procedural wires. Declare only these inert names at module scope; the
    # disabled branch and the complete file-download parser are unchanged.
    hps = work / 'reference/pico8/fpga/sys/hps_io.sv'
    hps_code = hps.read_text()
    declaration = 'localparam DW = (WIDE) ? 15 : 7;'
    if hps_code.count(declaration) != 1:
        raise RuntimeError('Pinned hps_io width declaration changed')
    hps_code = hps_code.replace(declaration, declaration + '\nreg [7:0] kbd_data,mouse_data;\nreg kbd_we,kbd_rd,mouse_we,mouse_rd;\nwire [8:0] kbd_data_host=0,mouse_data_host=0;')
    # The unused video-measurement module combines initialized local regs
    # with nonblocking assignments, which Verilator 5.020 diagnoses as mixed
    # assignments. Its video clocks are tied low in this transport harness.
    video_begin = hps_code.index('module video_calc')
    video_end = hps_code.index('endmodule', video_begin) + len('endmodule')
    hps_code = (hps_code[:video_begin] + '/* verilator lint_off BLKANDNBLK */\n' +
                hps_code[video_begin:video_end] + '\n/* verilator lint_on BLKANDNBLK */' + hps_code[video_end:])
    uio_begin = hps_code.index('always@(posedge clk_sys) begin : uio_block')
    uio_end = hps_code.index('///////////////////////////////   PS2', uio_begin)
    hps_code = (hps_code[:uio_begin] + '/* verilator lint_off BLKANDNBLK */\n' +
                hps_code[uio_begin:uio_end] + '\n/* verilator lint_on BLKANDNBLK */\n' + hps_code[uio_end:])
    hps.write_text(hps_code)
    main = work / 'main'
    main.mkdir()
    for path in (ROOT / 'reference/main').glob('*.h'):
        shutil.copyfile(path, main / path.name)
    for path in ('spi.cpp',):
        shutil.copyfile(ROOT / 'reference/main' / path, main / path)
    shutil.copyfile(ROOT / 'reference/main/sxmlc.c', main / 'sxmlc.c')
    shutil.copyfile(ROOT / 'reference/main/support/arcade/mra_loader.h', main / 'mra_loader.h')
    subprocess.run([sys.executable, str(ROOT / 'tools/stage_main_source.py'), '--output', str(main)], check=True)
    if args.hold_header:
        shutil.copyfile(args.hold_header, main / 'main_mgl_hold.h')
    staged_user = (main / 'user_io.cpp').read_text()
    user = staged_user
    if args.scenario == 'stock':
        # Extract transfer/status functions from unmodified Main. No source
        # packet or MGL reset overlay is sent in this compatibility scenario.
        user = (ROOT / 'reference/main/user_io.cpp').read_text()
        (main / 'stock-user_io.cpp').write_text(user)
        metadata = ''
    else:
        begin = user.index('\t// TIC-80 consumes a reserved metadata download')
        end = user.index('\t// set index byte (0=bios rom, 1-n=OSD entry index)', begin)
        metadata = user[begin:end]
    helpers = '#include "user_io.h"\n#include "spi.h"\n#include "file_io.h"\n#include "main_source_packet.h"\n#include "main_mgl_hold.h"\n#include "mra_loader.h"\n#include "sxmlc.h"\n#include <strings.h>\n#include <ctype.h>\n#include <stdio.h>\n#include <stdlib.h>\nstatic int fio_size=0, last_file_ext_idx=0;\nstatic char cur_status[16]={};\n'
    parser_code = (ROOT / 'reference/main/support/arcade/mra_loader.cpp').read_text()
    helpers += 'static mgl_struct mgl={};\n'
    helpers += '#include "main_horizontal_wheel.h"\nstatic unsigned char core_type=CORE_TYPE_8BIT;\n'
    helpers += function(staged_user, 'void user_io_tic80_horizontal_wheel(int32_t delta)')
    for signature in ('static int scan_mgl(XMLEvent evt, const XMLNode* node, SXML_CHAR* text, const int n, SAX_Data* sd)',
                      'mgl_struct* mgl_parse(const char *xml)', 'mgl_struct* mgl_get()'):
        helpers += function(parser_code, signature)
    for signature in ('int user_io_status_bits(const char *opt, int *s, int *e, int ex, int single)',
                      'uint32_t user_io_status_get(const char *opt, int ex)',
                      'void user_io_status_set(const char *opt, uint32_t value, int ex)'):
        helpers += function(user, signature)
    menu = (main / 'menu.cpp').read_text()
    complete_begin = menu.index('\t\t\t// File success, missing path and invalid submenu all finish here.')
    complete_end = menu.index('\t\t\tmgl->state = 0;', complete_begin)
    helpers += 'void tm_main_mgl_complete() { mgl_struct *mgl=mgl_get();\n' + menu[complete_begin:complete_end] + '}\n'
    for signature in ('void user_io_set_index(unsigned char index)',
                      'void user_io_set_download(unsigned char enable, int addr)',
                      'void user_io_file_tx_data(const uint8_t *addr, uint32_t len)',
                      'void user_io_file_info(const char *ext)'):
        helpers += function(user, signature)
    helpers += 'void tm_main_metadata(const char* name,unsigned char index,char composite,uint32_t load_addr) {\n' + metadata + '}\n'
    gpio = (ROOT / 'reference/main/fpga_io.cpp').read_text()
    helpers += 'uint32_t tm_gpo_read(); void tm_gpo_write(uint32_t); int tm_gpi_read();\n'
    helpers += '#define fpga_gpo_read() tm_gpo_read()\n#define fpga_gpo_write(v) tm_gpo_write(v)\n#define fpga_gpi_read() tm_gpi_read()\n#define SSPI_STROBE (1<<17)\n#define SSPI_ACK SSPI_STROBE\n'
    helpers += function(gpio, 'void fpga_spi_en(uint32_t mask, uint32_t en)')
    helpers += function(gpio, 'uint16_t fpga_spi(uint16_t word)')
    helpers += function(gpio, 'int fpga_core_id()')
    init = function(gpio, 'int fpga_io_init()')
    init_tail = '\tfpga_gpo_write(0);\n\treturn 0;\n}'
    if init.count(init_tail) != 1:
        raise RuntimeError('Pinned Main GPIO initialization changed')
    # Mapping kernel registers is outside this digital model. Retain the
    # actual initialization write and the complete actual core-ID exchange.
    helpers += ('void tm_main_bus_restart() {\n' + init_tail.split('\treturn 0;')[0] +
                '\tif(fpga_core_id() != 0xa4) abort();\n}\n')
    (main / 'transport.cpp').write_text(helpers)
    system = (ROOT / 'reference/pico8/fpga/sys/sys_top.v').read_text()
    begin = system.index('reg  io_ack;')
    end = system.index('`ifdef MISTER_DUAL_SDRAM', begin)
    handshake = system[begin:end]
    wrapper = '''module hps_transport_top(
    input clk_sys, reset, osd_open, input [31:0] gp_out, output [31:0] gp_in,
    output [31:0] horizontal_wheel,
    input [31:0] cart_ack, input cart_ack_valid, output [63:0] cart_meta,
    output write_request, output [28:0] write_address,
    output [63:0] write_data, output [7:0] write_be, input write_ready,
    output ioctl_download, output ioctl_wr, output [26:0] ioctl_addr,
    output [7:0] ioctl_dout, output [15:0] ioctl_index, output ioctl_wait,
    output [127:0] status
);
wire [48:0] HPS_BUS;
wire [35:0] EXT_BUS;
assign EXT_BUS[32] = 0;
assign EXT_BUS[15:0] = 0;
wire io_wait=HPS_BUS[37], io_wide=HPS_BUS[32];
wire [15:0] io_dout=HPS_BUS[15:0], io_din=gp_outr[15:0];
wire io_clk=gp_outr[17], io_ss0=gp_outr[18], io_ss1=gp_outr[19], io_ss2=gp_outr[20];
wire io_fpga=~io_ss1 & io_ss0, io_uio=~io_ss1 & io_ss2;
wire vs_wait=0;
assign gp_in=gp_out[31] ? {1'b0,2'd0,1'b0,8'd0,2'd1,io_ack,io_wide,io_dout} : 32'h5ca623a4;
assign HPS_BUS[48:38]=0;
assign HPS_BUS[35:33]={io_fpga,io_uio,io_strobe};
assign HPS_BUS[31:16]=io_din;
''' + handshake + '''
hps_io #(.CONF_STR("TIC-80;;F0,TICPNG,Load Cartridge;"),.VDNUM(1),.F12KEYMOD(1)) hps_io (
    .clk_sys(clk_sys),.HPS_BUS(HPS_BUS),.EXT_BUS(EXT_BUS),
    .ioctl_download(ioctl_download),.ioctl_wr(ioctl_wr),.ioctl_addr(ioctl_addr),
    .ioctl_dout(ioctl_dout),.ioctl_index(ioctl_index),.ioctl_wait(ioctl_wait),
    .ioctl_din(8'd0),.ioctl_upload_req(1'b0),.ioctl_upload_index(8'd0),
    .status_in(128'd0),.status_set(1'b0),.status_menumask(16'd0),.status(status),
    .sd_lba('{32'd0}),.sd_rd(1'b0),.sd_wr(1'b0),.sd_blk_cnt('{6'd0}),.sd_buff_din('{8'd0}),
    .info_req(1'b0),.info(8'd0),.new_vmode(1'b0),.video_rotated(1'b0)
);
tic80_horizontal_wheel horizontal_input (
    .clk(clk_sys),.reset(reset),.osd_open(osd_open),.io_enable(EXT_BUS[34]),
    .io_strobe(EXT_BUS[33]),.io_din(EXT_BUS[31:16]),.total(horizontal_wheel)
);
tic80_cart_loader loader (
    .clk(clk_sys),.reset(reset),.ioctl_download(ioctl_download),.ioctl_wr(ioctl_wr),
    .ioctl_addr(ioctl_addr),.ioctl_dout(ioctl_dout),.ioctl_index(ioctl_index),
    .ioctl_wait(ioctl_wait),.cart_ack(cart_ack),.cart_ack_valid(cart_ack_valid),.cart_meta(cart_meta),
    .write_request(write_request),.write_address(write_address),.write_data(write_data),
    .write_be(write_be),.write_ready(write_ready)
);
endmodule
'''
    (work / 'hps_transport_top.sv').write_text(wrapper)
    # Keep the pinned XML parser in its actual C language, as Main does.
    subprocess.run(['gcc', '-I', 'main', '-ffunction-sections', '-fdata-sections',
                    '-c', 'main/sxmlc.c', '-o', 'main/sxmlc.o'], check=True, cwd=work)
    inputs = {str(path.relative_to(work)): hashlib.sha256(path.read_bytes()).hexdigest()
              for path in sorted(work.rglob('*')) if path.is_file()}
    subprocess.run(['verilator', '--cc', '--exe', '--top-module', 'hps_transport_top', '--Mdir', 'obj_dir',
                    '-Wno-fatal', '-Wno-PINMISSING', '-Wno-WIDTH', '-Ifpga/rtl',
                    '-CFLAGS', '-I../include -I../main -ffunction-sections -fdata-sections',
                    '-LDFLAGS', '../main/sxmlc.o -Wl,--gc-sections', 'hps_transport_top.sv',
                    'reference/pico8/fpga/sys/hps_io.sv', 'fpga/rtl/tic80_cart_loader.sv', 'fpga/rtl/tic80_horizontal_wheel.sv',
                    'tests/hps_transport_test.cpp', 'main/transport.cpp', 'main/spi.cpp'], check=True, cwd=work)
    subprocess.run(['make', '-C', 'obj_dir', '-f', 'Vhps_transport_top.mk', '-j4'], check=True, cwd=work)
    if args.keep:
        import json
        args.keep.mkdir(parents=True, exist_ok=True)
        for name in ('main/transport.cpp', 'hps_transport_top.sv', 'main/user_io.cpp', 'main/menu.cpp', 'main/tic80-source.patch'):
            destination = args.keep / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(work / name, destination)
        if args.scenario == 'stock':
            shutil.copyfile(main / 'stock-user_io.cpp', args.keep / 'main/stock-user_io.cpp')
        (args.keep / 'inputs.json').write_text(json.dumps(inputs, indent=2) + '\n')
    subprocess.run([str(work / 'obj_dir/Vhps_transport_top'), args.scenario, 'legacy' if args.legacy_loader else 'current'], check=True, cwd=work)
