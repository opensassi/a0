import markdown
import os
import re

blog_dir = "/home/pc/projects/opensassi/a0/blog"
out_dir = "/home/pc/projects/opensassi/opensassi-org/html/blog"

for fname in sorted(os.listdir(blog_dir)):
    if not fname.endswith(".md"):
        continue

    path = os.path.join(blog_dir, fname)
    with open(path) as f:
        text = f.read()

    title_match = re.search(r"^# (.+)", text)
    title = title_match.group(1) if title_match else fname

    date_match = re.search(r"\*\*Date:\*\*\s*(\S+)", text)
    date = date_match.group(1) if date_match else ""

    body_text = re.sub(r"^# .+\n?", "", text, count=1)
    md = markdown.Markdown(extensions=["fenced_code", "tables"])
    body = md.convert(body_text)

    html = f"""<!doctype html>
<html>
  <head>
    <title>{title} — openSASSI</title>
  </head>
  <style>
    a, a:hover {{ color: black; text-decoration: none; }}
    body {{ max-width: 720px; margin: 40px auto; padding: 0 20px; font-family: system-ui, sans-serif; line-height: 1.6; color: #111; }}
    h1 {{ font-size: 1.6rem; }}
    h2 {{ font-size: 1.2rem; margin-top: 2em; }}
    img {{ max-width: 100%; }}
    table {{ border-collapse: collapse; width: 100%; }}
    th, td {{ border: 1px solid #ccc; padding: 6px 10px; text-align: left; }}
    th {{ background: #f5f5f5; }}
    code {{ background: #f0f0f0; padding: 2px 5px; font-size: 0.9em; }}
    pre {{ background: #f5f5f5; padding: 12px; overflow-x: auto; }}
    pre code {{ background: none; padding: 0; }}
    hr {{ border: none; border-top: 1px solid #ddd; margin: 2em 0; }}
    .meta {{ color: #666; font-size: 0.9rem; margin-bottom: 2em; }}
    .back {{ margin-top: 3em; }}
  </style>
  <body>
    <a href="../index.html"><img src="../logo.png" alt="openSASSI" /></a>
    <h1>{title}</h1>
    <div class="meta">{date}</div>
    {body}
    <div class="back"><a href="../index.html">← openSASSI</a></div>
  </body>
</html>"""

    stem = fname[:-3]
    stem = re.sub(r"-[a-zA-Z0-9]{20,}$", "", stem)
    out_name = stem + ".html"
    out_path = os.path.join(out_dir, out_name)
    with open(out_path, "w") as f:
        f.write(html)
    print(f"Wrote {out_path}")
