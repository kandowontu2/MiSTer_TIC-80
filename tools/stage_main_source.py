"""Stage TIC-80 source packets and delayed MGL reset holding; never alter pinned Main."""
import argparse
import difflib
from pathlib import Path
import shutil
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--source',type=Path,default=ROOT/'reference/main')
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
original=(args.source/'user_io.cpp').read_text()
needle='\t// set index byte (0=bios rom, 1-n=OSD entry index)\n\tuser_io_set_index(index);'
assert original.count(needle)==1,'Pinned file transfer entry changed'
insertion='''\t// TIC-80 consumes a reserved metadata download before the cart. Old
\t// bitstreams ignore this index. The bridge binds it to the next cart ticket.
\tif (!strcasecmp(user_io_get_core_name(), "TIC-80") && (index == 0 || index == 0x40))
\t{
\t\tuint8_t source_packet[8 + TM_CART_SOURCE_CAPACITY];
\t\tsize_t source_size = tm_main_source_packet(composite || load_addr ? NULL : getFullPath(name), index, source_packet);
\t\tuser_io_set_index(0xfe);
\t\t// Poison an interrupted cart at this unsupported index before ending
\t\t// it, including on old loaders which notice index changes only on data.
\t\tconst uint8_t abort_byte = 0;
\t\tuser_io_file_tx_data(&abort_byte, 1);
\t\tuser_io_set_download(0);
\t\tuser_io_set_download(1);
\t\tuser_io_file_tx_data(source_packet, source_size);
\t\tuser_io_set_download(0);
\t}

'''
status_needle='\t\tfor (uint32_t i = 0; i < sizeof(cur_status); i += 2) spi_w((cur_status[i + 1] << 8) | cur_status[i]);'
assert original.count(status_needle)==1,'Pinned status transmitter changed'
status_changed='''\t\tconst mgl_struct *mgl = mgl_get();
\t\tint hold = !strcasecmp(user_io_get_core_name(), "TIC-80") &&
\t\t\ttm_main_mgl_hold(mgl->count, mgl->current, mgl->done, mgl->state,
\t\t\t\tmgl->item[0].action, mgl->item[0].type, mgl->item[0].index);
\t\tfor (uint32_t i = 0; i < sizeof(cur_status); i += 2)
\t\t\tspi_w(tm_main_mgl_status_word((cur_status[i + 1] << 8) | cur_status[i], i / 2, hold));'''
changed='#include "main_source_packet.h"\n#include "main_mgl_hold.h"\n'+original.replace(needle,insertion+needle).replace(status_needle,status_changed)
mouse_needle='void user_io_mouse(unsigned char b, int16_t x, int16_t y, int16_t w)'
assert original.count(mouse_needle)==1, 'Pinned mouse transmitter changed'
horizontal_sender='''void user_io_tic80_horizontal_wheel(int32_t delta)
{
    if (!delta || core_type != CORE_TYPE_8BIT || user_io_osd_is_visible() ||
        strcasecmp(user_io_get_core_name(), "TIC-80")) return;
    spi_uio_cmd_cont(TM_MAIN_HWHEEL_COMMAND);
    spi_w((uint32_t)delta & 0xffff);
    spi_w((uint32_t)delta >> 16);
    DisableIO();
}

'''
changed='#include "main_horizontal_wheel.h"\n'+changed.replace(mouse_needle,horizontal_sender+mouse_needle)
input_original=(args.source/'input.cpp').read_text()
input_changed='#include "main_horizontal_wheel.h"\n'+input_original
def input_replace(needle, replacement):
    global input_changed
    assert input_changed.count(needle)==1, 'Pinned input hook changed: '+needle
    input_changed=input_changed.replace(needle,replacement)
input_replace('static int mouse_w = 0;', '''static int mouse_w = 0;
static tm_main_horizontal_wheel tic80_horizontal[NUMDEV] = {};
extern void user_io_tic80_horizontal_wheel(int32_t delta);''')
input_replace('\tmemset(input, 0, sizeof(input));', '\tmemset(input, 0, sizeof(input));\n\t\tmemset(tic80_horizontal, 0, sizeof(tic80_horizontal));')
input_replace('\t\t\t\t\t\tpool[n].fd = fd;', '''\t\t\t\t\t\tpool[n].fd = fd;
                        unsigned char rel_bits[(REL_MAX + 8) / 8] = {};
                        if (!strncmp(de->d_name, "event", 5) &&
                            ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rel_bits)), rel_bits) >= 0)
                            tic80_horizontal[n].high_resolution =
                                (rel_bits[REL_HWHEEL_HI_RES / 8] >> (REL_HWHEEL_HI_RES % 8)) & 1;''')
