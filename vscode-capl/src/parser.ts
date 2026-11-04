/**
 * Lightweight CAPL source analysis. Pure TypeScript (no `vscode` import).
 *
 * The approach is deliberately tolerant: comments, strings and preprocessor lines are
 * masked out (replaced by spaces, keeping offsets), then top-level constructs are found by
 * tracking brace depth. This keeps working while the user is in the middle of typing.
 */

import { ParamDef, parseParamText } from './registry';

export type SymbolKind =
  | 'variables'
  | 'includes'
  | 'event'
  | 'function'
  | 'testcase'
  | 'testfunction'
  | 'struct'
  | 'enum'
  | 'variable';

export interface CaplSymbol {
  kind: SymbolKind;
  name: string;
  /** e.g. event selector, return type, or variable type */
  detail: string;
  start: number;
  end: number;
  nameStart: number;
  nameEnd: number;
  params?: ParamDef[];
  returnType?: string;
  /** Comment directly above the definition. */
  doc?: string;
  children?: CaplSymbol[];
}

export interface Include {
  path: string;
  start: number;
  end: number;
}

export interface ParsedDocument {
  masked: string;
  symbols: CaplSymbol[];
  includes: Include[];
  /** Top-level `{...}` and block comment ranges, for folding. */
  blocks: Array<{ start: number; end: number; comment?: boolean }>;
  problems: Problem[];
}

export interface Problem {
  start: number;
  end: number;
  message: string;
  severity: 'error' | 'warning' | 'info';
  code?: string;
}

const CONTROL_WORDS = new Set([
  'if', 'else', 'for', 'while', 'do', 'switch', 'case', 'return', 'sizeof',
  'on', 'testcase', 'testfunction', 'variables', 'includes', 'struct', 'enum', 'default',
]);

/**
 * Replaces comments, string/char literal contents and preprocessor lines by spaces.
 * Newlines are preserved so offsets and line numbers stay valid.
 */
export function maskText(text: string): string {
  const out = text.split('');
  const n = text.length;
  let i = 0;
  let lineStart = true;
  const blank = (from: number, to: number) => {
    for (let k = from; k < to && k < n; k++) if (out[k] !== '\n' && out[k] !== '\r') out[k] = ' ';
  };
  while (i < n) {
    const c = text[i];
    const d = text[i + 1];
    if (c === '\n') {
      lineStart = true;
      i++;
      continue;
    }
    if (lineStart && c === '#') {
      let j = i;
      while (j < n && text[j] !== '\n') j++;
      blank(i, j);
      i = j;
      continue;
    }
    if (c !== ' ' && c !== '\t' && c !== '\r') lineStart = false;
    if (c === '/' && d === '/') {
      let j = i;
      while (j < n && text[j] !== '\n') j++;
      blank(i, j);
      i = j;
      continue;
    }
    if (c === '/' && d === '*') {
      const end = text.indexOf('*/', i + 2);
      const j = end < 0 ? n : end + 2;
      blank(i, j);
      i = j;
      continue;
    }
    if (c === '"' || c === "'") {
      let j = i + 1;
      while (j < n && text[j] !== c && text[j] !== '\n') {
        if (text[j] === '\\') j++;
        j++;
      }
      // keep the quotes, blank the content
      blank(i + 1, Math.min(j, n));
      i = j < n && text[j] === c ? j + 1 : j;
      continue;
    }
    i++;
  }
  return out.join('');
}

/** Returns the comment block directly above `offset` (doc comment or consecutive // lines). */
export function leadingComment(text: string, offset: number): string | undefined {
  const lineStart = text.lastIndexOf('\n', offset - 1) + 1;
  const before = text.slice(0, lineStart).replace(/[ \t\r]*\n?$/, '');
  if (before.endsWith('*/')) {
    const open = before.lastIndexOf('/*');
    if (open < 0) return undefined;
    const body = before.slice(open + 2, -2).replace(/^\*+/, '');
    const cleaned = body
      .split('\n')
      .map((l) => l.replace(/^\s*\*?\s?/, '').trimEnd())
      .join('\n')
      .trim();
    return cleaned || undefined;
  }
  const lines: string[] = [];
  const all = before.split('\n');
  for (let k = all.length - 1; k >= 0; k--) {
    const m = /^\s*\/\/\/?\s?(.*)$/.exec(all[k]);
    if (!m) break;
    lines.unshift(m[1].trimEnd());
  }
  const s = lines.join('\n').trim();
  return s || undefined;
}

