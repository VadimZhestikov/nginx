#!/usr/bin/env python3
"""
html2fb2.py — Convert asciidoctor HTML output to FictionBook 2.0 (FB2).

Usage:
    python3 html2fb2.py input.html output.fb2

FB2 spec: http://www.gribuser.ru/xml/fictionbook/2.0/
"""

import sys
import re
import uuid
from xml.etree.ElementTree import Element, SubElement, ElementTree, indent
from bs4 import BeautifulSoup, NavigableString, Tag

# ── FB2 namespaces ──────────────────────────────────────────────────────────
FB2_NS = "http://www.gribuser.ru/xml/fictionbook/2.0"
XLINK_NS = "http://www.w3.org/1999/xlink"


# ── Helpers ─────────────────────────────────────────────────────────────────

def fb2_elem(tag, parent=None, **attrib):
    if parent is None:
        el = Element(tag, attrib)
    else:
        el = SubElement(parent, tag, attrib)
    return el


def append_text(el, text):
    """Append text to element, merging with existing tail/text."""
    if len(el) == 0:
        el.text = (el.text or "") + text
    else:
        last = el[-1]
        last.tail = (last.tail or "") + text


def strip_ws(s):
    return re.sub(r'\s+', ' ', s or '').strip()


# ── Inline content converter ─────────────────────────────────────────────────

def convert_inline(node, parent_el):
    """Recursively convert inline HTML nodes into FB2 inline elements."""
    for child in node.children:
        if isinstance(child, NavigableString):
            text = str(child)
            # Collapse whitespace but preserve single spaces
            text = re.sub(r'\s+', ' ', text)
            append_text(parent_el, text)

        elif isinstance(child, Tag):
            name = child.name

            if name in ('strong', 'b'):
                el = SubElement(parent_el, 'strong')
                convert_inline(child, el)

            elif name in ('em', 'i'):
                el = SubElement(parent_el, 'emphasis')
                convert_inline(child, el)

            elif name == 'code':
                el = SubElement(parent_el, 'code')
                el.text = child.get_text()

            elif name == 'a':
                href = child.get('href', '')
                if href.startswith('#'):
                    el = SubElement(parent_el, 'a',
                                    {'{%s}href' % XLINK_NS: href})
                    convert_inline(child, el)
                else:
                    # External link: render as "text [url]"
                    convert_inline(child, parent_el)
                    if href:
                        append_text(parent_el, f' [{href}]')

            elif name == 'br':
                append_text(parent_el, '\n')

            elif name in ('span', 'sup', 'sub', 'mark', 'del', 's'):
                # Pass through content
                convert_inline(child, parent_el)

            else:
                # Unknown inline — pass through text
                convert_inline(child, parent_el)


# ── Block content converter ──────────────────────────────────────────────────

def is_block(tag_name):
    return tag_name in ('p', 'pre', 'div', 'ul', 'ol', 'table',
                        'blockquote', 'h1', 'h2', 'h3', 'h4',
                        'hr', 'section', 'article', 'aside',
                        'figure', 'figcaption')


def convert_paragraph(node, section_el):
    """Convert a <p> node to a FB2 <p>."""
    p = SubElement(section_el, 'p')
    convert_inline(node, p)
    return p


def convert_code_block(node, section_el):
    """
    Convert <pre> (code block) to a FB2 <poem><stanza>.
    FB2 has no native preformatted block; <poem> preserves line structure.
    """
    # asciidoctor wraps code in <pre><code class="language-*">...</code></pre>
    code_node = node.find('code') or node
    raw = code_node.get_text()

    poem = SubElement(section_el, 'poem')
    stanza = SubElement(poem, 'stanza')
    for line in raw.split('\n'):
        v = SubElement(stanza, 'v')
        v.text = line
    return poem


def convert_list(node, section_el, depth=0):
    """Convert <ul>/<ol> to FB2 paragraphs with bullet/number prefixes."""
    ordered = (node.name == 'ol')
    items = [c for c in node.children
             if isinstance(c, Tag) and c.name == 'li']
    for idx, li in enumerate(items, 1):
        prefix = f'{idx}. ' if ordered else '• '
        indent_str = '    ' * depth
        p = SubElement(section_el, 'p')
        # Put the prefix as text directly on <p>
        p.text = indent_str + prefix
        # Convert li content inline; handle nested lists separately
        for child in li.children:
            if isinstance(child, Tag) and child.name in ('ul', 'ol'):
                convert_list(child, section_el, depth + 1)
            elif isinstance(child, Tag) and is_block(child.name):
                convert_block(child, section_el)
            else:
                # inline content — attach to the same <p>
                tmp_wrapper = Tag(name='span', parser='lxml')
                tmp_wrapper.append(child.__copy__())
                convert_inline(tmp_wrapper, p)


