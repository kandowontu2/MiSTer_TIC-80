"""Check scalar output startup values in a Quartus functional Verilog netlist.

Only direct assignments, inversion, Cyclone V output buffers and dffeas
power-up states are supported. An unknown cone is a failure, never a guessed
value. This checks compiler initialization, not electrical/timing behavior.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re


NAME = r'(?:\\\S+|[A-Za-z_]\w*)'


def check(netlist, expected):
    source = re.sub(r'//[^\n]*', '', netlist)
    drivers = {}
    power = {}

    def add(net, driver):
        drivers.setdefault(net.strip(), []).append(driver)

    for match in re.finditer(r'^\s*assign\s+([^=;]+)=\s*([^;]+);', source, re.M):
        add(match[1], ('assign', match[2].strip()))
    for match in re.finditer(
            rf'^\s*(dffeas|cyclonev_io_obuf)\s+({NAME})\s*\((.*?)\);\s*$',
            source, re.M | re.S):
        kind, instance, body = match.groups()
        ports = dict(re.findall(r'\.(\w+)\(([^()]*)\)', body))
        out = ports.get('q' if kind == 'dffeas' else 'o', '').strip()
        if out:
            add(out, (kind, instance, ports))
    for match in re.finditer(
            rf'^\s*defparam\s+({NAME})\s*\.power_up\s*=\s*"(low|high)"\s*;',
            source, re.M):
        instance, value = match.groups()
        if instance in power:
            raise ValueError('Duplicate power-up parameter: ' + instance)
        power[instance] = int(value == 'high')

    def evaluate(expression, seen, trace):
        expression = expression.strip()
        if expression in ("1'b0", "1'b1"):
            return int(expression[-1])
        if expression.startswith(('!', '~')):
            return 1 - evaluate(expression[1:], seen, trace)
        if not re.fullmatch(NAME, expression):
            raise ValueError('Unsupported startup expression: ' + expression)
        if expression in seen:
            raise ValueError('Cyclic startup cone: ' + expression)
        seen = seen | {expression}
        candidates = drivers.get(expression, [])
        if len(candidates) != 1:
            raise ValueError(f'Expected one supported driver for {expression}, got {len(candidates)}')
        driver = candidates[0]
        kind = driver[0]
        trace.append(dict(net=expression, kind=kind))
        if kind == 'assign':
            return evaluate(driver[1], seen, trace)
        instance, ports = driver[1:]
        trace[-1]['instance'] = instance
        if kind == 'cyclonev_io_obuf':
            # An enabled buffer is required; disabled/high-Z is not a logic value.
            if evaluate(ports.get('oe', ''), seen, []) != 1:
                raise ValueError('Output buffer not enabled: ' + instance)
            trace[-1]['input_expression'] = ports.get('i', '')
            return evaluate(ports.get('i', ''), seen, trace)
        for control, inactive in (('clrn', 1), ('prn', 1), ('aload', 0)):
            if evaluate(ports.get(control, ''), seen, []) != inactive:
                raise ValueError('Register asynchronous control active: ' + instance + '.' + control)
        if instance not in power:
            raise ValueError('Missing explicit power-up state: ' + instance)
        trace[-1]['physical_register_power_up'] = power[instance]
        return power[instance]

    observations = {}
    for output, wanted in expected.items():
        if not re.search(rf'^\s*output\s+{re.escape(output)}\s*;', source, re.M):
            raise ValueError('Expected scalar top-level output absent: ' + output)
        trace = []
        actual = evaluate(output, set(), trace)
        observations[output] = dict(expected=wanted, actual=actual, passed=actual == wanted, trace=trace)
    return observations


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('netlist', type=Path)
    parser.add_argument('--expect', action='append', required=True, metavar='OUTPUT=BIT')
    parser.add_argument('--result', type=Path, required=True, help='Fresh JSON result file')
    args = parser.parse_args()
    result = dict(passed=False, hardware_accessed=False, external_qualified=False,
                  scope='Compiler functional-netlist scalar output startup values',
                  assumption='Before clock edges, with global device clear and power-on reset released')
    # Reserve the result before reading input. Existing evidence is immutable.
    with args.result.open('x') as destination:
        try:
            expected = {}
            for item in args.expect:
                name, bit = item.split('=')
                if name in expected or bit not in ('0', '1'):
                    raise ValueError('Invalid or duplicate output expectation: ' + item)
                expected[name] = int(bit)
            content = args.netlist.read_bytes()
            result['netlist_sha256'] = hashlib.sha256(content).hexdigest()
            result['outputs'] = check(content.decode(), expected)
            result['passed'] = all(item['passed'] for item in result['outputs'].values())
        except (ValueError, OSError) as error:
            result['error'] = str(error)
        destination.write(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
    raise SystemExit(0 if result['passed'] else 1)


if __name__ == '__main__':
    main()