/** Finds the matching closing bracket for the opener at `open` in masked text. */
export function matchBracket(masked: string, open: number): number {
  const o = masked[open];
  const c = o === '(' ? ')' : o === '[' ? ']' : '}';
  let depth = 0;
  for (let i = open; i < masked.length; i++) {
    const ch = masked[i];
    if (ch === o) depth++;
    else if (ch === c) {
      depth--;
      if (depth === 0) return i;
    }
  }
  return -1;
}

/** Splits `s` at top-level occurrences of `sep` (ignoring nested (), [], {}). Returns [start,end) ranges. */
export function splitTopLevel(s: string, sep: string, base = 0): Array<[number, number]> {
  const out: Array<[number, number]> = [];
  let depth = 0;
  let from = 0;
  for (let i = 0; i < s.length; i++) {
    const ch = s[i];
    if (ch === '(' || ch === '[' || ch === '{') depth++;
    else if (ch === ')' || ch === ']' || ch === '}') depth--;
    else if (ch === sep && depth === 0) {
      out.push([base + from, base + i]);
      from = i + 1;
    }
  }
  out.push([base + from, base + s.length]);
  return out;
}

function trimRange(s: string, [a, b]: [number, number]): [number, number] {
  while (a < b && /\s/.test(s[a])) a++;
  while (b > a && /\s/.test(s[b - 1])) b--;
  return [a, b];
}

export function parseParams(masked: string, open: number, close: number): ParamDef[] {
  const inner = masked.slice(open + 1, close);
  if (inner.trim() === '' || inner.trim() === 'void') return [];
  return splitTopLevel(inner, ',').map(([a, b]) => parseParamText(inner.slice(a, b)));
}

/**
 * Parses declarations like `int a = 1, b[10]; message 0x100 m; msTimer t;` inside the
 * given range of masked text.
 */
export function parseDeclarations(masked: string, start: number, end: number, typeNames?: Set<string>): CaplSymbol[] {
  const out: CaplSymbol[] = [];
  const body = masked.slice(start, end);
  for (const stmt of splitTopLevel(body, ';', start)) {
    const [a, b] = trimRange(masked, stmt);
    if (a >= b) continue;
    let text = masked.slice(a, b);
    let offset = a;
    // struct/enum type definitions inside a variables block
    const typeDef = /^(?:_align\s*\(\s*\d+\s*\)\s*)?(struct|enum)\s+([A-Za-z_]\w*)\s*\{/.exec(text);
    if (typeDef) {
      const open = a + typeDef[0].length - 1;
      const close = matchBracket(masked, open);
      const nameStart = a + typeDef[0].indexOf(typeDef[2], typeDef[1].length);
      out.push({
        kind: typeDef[1] as SymbolKind,
        name: typeDef[2],
        detail: typeDef[1],
        start: a,
        end: close < 0 ? b : close + 1,
        nameStart,
        nameEnd: nameStart + typeDef[2].length,
        children: typeDef[1] === 'struct' && close > 0 ? parseDeclarations(masked, open + 1, close) : undefined,
      });
      if (close < 0 || close + 1 >= b) continue;
      // `struct P { ... } p;`
      text = `${typeDef[1]} ${typeDef[2]} ` + masked.slice(close + 1, b);
      offset = close + 1 - (typeDef[1].length + typeDef[2].length + 2);
    }
    if (/^\s*(return|if|for|while|switch|do|else|case|break|continue)\b/.test(text)) continue;
    const parts = splitTopLevel(text, ',');
    let type = '';
    parts.forEach((range, idx) => {
      const [pa, pb] = trimRange(text, range);
      const part = text.slice(pa, pb);
      const eq = splitTopLevel(part, '=')[0];
      const declText = part.slice(0, eq[1]).trimEnd();
      const m = idx === 0
        ? /^((?:const\s+|static\s+)*.*?)\b([A-Za-z_]\w*)\s*((?:\[(?:[^[\]]|\[[^\]]*\])*\]\s*)*)$/.exec(declText)
        : /^()\s*\**\s*([A-Za-z_]\w*)\s*((?:\[(?:[^[\]]|\[[^\]]*\])*\]\s*)*)$/.exec(declText);
      if (!m) return;
      if (idx === 0) {
        type = m[1].trim().replace(/\s+/g, ' ');
        if (type === '') return;
        const firstWord = type.replace(/^(const|static)\s+/g, '').split(/[\s*]/)[0];
        if (typeNames && !typeNames.has(firstWord) && !/^(struct|enum)$/.test(firstWord)) return;
        if (CONTROL_WORDS.has(firstWord)) return;
      }
      const nameIdx = pa + declText.lastIndexOf(m[2], declText.length - m[3].length);
      const nameStart = offset + nameIdx;
      out.push({
        kind: 'variable',
        name: m[2],
        detail: type + (m[3] ? m[3].replace(/\s+/g, '') : ''),
        start: offset + pa,
        end: offset + pb,
        nameStart,
        nameEnd: nameStart + m[2].length,
      });
    });
  }
  return out;
}

