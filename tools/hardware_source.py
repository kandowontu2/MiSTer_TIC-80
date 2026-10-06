"""Read and validate retained cartridge/source tickets without owning a session."""
import struct


def source_snapshot(command, expected_path, expected_size):
    def words(addresses):
        return [int(value, 16) for value in command(
            'for a in ' + ' '.join(hex(address) for address in addresses) +
            '; do devmem "$a" 32 || exit 31; done').split()]

    assert command('cat /tmp/CORENAME').strip() == 'TIC-80'
    addresses = [0x3A000004, 0x3A000070, 0x3A000074, 0x3A000038, 0x3A04C200, 0x3A04C204]
    magic, ticket, size, ack, source_ticket, length = words(addresses)
    assert magic == 0x314E5354
    assert ticket & 3 == 2 and ticket == ack == source_ticket
    assert size == expected_size and 2 <= length <= 256
    raw = b''.join(struct.pack('<I', value) for value in words(
        [0x3A04C208 + index * 4 for index in range((length + 3) // 4)]))[:length]
    assert raw[-1:] == b'\0' and b'\0' not in raw[:-1]
    assert raw[:-1].decode('utf-8') == expected_path
    assert words(addresses) == [magic, ticket, size, ack, source_ticket, length], 'Source changed during observation'
    assert command('cat /tmp/CORENAME').strip() == 'TIC-80'
    return dict(magic=magic, ticket=ticket, acknowledged_ticket=ack,
                source_ticket=source_ticket, bytes=size, path=expected_path, source_length=length)
