"""Make the pinned RTL's five static local initializers explicit for simulation.

Only declarations initialized to zero are moved to module scope. References
inside their original lexical block are renamed; clocked assignments and
expressions are unchanged. This is a simulator input conversion, not product RTL.
"""
import re


def normalize(source):
    regions = (
        ('sample', 'reg sample_ce;', 'reg flt_ce;', {'div', 'add'}),
        ('filter', 'reg flt_ce;', 'reg [15:0] cl,cr;', {'cnt'}),
        ('enable', 'reg a_en1 = 0, a_en2 = 0;', 'wire [15:0] acl, acr;', {'dly1', 'dly2'}),
    )
    declarations, edits, fields = [], [], []
    pattern = re.compile(r'^([ \t]*)reg([ \t]+(?:\[[^\]\n]+\][ \t]+)?)(\w+)[ \t]*=[ \t]*0;', re.M)
    for label, start, stop, expected in regions:
        if source.count(start) != 1 or source.count(stop) != 1:
            raise ValueError('Pinned initializer region changed: ' + label)
        begin = source.index(start); end = source.index(stop, begin)
        original = source[begin:end]
        matches = list(pattern.finditer(original))
        if {match[3] for match in matches} != expected or len(matches) != len(expected):
            raise ValueError('Pinned static initializer declarations changed: ' + label)
        lowered = pattern.sub('', original)
        for match in matches:
            name = match[3]; unique = 'tm_static_' + label + '_' + name
            if re.search(r'\b' + unique + r'\b', source):
                raise ValueError('Initializer namespace collision: ' + unique)
            declarations.append('reg' + match[2] + unique + ' = 0;\n')
            lowered = re.sub(r'\b' + name + r'\b', unique, lowered)
            fields.append(dict(region=label, original_name=name, module_name=unique, initial_value=0))
        edits.append((begin, end, original, lowered))
    normalized = source
    for begin, end, original, lowered in reversed(edits):
        normalized = normalized[:begin] + lowered + normalized[end:]
    marker = 'localparam AUDIO_RATE = 48000;'
    if source.count(marker) != 1:
        raise ValueError('Pinned audio module anchor changed')
    prefix = ''.join(declarations)
    normalized = normalized.replace(marker, prefix + marker)
    # Keep an exact round-trip guard against accidental changes outside these
    # declaration/reference edits; no source logic is relaxed or replaced.
    restored = normalized.replace(prefix + marker, marker)
    for _, _, original, lowered in reversed(edits):
        if restored.count(lowered) != 1:
            raise ValueError('Ambiguous normalized initializer region')
        restored = restored.replace(lowered, original)
    if restored != source or len(fields) != 5:
        raise ValueError('Initializer conversion changed unrelated RTL')
    return normalized, fields