def convert_table(node, section_el):
    """
    Convert <table> to a series of FB2 <p> rows.
    FB2 has no native table element; we render as fixed-width text lines.
    """
    rows = node.find_all('tr')
    for row in rows:
        cells = row.find_all(['th', 'td'])
        texts = [strip_ws(c.get_text()) for c in cells]
        line = ' │ '.join(texts)
        p = SubElement(section_el, 'p')
        p.text = line

    # Add a thin separator after the table
    p = SubElement(section_el, 'empty-line')


def convert_block(node, section_el):
    """Dispatch a block-level node to the right converter."""
    name = node.name
    classes = node.get('class', [])

    if name == 'p':
        convert_paragraph(node, section_el)

    elif name == 'pre':
        convert_code_block(node, section_el)

    elif name in ('ul', 'ol'):
        convert_list(node, section_el)

    elif name == 'table':
        convert_table(node, section_el)

    elif name == 'blockquote':
        # Render blockquote as indented paragraph
        p = SubElement(section_el, 'p')
        p.text = '  '
        convert_inline(node, p)

    elif name == 'hr':
        SubElement(section_el, 'empty-line')

    elif name in ('div', 'aside', 'figure'):
        # Recurse into generic containers
        convert_children(node, section_el)

    elif name in ('h1', 'h2', 'h3', 'h4'):
        # Heading inside a block context — treat as sub-title paragraph
        p = SubElement(section_el, 'p')
        strong = SubElement(p, 'strong')
        convert_inline(node, strong)

    else:
        convert_children(node, section_el)


def convert_children(node, section_el):
    """Convert all direct children of node as block or inline content."""
    for child in node.children:
        if isinstance(child, NavigableString):
            text = str(child).strip()
            if text:
                p = SubElement(section_el, 'p')
                p.text = text
        elif isinstance(child, Tag):
            if is_block(child.name):
                convert_block(child, section_el)
            else:
                # Orphaned inline node — wrap in <p>
                p = SubElement(section_el, 'p')
                convert_inline(child, p)


# ── Section builder ──────────────────────────────────────────────────────────

def heading_level(tag_name):
    m = re.match(r'h(\d)', tag_name)
    return int(m.group(1)) if m else 0


