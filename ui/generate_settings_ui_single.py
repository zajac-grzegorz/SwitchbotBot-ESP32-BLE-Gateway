#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
One-file ESP32 settings UI generator.

Reads separate input files (schema.json, template.html, app.css, app.js)
and produces ONE self-contained HTML (CSS + JS + schema embedded).

Usage:
  python generate_settings_ui_single.py \
      --schema schema.json \
      --template template.html \
      --css app.css \
      --js app.js \
      --out settings_all_in_one.html \
      [--accent #22a6b3]
"""

import argparse
import json
import re
from pathlib import Path

_DEF_ACCENT = "#20a4a9"

def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")

def _write(path: Path, content: str):
    path.write_text(content, encoding="utf-8")

def inline_css(template: str, css_text: str) -> str:
    style_block = f"<style>\n{css_text}\n</style>"
    if "{{INLINE_CSS}}" in template:
        return template.replace("{{INLINE_CSS}}", style_block)
    # Try to replace a placeholder link tag if present (optional pattern)
    link_pattern = r'<link[^>]*href="\{\{CSS_PATH\}\}"[^>]*>'
    if re.search(link_pattern, template):
        return re.sub(link_pattern, style_block, template, count=1)
    # Fallback: inject before </head>
    if "</head>" in template:
        return template.replace("</head>", style_block + "\n</head>")
    # Last resort: prepend
    return style_block + "\n" + template

def inline_js(template: str, js_text: str) -> str:
    script_block = f"<script>\n{js_text}\n</script>"
    if "{{INLINE_JS}}" in template:
        return template.replace("{{INLINE_JS}}", script_block)
    # Try to replace a placeholder script src tag if present (optional pattern)
    script_pattern = r'<script[^>]*src="\{\{JS_PATH\}\}"[^>]*></script>'
    if re.search(script_pattern, template):
        return re.sub(script_pattern, script_block, template, count=1)
    # Fallback: inject before </body>
    if "</body>" in template:
        return template.replace("</body>", script_block + "\n</body>")
    # Last resort: append
    return template + "\n" + script_block

def inline_schema(template: str, schema_json: str) -> str:
    schema_block = f'<script id="schema" type="application/json">{schema_json}</script>'
    if "{{SCHEMA_INLINE}}" in template:
        return template.replace("{{SCHEMA_INLINE}}", schema_block)
    # Fallback: put before </body>
    if "</body>" in template:
        return template.replace("</body>", schema_block + "\n</body>")
    # Last resort: append
    return template + "\n" + schema_block

def main():
    ap = argparse.ArgumentParser(description="Generate ONE self-contained HTML from separate template/CSS/JS/schema")
    ap.add_argument('--schema', required=True, help='Path to schema.json')
    ap.add_argument('--template', required=True, help='Path to template.html')
    ap.add_argument('--css', required=True, help='Path to app.css')
    ap.add_argument('--js', required=True, help='Path to app.js')
    ap.add_argument('--out', required=True, help='Output HTML file path (single file)')
    ap.add_argument('--accent', default=None, help='Override accent color (e.g., #20a4a9)')
    args = ap.parse_args()

    schema_path = Path(args.schema)
    template_path = Path(args.template)
    css_path = Path(args.css)
    js_path = Path(args.js)
    out_path = Path(args.out)

    schema = json.loads(_read(schema_path))
    title = schema.get('title', 'ESP32 Settings')
    accent = args.accent or ((schema.get('theme') or {}).get('accent')) or _DEF_ACCENT
  
    # Read assets
    template = _read(template_path)
    css_text = _read(css_path).replace('ACCENT_COLOR', accent)
    js_text = _read(js_path)

    # Replace title token if present
    html = template.replace('{{TITLE}}', title)

    # Inline CSS, JS, and schema
    html = inline_css(html, css_text)
    html = inline_js(html, js_text)
    schema_min = json.dumps(schema, ensure_ascii=False, separators=(',', ':'))
    html = inline_schema(html, schema_min)

    # Clean any leftover old-path placeholders if any exist
    html = html.replace('{{CSS_PATH}}', '').replace('{{JS_PATH}}', '')

    _write(out_path, html)

if __name__ == '__main__':
    main()