/** Parses a CAPL document into symbols, includes, foldable blocks and structural problems. */
export function parseDocument(text: string): ParsedDocument {
  const masked = maskText(text);
  const symbols: CaplSymbol[] = [];
  const includes: Include[] = [];
  const blocks: ParsedDocument['blocks'] = [];
  const problems: Problem[] = [];

  // includes — anywhere (normally inside `includes { }`)
  const incRe = /^[ \t]*#[ \t]*include[ \t]+["<]([^">\n]+)[">]/gm;
  for (let m; (m = incRe.exec(text)); ) includes.push({ path: m[1].trim(), start: m.index, end: m.index + m[0].length });

  // block comments for folding
  const cmRe = /\/\*[\s\S]*?\*\//g;
  for (let m; (m = cmRe.exec(text)); ) {
    if (masked[m.index] === ' ' && text.slice(m.index, m.index + m[0].length).includes('\n')) {
      blocks.push({ start: m.index, end: m.index + m[0].length - 1, comment: true });
    }
  }

  // bracket balance
  const stack: Array<{ ch: string; at: number }> = [];
  const pairs: Record<string, string> = { ')': '(', ']': '[', '}': '{' };
  for (let i = 0; i < masked.length; i++) {
    const ch = masked[i];
    if (ch === '(' || ch === '[' || ch === '{') stack.push({ ch, at: i });
    else if (ch === ')' || ch === ']' || ch === '}') {
      const top = stack[stack.length - 1];
      if (top && top.ch === pairs[ch]) {
        stack.pop();
        if (ch === '}' && text.slice(top.at, i).includes('\n')) blocks.push({ start: top.at, end: i });
      } else if (top && ch === '}' && top.ch !== '{') {
        // unclosed ( or [ before a closing brace
        problems.push({ start: top.at, end: top.at + 1, message: `Unclosed '${top.ch}'`, severity: 'error', code: 'unbalanced' });
        stack.pop();
        i--; // re-process this '}'
      } else {
        problems.push({ start: i, end: i + 1, message: `Unexpected '${ch}'`, severity: 'error', code: 'unbalanced' });
      }
    }
  }
  for (const s of stack) {
    problems.push({ start: s.at, end: s.at + 1, message: `Unclosed '${s.ch}'`, severity: 'error', code: 'unbalanced' });
  }

  // top-level constructs
  let headerStart = 0;
  let i = 0;
  const n = masked.length;
  while (i < n) {
    const ch = masked[i];
    if (ch === ';') {
      // top-level declaration (e.g. in .cin without variables block) or forward decl
      headerStart = i + 1;
      i++;
      continue;
    }
    if (ch === '}') {
      headerStart = i + 1;
      i++;
      continue;
    }
    if (ch !== '{') {
      i++;
      continue;
    }
    const close = matchBracket(masked, i);
    const end = close < 0 ? n : close + 1;
    const rawHeader = masked.slice(headerStart, i);
    const lead = rawHeader.length - rawHeader.trimStart().length;
    const hStart = headerStart + lead;
    const header = rawHeader.trim().replace(/\s+/g, ' ');
    const sym = classifyHeader(text, masked, header, hStart, i, end);
    if (sym) {
      symbols.push(sym);
    }
    // struct/enum at top level end with `};`
    let next = end;
    if (sym && (sym.kind === 'struct' || sym.kind === 'enum')) {
      const semi = masked.indexOf(';', end);
      if (semi >= 0 && masked.slice(end, semi).trim() === '') next = semi + 1;
    }
    headerStart = next;
    i = next;
  }

  // structural checks
  const vars = symbols.filter((s) => s.kind === 'variables');
  for (const dup of vars.slice(1)) {
    problems.push({ start: dup.nameStart, end: dup.nameEnd, message: 'Duplicate "variables" section', severity: 'error', code: 'duplicate-section' });
  }
  const seenEvents = new Map<string, CaplSymbol>();
  for (const s of symbols) {
    if (s.kind !== 'event') continue;
    const key = `${s.name} ${s.detail}`.replace(/\s+/g, ' ').trim();
    if (seenEvents.has(key)) {
      problems.push({ start: s.nameStart, end: s.nameEnd, message: `Duplicate event procedure "on ${key}"`, severity: 'error', code: 'duplicate-event' });
    } else seenEvents.set(key, s);
  }
  const seenTests = new Set<string>();
  for (const s of symbols) {
    if (s.kind !== 'testcase' && s.kind !== 'testfunction') continue;
    if (seenTests.has(s.name)) {
      problems.push({ start: s.nameStart, end: s.nameEnd, message: `Duplicate ${s.kind} "${s.name}"`, severity: 'error', code: 'duplicate-test' });
    }
    seenTests.add(s.name);
  }

  return { masked, symbols, includes, blocks, problems };
}