def build_sections(content_el, body_el, doc_title):
    """
    Walk the asciidoctor HTML content div and build a hierarchy of
    FB2 <section> elements that mirrors the heading structure.
    """
    # Stack of (level, section_element)
    stack = []

    def current_section():
        return stack[-1][1] if stack else body_el

    def open_section(level, title_text):
        # Close sections that are at the same or deeper level
        while stack and stack[-1][0] >= level:
            stack.pop()
        parent = current_section()
        sec = SubElement(parent, 'section')
        title_el = SubElement(sec, 'title')
        p = SubElement(title_el, 'p')
        p.text = title_text
        stack.append((level, sec))
        return sec

    children = list(content_el.children)
    i = 0
    while i < len(children):
        node = children[i]
        i += 1

        if isinstance(node, NavigableString):
            text = str(node).strip()
            if text:
                sec = current_section()
                p = SubElement(sec, 'p')
                p.text = text
            continue

        if not isinstance(node, Tag):
            continue

        name = node.name
        classes = node.get('class', [])

        # ── Headings → open new section ──────────────────────────────────
        if re.match(r'h[1-4]', name):
            level = heading_level(name)
            title_text = node.get_text(strip=True)
            # Strip leading section numbers like "1. " or "1.2.3. "
            title_text = re.sub(r'^[\d.]+\s+', '', title_text)
            open_section(level, title_text)
            continue

        # ── asciidoctor sect1/sect2/sect3 divs — recurse ─────────────────
        if name == 'div' and any(c.startswith('sect') for c in classes):
            # Find the heading inside this sect div
            heading = node.find(re.compile(r'h[1-4]'))
            if heading:
                level = heading_level(heading.name)
                title_text = heading.get_text(strip=True)
                title_text = re.sub(r'^[\d.]+\s+', '', title_text)
                sec = open_section(level, title_text)
                # Recurse into the sectionbody
                sectionbody = node.find('div', class_='sectionbody')
                if sectionbody:
                    _recurse_sectionbody(sectionbody, sec)
                else:
                    # No sectionbody wrapper — recurse directly, skip heading
                    for child in node.children:
                        if isinstance(child, Tag) and child != heading:
                            if re.match(r'h[1-4]', child.name):
                                lvl2 = heading_level(child.name)
                                t2 = re.sub(r'^[\d.]+\s+', '',
                                            child.get_text(strip=True))
                                open_section(lvl2, t2)
                            elif is_block(child.name):
                                convert_block(child, current_section())
                            else:
                                convert_children(child, current_section())
            else:
                convert_children(node, current_section())
            continue

        # ── Table of contents div — skip ─────────────────────────────────
        if name == 'div' and 'toc' in classes:
            continue

        # ── preamble / content div — recurse ─────────────────────────────
        if name == 'div' and ('preamble' in classes or 'content' in classes):
            _recurse_sectionbody(node, current_section())
            continue

        # ── admonition blocks (NOTE, TIP, WARNING, CAUTION, IMPORTANT) ───
        if name == 'div' and any(c in classes for c in
                                 ('admonitionblock', 'note', 'tip',
                                  'warning', 'caution', 'important')):
            convert_admonition(node, current_section())
            continue

        # ── listing / literal / source blocks ────────────────────────────
        if name == 'div' and any(c in classes for c in
                                 ('listingblock', 'literalblock')):
            pre = node.find('pre')
            if pre:
                convert_code_block(pre, current_section())
            continue

        # ── image blocks — skip (FB2 images require base64 embedding) ────
        if name == 'div' and 'imageblock' in classes:
            continue

        # ── paragraph block ───────────────────────────────────────────────
        if name == 'div' and 'paragraph' in classes:
            p_node = node.find('p')
            if p_node:
                convert_paragraph(p_node, current_section())
            continue

        # ── ulist / olist ─────────────────────────────────────────────────
        if name == 'div' and any(c in ('ulist', 'olist') for c in classes):
            list_node = node.find(['ul', 'ol'])
            if list_node:
                convert_list(list_node, current_section())
            continue

        # ── table wrapper ─────────────────────────────────────────────────
        if name == 'div' and 'tableblock' in classes:
            tbl = node.find('table')
            if tbl:
                convert_table(tbl, current_section())
            continue

        if name == 'table':
            convert_table(node, current_section())
            continue

        # ── generic block fallback ────────────────────────────────────────
        if is_block(name):
            convert_block(node, current_section())
        else:
            sec = current_section()
            p = SubElement(sec, 'p')
            convert_inline(node, p)


def _recurse_sectionbody(body_node, section_el):
    """Process the children of a sectionbody div."""
    for child in body_node.children:
        if isinstance(child, NavigableString):
            continue
        if not isinstance(child, Tag):
            continue

        name = child.name
        classes = child.get('class', [])

        if name == 'div' and any(c.startswith('sect') for c in classes):
            # Nested section — handled by build_sections logic
            heading = child.find(re.compile(r'h[1-4]'))
            if heading:
                level = heading_level(heading.name)
                title_text = re.sub(r'^[\d.]+\s+', '',
                                    heading.get_text(strip=True))
                sub_sec = SubElement(section_el, 'section')
                title_el = SubElement(sub_sec, 'title')
                p = SubElement(title_el, 'p')
                p.text = title_text
                inner_body = child.find('div', class_='sectionbody')
                if inner_body:
                    _recurse_sectionbody(inner_body, sub_sec)
                else:
                    for c2 in child.children:
                        if isinstance(c2, Tag) and c2 != heading:
                            _dispatch_content(c2, sub_sec)
            else:
                _recurse_sectionbody(child, section_el)

        else:
            _dispatch_content(child, section_el)


