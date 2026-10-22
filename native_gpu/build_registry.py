"""Build the runtime lookup table from public numeric material identities."""
import argparse
import json
import math
from pathlib import Path
import struct


def build(source, destination):
    spec = json.loads(Path(source).read_text(encoding='utf-8'))
    if spec.get('schema') != 'DBZMAT01' or not 0 < len(spec['entries']) <= 1024:
        raise ValueError('Invalid material registry schema/count')
    records = []
    seen = set()
    for entry in spec['entries']:
        vs, ps = (int(entry[name], 16) for name in ('vertex_fnv', 'pixel_fnv'))
        vb, pb, kind = (entry[name] for name in ('vertex_bytes', 'pixel_bytes', 'kind'))
        diffuse = entry['diffuse']
        if not (0 <= vs < 2**64 and 0 <= ps < 2**64 and
                0 < vb <= 65536 and 0 < pb <= 65536 and 0 <= kind <= 22 and
                len(diffuse) == 3 and all(math.isfinite(v) for v in diffuse)):
            raise ValueError('Invalid material registry entry')
        key = (vs, ps, vb, pb)
        if key in seen:
            raise ValueError('Duplicate material identity')
        seen.add(key)
        records.append(struct.pack('<2Q3I3f', vs, ps, vb, pb, kind, *diffuse))
    output = Path(destination)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(b'DBZMAT01' + struct.pack('<I', len(records)) + b''.join(records))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).with_name('materials.registry.json'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build(args.source, args.output)