input_replace('int input_test(int getchar)\n{', '''int input_test(int getchar)
{
    const int tic80_mouse_active = !getchar && grabbed && !user_io_osd_is_visible() &&
        !strcasecmp(user_io_get_core_name(), "TIC-80");
    if (!tic80_mouse_active)
        for (int dev = 0; dev < NUMDEV; ++dev) tm_main_horizontal_clear(&tic80_horizontal[dev]);''')
input_replace('\t\t\t\t\t\tif (read(pool[i].fd, &ev, sizeof(ev)) == sizeof(ev))\n\t\t\t\t\t\t{', '''\t\t\t\t\t\tif (read(pool[i].fd, &ev, sizeof(ev)) == sizeof(ev))
\t\t\t\t\t\t{
                            int32_t delta = tm_main_horizontal_event(&tic80_horizontal[i],
                                ev.type, ev.code, ev.value, tic80_mouse_active);
                            if (delta) user_io_tic80_horizontal_wheel(delta);''')
menu_original=(args.source/'menu.cpp').read_text()
menu_dispatch_needle='\t\tcase 3:\n\t\t\tmgl->state = 0;'
assert menu_original.count(menu_dispatch_needle)==1,'Pinned MGL dispatch branch changed'
menu_dispatch='''\t\tcase 1:
\t\t\t// A controller/info popup must not extend the first cart's MGL
\t\t\t// delay while TIC-80 is held in reset waiting for that cart.
\t\t\tif (menustate == MENU_INFO && !strcasecmp(user_io_get_core_name(), "TIC-80") &&
\t\t\t\ttm_main_mgl_initial_cart(mgl->count, mgl->current,
\t\t\t\t\tmgl->item[0].action, mgl->item[0].type, mgl->item[0].index))
\t\t\t\tmenustate = MENU_NONE1;
\t\t\tbreak;

'''
menu_original_with_dispatch=menu_original.replace(menu_dispatch_needle,menu_dispatch+menu_dispatch_needle)
menu_needle='\t\tcase 3:\n\t\t\tmgl->state = 0;'
assert menu_original.count(menu_needle)==1,'Pinned MGL completion branch changed'
menu_changed='#include "main_mgl_hold.h"\n'+menu_original_with_dispatch.replace(menu_needle,'''\t\tcase 3:
\t\t\t// File success, missing path and invalid submenu all finish here.
\t\t\t// Drop only the temporary overlay; preserve explicit OSD reset state.
\t\t\tif (!strcasecmp(user_io_get_core_name(), "TIC-80") &&
\t\t\t\ttm_main_mgl_initial_cart(mgl->count, mgl->current,
\t\t\t\t\tmgl->item[0].action, mgl->item[0].type, mgl->item[0].index))
\t\t\t\tuser_io_status_set("[0]", user_io_status_get("[0]"));
\t\t\tmgl->state = 0;''')
args.output.mkdir(parents=True,exist_ok=True)
(args.output/'user_io.cpp').write_text(changed)
(args.output/'menu.cpp').write_text(menu_changed)
(args.output/'input.cpp').write_text(input_changed)
shutil.copyfile(ROOT/'tools/main_horizontal_wheel.h',args.output/'main_horizontal_wheel.h')
shutil.copyfile(ROOT/'tools/main_source_packet.h',args.output/'main_source_packet.h')
shutil.copyfile(ROOT/'tools/main_mgl_hold.h',args.output/'main_mgl_hold.h')
inc=args.output/'tic80_mister'; inc.mkdir(exist_ok=True)
shutil.copyfile(ROOT/'include/tic80_mister/memory_map.h',inc/'memory_map.h')
(args.output/'tic80-source.patch').write_text(''.join(difflib.unified_diff(
    original.splitlines(keepends=True),changed.splitlines(keepends=True),fromfile='a/user_io.cpp',tofile='b/user_io.cpp')) + ''.join(difflib.unified_diff(
    menu_original.splitlines(keepends=True),menu_changed.splitlines(keepends=True),fromfile='a/menu.cpp',tofile='b/menu.cpp')) + ''.join(difflib.unified_diff(
    input_original.splitlines(keepends=True),input_changed.splitlines(keepends=True),fromfile='a/input.cpp',tofile='b/input.cpp')))
print('Staged TIC-80 source packet and delayed MGL reset overlay; pinned checkout unchanged')
