import sys, json
from graphify.build import build_from_json
from graphify.cluster import score_all
from graphify.analyze import god_nodes, surprising_connections, suggest_questions
from graphify.report import generate
from graphify.export import to_html
from pathlib import Path

extraction = json.loads(Path('graphify-out/.graphify_extract.json').read_text())
detection  = json.loads(Path('graphify-out/.graphify_detect.json').read_text())
analysis   = json.loads(Path('graphify-out/.graphify_analysis.json').read_text())

G = build_from_json(extraction)
communities = {int(k): v for k, v in analysis['communities'].items()}
cohesion = {int(k): v for k, v in analysis['cohesion'].items()}
tokens = {'input': extraction.get('input_tokens', 0), 'output': extraction.get('output_tokens', 0)}

# Step 5 - Label communities
labels = {
    0: "Viewer Utilities",
    1: "Lossy Quality & Analysis",
    2: "Coefficient Context Analysis",
    3: "Color Space Conversion",
    4: "Metadata Parsing & Editing",
    5: "Coefficient Tables",
    6: "Coefficient Presence",
    7: "Coefficient Spans",
    8: "Exif Import",
    9: "Container Format Parser",
    10: "Image I/O",
    11: "Lossless Decorrelation",
    12: "RANS Entropy Engine",
    13: "Metrics & Utilities",
    14: "Coefficient Signs",
    15: "Image Quality Metrics",
    16: "Prediction Modes",
    17: "wkenc CLI",
    18: "wkmeta-edit CLI",
    19: "wkdec CLI",
    20: "C++ API Types",
    21: "Thread Pool",
    22: "Metrics Tests",
    23: "Wkmeta API",
}
# Fallback for remaining
for cid in communities:
    if cid not in labels:
        labels[cid] = f"Community {cid}"

questions = suggest_questions(G, communities, labels)

report = generate(G, communities, cohesion, labels, analysis['gods'], analysis['surprises'], detection, tokens, 'src', suggested_questions=questions)
Path('graphify-out/GRAPH_REPORT.md').write_text(report, encoding='utf-8')
Path('graphify-out/.graphify_labels.json').write_text(json.dumps({str(k): v for k, v in labels.items()}))
print('Report updated with community labels')

# Step 6 - Generate HTML
to_html(G, communities, 'graphify-out/graph.html', community_labels=labels)
print('graph.html written - open in any browser, no server needed')