def _dispatch_content(node, section_el):
    """Route a single content node to the right handler."""
    if not isinstance(node, Tag):
        return

    name = node.name
    classes = node.get('class', [])

    if name == 'div' and any(c in classes for c in
                             ('listingblock', 'literalblock')):
        pre = node.find('pre')
        if pre:
            convert_code_block(pre, section_el)

    elif name == 'div' and 'paragraph' in classes:
        p_node = node.find('p')
        if p_node:
            convert_paragraph(p_node, section_el)

    elif name == 'div' and any(c in ('ulist', 'olist') for c in classes):
        lst = node.find(['ul', 'ol'])
        if lst:
            convert_list(lst, section_el)

    elif name == 'div' and 'tableblock' in classes:
        tbl = node.find('table')
        if tbl:
            convert_table(tbl, section_el)

    elif name == 'div' and any(c in classes for c in
                               ('admonitionblock', 'note', 'tip',
                                'warning', 'caution', 'important')):
        convert_admonition(node, section_el)

    elif name == 'div' and 'imageblock' in classes:
        pass  # skip images

    elif name == 'div':
        # Generic div — recurse
        _recurse_sectionbody(node, section_el)

    elif name in ('p',):
        convert_paragraph(node, section_el)

    elif name in ('ul', 'ol'):
        convert_list(node, section_el)

    elif name == 'table':
        convert_table(node, section_el)

    elif name == 'pre':
        convert_code_block(node, section_el)

    elif name == 'hr':
        SubElement(section_el, 'empty-line')

    elif re.match(r'h[1-4]', name):
        # Stray heading inside a section body
        p = SubElement(section_el, 'p')
        strong = SubElement(p, 'strong')
        convert_inline(node, strong)

    else:
        if is_block(name):
            convert_block(node, section_el)


def convert_admonition(node, section_el):
    """Render NOTE/TIP/WARNING/IMPORTANT/CAUTION blocks as indented paragraphs."""
    # asciidoctor admonition: <div class="admonitionblock note"><table>...
    label = node.find(class_='title')
    if label is None:
        # Try the type cell
        type_cell = node.find('td', class_='icon')
        if type_cell:
            label_text = type_cell.get_text(strip=True) + ': '
        else:
            label_text = ''
    else:
        label_text = label.get_text(strip=True) + ': '

    content_cell = node.find('td', class_='content')
    if content_cell is None:
        content_cell = node

    p = SubElement(section_el, 'p')
    if label_text:
        strong = SubElement(p, 'strong')
        strong.text = label_text
    convert_inline(content_cell, p)


# ── FB2 document builder ─────────────────────────────────────────────────────

def build_fb2(soup):
    # Extract metadata
    doc_title = strip_ws(soup.title.get_text()) if soup.title else 'NGINX JS DOM Manual'
    doc_title = re.sub(r'\s*—\s*Asciidoctor.*', '', doc_title).strip()

    # Root element
    root = Element('FictionBook', {
        'xmlns':   FB2_NS,
        'xmlns:l': XLINK_NS,
    })

    # ── <description> ────────────────────────────────────────────────────────
    desc = SubElement(root, 'description')

    ti = SubElement(desc, 'title-info')
    SubElement(ti, 'genre').text = 'computers'
    author = SubElement(ti, 'author')
    SubElement(author, 'first-name').text = ''
    SubElement(author, 'last-name').text  = 'NGINX JS DOM Project'
    SubElement(ti, 'book-title').text = doc_title
    SubElement(ti, 'date').text = '2026-03-19'
    SubElement(ti, 'lang').text = 'en'

    di = SubElement(desc, 'document-info')
    da = SubElement(di, 'author')
    SubElement(da, 'nickname').text = 'asciidoctor + html2fb2.py'
    SubElement(di, 'date').text = '2026-03-19'
    SubElement(di, 'id').text = str(uuid.uuid4())
    SubElement(di, 'version').text = '1.0'

    # ── <body> ───────────────────────────────────────────────────────────────
    body = SubElement(root, 'body')

    # Title section
    title_el = SubElement(body, 'title')
    p = SubElement(title_el, 'p')
    p.text = doc_title

    # Find asciidoctor's main content div
    content = (soup.find('div', id='content') or
               soup.find('div', class_='content') or
               soup.body)

    if content is None:
        p2 = SubElement(body, 'section')
        SubElement(p2, 'p').text = '(empty document)'
    else:
        build_sections(content, body, doc_title)

    return root


# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    if len(sys.argv) != 3:
        print(f'Usage: {sys.argv[0]} input.html output.fb2', file=sys.stderr)
        sys.exit(1)

    html_path, fb2_path = sys.argv[1], sys.argv[2]

    with open(html_path, encoding='utf-8') as fh:
        html = fh.read()

    soup = BeautifulSoup(html, 'lxml')

    root = build_fb2(soup)

    # Pretty-print (Python 3.9+)
    try:
        indent(root, space='  ')
    except TypeError:
        pass  # Python < 3.9 — no indentation

    tree = ElementTree(root)
    tree.write(fb2_path, encoding='utf-8', xml_declaration=True)
    print(f'Written {fb2_path}')


if __name__ == '__main__':
    main()