function findWord(masked: string, word: string, from: number, to: number): number | undefined {
  const re = new RegExp(`\\b${word}\\b`, 'g');
  re.lastIndex = from;
  const m = re.exec(masked);
  return m && m.index < to ? m.index : undefined;
}

/** Start offset of the identifier that ends right before `pos` (skipping whitespace). */
function lastWordBefore(masked: string, pos: number): number {
  let j = pos - 1;
  while (j >= 0 && /\s/.test(masked[j])) j--;
  while (j >= 0 && /\w/.test(masked[j])) j--;
  return j + 1;
}

function classifyHeader(text: string, masked: string, header: string, hStart: number, open: number, end: number): CaplSymbol | undefined {
  const close = end - 1;
  const at = (word: string, from = hStart) => findWord(masked, word, from, open) ?? hStart;
  if (header === 'variables' || header === 'includes') {
    const ns = at(header);
    return {
      kind: header,
      name: header,
      detail: '',
      start: hStart,
      end,
      nameStart: ns,
      nameEnd: ns + header.length,
      children: header === 'variables' ? parseDeclarations(masked, open + 1, close) : undefined,
    };
  }
  let m = /^on ([A-Za-z_]\w*)\b ?(.*)$/.exec(header);
  if (m) {
    const ns = at(m[1], hStart + 2);
    const selector = text
      .slice(ns + m[1].length, open)
      .replace(/\/\*[\s\S]*?\*\/|\/\/.*$/gm, ' ')
      .replace(/\s+/g, ' ')
      .trim();
    return {
      kind: 'event',
      name: m[1],
      detail: selector,
      start: hStart,
      end,
      nameStart: ns,
      nameEnd: ns + m[1].length,
      doc: leadingComment(text, hStart),
    };
  }
  m = /^(?:export )?(testcase|testfunction) ([A-Za-z_]\w*) ?\(/.exec(header);
  if (m) {
    const po = masked.indexOf('(', hStart);
    const ns = lastWordBefore(masked, po);
    const pc = matchBracket(masked, po);
    return {
      kind: m[1] as SymbolKind,
      name: m[2],
      detail: m[1],
      start: hStart,
      end,
      nameStart: ns,
      nameEnd: ns + m[2].length,
      params: pc > 0 && pc < open ? parseParams(masked, po, pc) : [],
      returnType: 'void',
      doc: leadingComment(text, hStart),
    };
  }
  m = /^(?:_align ?\( ?\d+ ?\) )?(struct|enum) ([A-Za-z_]\w*)$/.exec(header);
  if (m) {
    const ns = at(m[2], at(m[1]) + m[1].length);
    return {
      kind: m[1] as SymbolKind,
      name: m[2],
      detail: m[1],
      start: hStart,
      end,
      nameStart: ns,
      nameEnd: ns + m[2].length,
      children: m[1] === 'struct' ? parseDeclarations(masked, open + 1, close) : undefined,
      doc: leadingComment(text, hStart),
    };
  }
  m = /^(?:(?:export|static|inline) )*([A-Za-z_][\w ]*?(?: ?\*)?(?: ?\[ ?\])*) ([A-Za-z_]\w*) ?\(.*\)$/.exec(header);
  if (m && !CONTROL_WORDS.has(m[2])) {
    const po = masked.indexOf('(', hStart);
    const ns = lastWordBefore(masked, po);
    const pc = matchBracket(masked, po);
    return {
      kind: 'function',
      name: m[2],
      detail: m[1],
      start: hStart,
      end,
      nameStart: ns,
      nameEnd: ns + m[2].length,
      params: pc > 0 && pc < open ? parseParams(masked, po, pc) : [],
      returnType: m[1].replace(/^(export|static|inline) /, ''),
      doc: leadingComment(text, hStart),
    };
  }
  return undefined;
}

/** Symbols visible at `offset`: globals plus locals/params of the enclosing function. */
export function localSymbols(doc: ParsedDocument, offset: number, typeNames: Set<string>): CaplSymbol[] {
  const owner = doc.symbols.find((s) => s.start <= offset && offset <= s.end && s.kind !== 'variables' && s.kind !== 'includes');
  if (!owner) return [];
  const out: CaplSymbol[] = [];
  for (const p of owner.params ?? []) {
    if (p.name === '...') continue;
    out.push({ kind: 'variable', name: p.name, detail: `${p.type ?? ''} (parameter)`.trim(), start: owner.start, end: owner.end, nameStart: owner.nameStart, nameEnd: owner.nameEnd });
  }
  const open = doc.masked.indexOf('{', owner.nameEnd);
  if (open < 0) return out;
  const allTypes = new Set(typeNames);
  for (const s of doc.symbols) if (s.kind === 'struct' || s.kind === 'enum') allTypes.add(s.name);
  // scan all statement starts inside the body
  const body = doc.masked.slice(open + 1, owner.end - 1);
  const stmtRe = /(^|[;{}])\s*((?:const\s+|static\s+)?([A-Za-z_]\w*)[^;{}]*?);/g;
  for (let m; (m = stmtRe.exec(body)); ) {
    stmtRe.lastIndex = m.index + m[0].length - 1; // let this ';' start the next statement
    const typeWord = m[3];
    if (!allTypes.has(typeWord) && typeWord !== 'struct' && typeWord !== 'enum' && typeWord !== 'const' && typeWord !== 'static') continue;
    const s0 = open + 1 + m.index + m[0].length - 1 - m[2].length;
    out.push(...parseDeclarations(doc.masked, s0, s0 + m[2].length, allTypes));
  }
  return out;
}

export interface CallContext {
  name: string;
  nameStart: number;
  openParen: number;
  activeParam: number;
}

/** Finds the innermost unclosed call around `offset` (for signature help). */
export function callContextAt(masked: string, offset: number): CallContext | undefined {
  let depth = 0;
  let commas = 0;
  for (let i = offset - 1; i >= 0; i--) {
    const ch = masked[i];
    if (ch === ')' || ch === ']') depth++;
    else if (ch === '[') {
      if (depth === 0) return undefined;
      depth--;
    } else if (ch === '(') {
      if (depth === 0) {
        let j = i - 1;
        while (j >= 0 && /\s/.test(masked[j])) j--;
        const e = j + 1;
        while (j >= 0 && /\w/.test(masked[j])) j--;
        const name = masked.slice(j + 1, e);
        if (!/^[A-Za-z_]\w*$/.test(name) || CONTROL_WORDS.has(name)) return undefined;
        return { name, nameStart: j + 1, openParen: i, activeParam: commas };
      }
      depth--;
    } else if (ch === ',' && depth === 0) commas++;
    else if ((ch === ';' || ch === '{' || ch === '}') && depth === 0) return undefined;
  }
  return undefined;
}

export interface CallSite {
  name: string;
  nameStart: number;
  openParen: number;
  closeParen: number;
  /** Trimmed [start,end) ranges of each argument; empty for `f()`. */
  args: Array<[number, number]>;
}

/** Finds all function calls in masked text (definitions and control keywords excluded). */
export function findCalls(doc: ParsedDocument, from = 0, to = doc.masked.length): CallSite[] {
  const masked = doc.masked;
  const defs = new Set(doc.symbols.filter((s) => s.params).map((s) => s.nameStart));
  const out: CallSite[] = [];
  const re = /\b([A-Za-z_]\w*)\s*\(/g;
  re.lastIndex = from;
  for (let m; (m = re.exec(masked)) && m.index < to; ) {
    const name = m[1];
    if (CONTROL_WORDS.has(name) || defs.has(m.index)) continue;
    // skip declarations like `message 0x1 m(...)` unlikely; skip member selectors `.byte(` too
    const prev = masked.slice(Math.max(0, m.index - 1), m.index);
    if (prev === '.') continue;
    const open = m.index + m[0].length - 1;
    const close = matchBracket(masked, open);
    if (close < 0) continue;
    const inner = masked.slice(open + 1, close);
    const args = inner.trim() === '' ? [] : splitTopLevel(inner, ',', open + 1).map((r) => trimRange(masked, r));
    out.push({ name, nameStart: m.index, openParen: open, closeParen: close, args });
  }
  return out;
}

/** The identifier before the cursor and what precedes it, for completion context. */
export function completionContext(masked: string, offset: number): { kind: 'event' | 'member' | 'sysvar' | 'signal' | 'general'; prefix: string } {
  let j = offset;
  while (j > 0 && /\w/.test(masked[j - 1])) j--;
  const prefix = masked.slice(j, offset);
  const lineStart = masked.lastIndexOf('\n', j - 1) + 1;
  const before = masked.slice(lineStart, j);
  if (/^\s*on\s+$/.test(before)) return { kind: 'event', prefix };
  if (/\.\s*$/.test(before)) return { kind: 'member', prefix };
  if (/@(?:[\w]+::)*$/.test(before)) return { kind: 'sysvar', prefix };
  if (/\$(?:[\w]+::)*$/.test(before)) return { kind: 'signal', prefix };
  return { kind: 'general', prefix };
}

/** True if `text` (original source of an argument) is a literal. */
export function isLiteral(text: string): boolean {
  const t = text.trim();
  return (
    /^-?(0[xX][0-9A-Fa-f]+|0[bB][01]+|\d+(\.\d*)?([eE][+-]?\d+)?|\.\d+)[uUlLfF]*$/.test(t) ||
    /^"([^"\\]|\\.)*"$/.test(t) ||
    /^'([^'\\]|\\.)+'$/.test(t) ||
    /^(true|false|TRUE|FALSE)$/.test(t)
  );
}

/** Converts an offset into a 0-based line/character pair. */
export function positionAt(text: string, offset: number): { line: number; character: number } {
  let line = 0;
  let last = -1;
  for (let i = 0; i < offset && i < text.length; i++) {
    if (text[i] === '\n') {
      line++;
      last = i;
    }
  }
  return { line, character: offset - last - 1 };
}
