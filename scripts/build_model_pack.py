import argparse
import json
from pathlib import Path


def load_json(path: Path):
    return json.loads(path.read_text(encoding='utf-8'))


def main():
    ap = argparse.ArgumentParser(description='Merge rules JSON + signal-map JSON into a single model pack JSON')
    ap.add_argument('--rules', required=True, help='rules JSON path')
    ap.add_argument('--signals', required=True, help='signal-map JSON path')
    ap.add_argument('--out', required=True, help='output model pack path')
    ap.add_argument('--model-name', default='External Model Pack')
    ap.add_argument('--model-key', default='external_model_pack')
    ap.add_argument('--model-version', default='v1')
    ap.add_argument('--vendor', default='')
    ap.add_argument('--notes', default='')
    ap.add_argument('--schema', default='can-monitor-model-pack.v1')
    args = ap.parse_args()

    rules_obj = load_json(Path(args.rules))
    signal_obj = load_json(Path(args.signals))

    pack = {
        'schema': args.schema,
        'model_key': args.model_key,
        'model_name': args.model_name,
        'model_version': args.model_version,
        'vendor': args.vendor,
        'notes': args.notes,
        'rules': rules_obj.get('rules', []),
        'messages': signal_obj.get('messages', []),
    }

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(pack, ensure_ascii=False, indent=2), encoding='utf-8')
    print(f'OK: {out_path}')


if __name__ == '__main__':
    main()
