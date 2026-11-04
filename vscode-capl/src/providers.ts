import * as vscode from 'vscode';
import { Analysis, IncludedFile, globalSymbols, rangeInText, toRange } from './analysis';
import { DefinitionManager } from './definitions';
import {
  CaplSymbol,
  ParsedDocument,
  callContextAt,
  completionContext,
  findCalls,
  isLiteral,
  localSymbols,
} from './parser';
import {
  FunctionDef,
  KeywordDef,
  ParamDef,
  Registry,
  formatParam,
  formatSignature,
  isVariadic,
  pickSignature,
  signaturesOf,
} from './registry';

export const LANGUAGE_ID = 'capl';
const SELECTOR: vscode.DocumentSelector = [{ language: LANGUAGE_ID }];

/** Members available on message / frame / signal objects (`this.dlc`, `msg.byte(0)`, `$Sig.raw` …). */
export const MEMBER_SELECTORS: Array<{ name: string; detail: string; call?: boolean }> = [
  { name: 'id', detail: 'Message identifier' },
  { name: 'dlc', detail: 'Data length code' },
  { name: 'dataLength', detail: 'Payload length in bytes (CAN FD)' },
  { name: 'can', detail: 'Channel number' },
  { name: 'msgChannel', detail: 'Channel number' },
  { name: 'dir', detail: 'Direction: Rx, Tx, TxRequest' },
  { name: 'time', detail: 'Time stamp (10 µs units)' },
  { name: 'time_ns', detail: 'Time stamp in nanoseconds' },
  { name: 'type', detail: 'Message type flags' },
  { name: 'rtr', detail: 'Remote transmission request flag' },
  { name: 'fdf', detail: 'CAN FD frame flag' },
  { name: 'brs', detail: 'CAN FD bit rate switch flag' },
  { name: 'esi', detail: 'CAN FD error state indicator' },
  { name: 'simulated', detail: '1 if sent by a simulated node' },
  { name: 'name', detail: 'Symbolic name' },
  { name: 'byte', detail: 'Access byte at index', call: true },
  { name: 'word', detail: 'Access word at index', call: true },
  { name: 'dword', detail: 'Access dword at index', call: true },
  { name: 'qword', detail: 'Access qword at index', call: true },
  { name: 'char', detail: 'Access char at index', call: true },
  { name: 'int', detail: 'Access int at index', call: true },
  { name: 'long', detail: 'Access long at index', call: true },
  { name: 'int64', detail: 'Access int64 at index', call: true },
  { name: 'phys', detail: 'Physical signal value' },
  { name: 'raw', detail: 'Raw signal value' },
  { name: 'raw64', detail: 'Raw 64-bit signal value' },
  { name: 'txReq', detail: 'Signal transmit request' },
  { name: 'startBit', detail: 'Signal start bit' },
  { name: 'bitLength', detail: 'Signal length in bits' },
  { name: 'SA', detail: 'J1939 source address' },
  { name: 'DA', detail: 'J1939 destination address' },
  { name: 'PGN', detail: 'J1939 parameter group number' },
  { name: 'Priority', detail: 'J1939 priority' },
  { name: 'FR_ChannelMask', detail: 'FlexRay channel mask' },
  { name: 'FR_Cycle', detail: 'FlexRay cycle' },
  { name: 'FR_SlotID', detail: 'FlexRay slot ID' },
  { name: 'FR_PayloadLength', detail: 'FlexRay payload length' },
];

interface Context {
  analysis: Analysis;
  definitions: DefinitionManager;
}

/** A user-defined (in this file or an included one) or registry function. */
interface ResolvedFunction {
  def: FunctionDef;
  user?: { symbol: CaplSymbol; uri: vscode.Uri; text?: string };
}

function cfg() {
  return vscode.workspace.getConfiguration('capl');
}

function insideCommentOrString(document: vscode.TextDocument, parsed: ParsedDocument, offset: number): boolean {
  if (offset === 0) return false;
  const orig = document.getText(new vscode.Range(document.positionAt(offset - 1), document.positionAt(offset)));
  return orig !== parsed.masked[offset - 1];
}

function symbolToFunction(s: CaplSymbol, source: string): FunctionDef {
  return { name: s.name, returnType: s.returnType ?? 'void', parameters: s.params ?? [], description: s.doc, category: s.kind, source };
}

function lookupFunction(ctx: Context, document: vscode.TextDocument, name: string): ResolvedFunction | undefined {
  const parsed = ctx.analysis.get(document);
  const own = parsed.symbols.find((s) => s.params && s.name === name);
  if (own) return { def: symbolToFunction(own, 'this file'), user: { symbol: own, uri: document.uri } };
  for (const inc of ctx.analysis.includes(document)) {
    const s = inc.parsed.symbols.find((x) => x.params && x.name === name);
    if (s) return { def: symbolToFunction(s, vscode.workspace.asRelativePath(inc.uri)), user: { symbol: s, uri: inc.uri, text: inc.text } };
  }
  const fn = ctx.definitions.current.functions.get(name);
  return fn ? { def: fn } : undefined;
}

function functionMarkdown(fn: FunctionDef): vscode.MarkdownString {
  const md = new vscode.MarkdownString();
  const sigs = signaturesOf(fn);
  md.appendCodeblock(sigs.map((p) => formatSignature(fn, p)).join('\n'), 'capl');
  if (fn.deprecated) md.appendMarkdown(`**Deprecated:** ${fn.deprecated}\n\n`);
  if (fn.description) md.appendMarkdown(fn.description + '\n\n');
  const params = sigs.flat().filter((p, i, all) => p.description && all.findIndex((q) => q.name === p.name) === i);
  if (params.length > 0) {
    md.appendMarkdown(params.map((p) => `- \`${p.name}\` — ${p.description}`).join('\n') + '\n\n');
  }
  const meta = [fn.category, fn.source].filter(Boolean).join(' · ');
  if (meta) md.appendMarkdown(`*${meta}*`);
  return md;
}

function keywordMarkdown(defs: KeywordDef[]): vscode.MarkdownString {
  const md = new vscode.MarkdownString();
  for (const k of defs) {
    md.appendMarkdown(`**${k.name}** — *${k.kind}*${k.source && k.source !== 'built-in' ? ` · ${k.source}` : ''}\n\n`);
    if (k.description) md.appendMarkdown(k.description + '\n\n');
  }
  return md;
}

function functionSnippet(fn: FunctionDef): vscode.SnippetString {
  const params = (fn.parameters ?? []).filter((p) => !isVariadic(p));
  const s = new vscode.SnippetString(fn.name + '(');
  params.forEach((p, i) => {
    if (i > 0) s.appendText(', ');
    s.appendPlaceholder(p.name);
  });
  s.appendText(')');
  return s;
}

function visibleSymbols(ctx: Context, document: vscode.TextDocument, offset: number): Array<{ symbol: CaplSymbol; uri: vscode.Uri; text?: string }> {
  const parsed = ctx.analysis.get(document);
  const types = ctx.definitions.current.typeNames();
  const out: Array<{ symbol: CaplSymbol; uri: vscode.Uri; text?: string }> = [];
  for (const s of localSymbols(parsed, offset, types)) out.push({ symbol: s, uri: document.uri });
  for (const s of globalSymbols(parsed)) out.push({ symbol: s, uri: document.uri });
  for (const inc of ctx.analysis.includes(document)) {
    for (const s of globalSymbols(inc.parsed)) out.push({ symbol: s, uri: inc.uri, text: inc.text });
  }
  return out;
}

const SYMBOL_COMPLETION_KIND: Record<string, vscode.CompletionItemKind> = {
  function: vscode.CompletionItemKind.Function,
  testcase: vscode.CompletionItemKind.Method,
  testfunction: vscode.CompletionItemKind.Method,
  struct: vscode.CompletionItemKind.Struct,
  enum: vscode.CompletionItemKind.Enum,
  variable: vscode.CompletionItemKind.Variable,
};

const KEYWORD_COMPLETION_KIND: Record<string, vscode.CompletionItemKind> = {
  keyword: vscode.CompletionItemKind.Keyword,
  type: vscode.CompletionItemKind.Class,
  event: vscode.CompletionItemKind.Event,
  constant: vscode.CompletionItemKind.Constant,
};

class CompletionProvider implements vscode.CompletionItemProvider {
  constructor(private readonly ctx: Context) {}

  provideCompletionItems(
    document: vscode.TextDocument,
    position: vscode.Position,
    _token: vscode.CancellationToken,
    context: vscode.CompletionContext,
  ): vscode.CompletionItem[] | undefined {
    if (!cfg().get<boolean>('completion.enable', true)) return undefined;
    const parsed = this.ctx.analysis.get(document);
    const offset = document.offsetAt(position);
    if (insideCommentOrString(document, parsed, offset)) return undefined;
    const reg = this.ctx.definitions.current;
    const cc = completionContext(parsed.masked, offset);
    // ' ' is only a trigger so that `on ` pops up the event list
    if (context.triggerCharacter === ' ' && cc.kind !== 'event') return undefined;

    if (cc.kind === 'event') {
      return reg.keywordsOfKind('event').map((k) => {
        const item = new vscode.CompletionItem(k.name, vscode.CompletionItemKind.Event);
        item.documentation = keywordMarkdown([k]);
        item.detail = 'event';
        if (k.snippet) item.insertText = new vscode.SnippetString(k.snippet.replace(/^on\s+/, ''));
        return item;
      });
    }
    if (cc.kind === 'member') {
      return MEMBER_SELECTORS.map((m) => {
        const item = new vscode.CompletionItem(m.name, vscode.CompletionItemKind.Field);
        item.detail = m.detail;
        if (m.call) item.insertText = new vscode.SnippetString(`${m.name}(\${1:0})`);
        return item;
      });
    }
    if (cc.kind === 'sysvar' || cc.kind === 'signal') return undefined;

    const items: vscode.CompletionItem[] = [];
    const seen = new Set<string>();
    for (const { symbol, uri } of visibleSymbols(this.ctx, document, offset)) {
      if (seen.has(symbol.name)) continue;
      seen.add(symbol.name);
      const item = new vscode.CompletionItem(symbol.name, SYMBOL_COMPLETION_KIND[symbol.kind] ?? vscode.CompletionItemKind.Text);
      item.sortText = '0' + symbol.name;
      if (symbol.params) {
        const fn = symbolToFunction(symbol, uri === document.uri ? 'this file' : vscode.workspace.asRelativePath(uri));
        item.detail = formatSignature(fn, fn.parameters ?? []);
        item.documentation = functionMarkdown(fn);
        item.insertText = functionSnippet(fn);
      } else {
        item.detail = symbol.detail;
      }
      items.push(item);
    }
    for (const fn of reg.functions.values()) {
      if (seen.has(fn.name)) continue;
      seen.add(fn.name);
      const item = new vscode.CompletionItem(fn.name, vscode.CompletionItemKind.Function);
      item.detail = formatSignature(fn, fn.parameters ?? []) + (fn.overloads?.length ? `  (+${fn.overloads.length} overload${fn.overloads.length > 1 ? 's' : ''})` : '');
      item.documentation = functionMarkdown(fn);
      item.insertText = functionSnippet(fn);
      item.sortText = (fn.source === 'built-in' ? '2' : '1') + fn.name;
      if (fn.deprecated) item.tags = [vscode.CompletionItemTag.Deprecated];
      item.command = { command: 'editor.action.triggerParameterHints', title: 'Parameter hints' };
      items.push(item);
    }
    for (const [name, defs] of reg.keywords) {
      const nonEvent = defs.filter((d) => d.kind !== 'event');
      if (nonEvent.length === 0 || seen.has(name)) continue;
      seen.add(name);
      const k = nonEvent[0];
      const item = new vscode.CompletionItem(name, KEYWORD_COMPLETION_KIND[k.kind ?? 'keyword']);
      item.detail = k.kind + (k.source && k.source !== 'built-in' ? ` · ${k.source}` : '');
      item.documentation = keywordMarkdown(defs);
      item.sortText = (k.source === 'built-in' ? '3' : '1') + name;
      if (k.snippet) item.insertText = new vscode.SnippetString(k.snippet);
      items.push(item);
    }
    return items;
  }
}

function wordAt(document: vscode.TextDocument, position: vscode.Position): { word: string; range: vscode.Range } | undefined {
  const range = document.getWordRangeAtPosition(position, /[A-Za-z_]\w*/);
  return range ? { word: document.getText(range), range } : undefined;
}

class HoverProvider implements vscode.HoverProvider {
  constructor(private readonly ctx: Context) {}

  provideHover(document: vscode.TextDocument, position: vscode.Position): vscode.Hover | undefined {
    if (!cfg().get<boolean>('hover.enable', true)) return undefined;
    const w = wordAt(document, position);
    if (!w) return undefined;
    const parsed = this.ctx.analysis.get(document);
    const offset = document.offsetAt(w.range.start);
    if (insideCommentOrString(document, parsed, offset + 1)) return undefined;

    const lineText = document.lineAt(position.line).text.slice(0, w.range.start.character);
    const afterOn = /^\s*on\s+$/.test(lineText);
    const reg = this.ctx.definitions.current;
    if (afterOn) {
      const ev = reg.keyword(w.word, 'event');
      if (ev) return new vscode.Hover(keywordMarkdown([ev]), w.range);
    }
    const memberAccess = /\.\s*$/.test(lineText);
    if (memberAccess) {
      const sel = MEMBER_SELECTORS.find((m) => m.name === w.word);
      if (sel) return new vscode.Hover(new vscode.MarkdownString(`**.${sel.name}** — ${sel.detail}`), w.range);
      return undefined;
    }

    const fn = lookupFunction(this.ctx, document, w.word);
    if (fn) return new vscode.Hover(functionMarkdown(fn.def), w.range);

    const sym = visibleSymbols(this.ctx, document, offset).find((s) => s.symbol.name === w.word);
    if (sym) {
      const md = new vscode.MarkdownString();
      md.appendCodeblock(`${sym.symbol.detail} ${sym.symbol.name}`.trim(), 'capl');
      if (sym.symbol.doc) md.appendMarkdown(sym.symbol.doc);
      return new vscode.Hover(md, w.range);
    }

    const defs = reg.keywords.get(w.word);
    if (defs) return new vscode.Hover(keywordMarkdown(defs.filter((d) => d.kind !== 'event').length ? defs.filter((d) => d.kind !== 'event') : defs), w.range);
    return undefined;
  }
}

class SignatureHelpProvider implements vscode.SignatureHelpProvider {
  constructor(private readonly ctx: Context) {}

  provideSignatureHelp(document: vscode.TextDocument, position: vscode.Position): vscode.SignatureHelp | undefined {
    if (!cfg().get<boolean>('signatureHelp.enable', true)) return undefined;
    const parsed = this.ctx.analysis.get(document);
    const offset = document.offsetAt(position);
    const call = callContextAt(parsed.masked, offset);
    if (!call) return undefined;
    const fn = lookupFunction(this.ctx, document, call.name);
    if (!fn) return undefined;
    const help = new vscode.SignatureHelp();
    const sigs = signaturesOf(fn.def);
    help.signatures = sigs.map((params) => {
      const label = formatSignature(fn.def, params);
      const info = new vscode.SignatureInformation(label, fn.def.description ? new vscode.MarkdownString(fn.def.description) : undefined);
      let cursor = label.indexOf('(') + 1;
      info.parameters = params.map((p) => {
        const text = formatParam(p);
        const start = label.indexOf(text, cursor);
        cursor = start + text.length;
        return new vscode.ParameterInformation([start, start + text.length], p.description ? new vscode.MarkdownString(p.description) : undefined);
      });
      return info;
    });
    help.activeSignature = pickSignature(fn.def, call.activeParam + 1);
    const active = sigs[help.activeSignature];
    const variadicAt = active.findIndex(isVariadic);
    help.activeParameter = variadicAt >= 0 && call.activeParam >= variadicAt ? variadicAt : call.activeParam;
    return help;
  }
}

class InlayHintsProvider implements vscode.InlayHintsProvider {
  private readonly emitter = new vscode.EventEmitter<void>();
  readonly onDidChangeInlayHints = this.emitter.event;
  constructor(private readonly ctx: Context) {}

  refresh(): void {
    this.emitter.fire();
  }

  provideInlayHints(document: vscode.TextDocument, range: vscode.Range): vscode.InlayHint[] {
    const mode = cfg().get<string>('inlayHints.parameterNames', 'literals');
    if (mode === 'none') return [];
    const parsed = this.ctx.analysis.get(document);
    const text = document.getText();
    const hints: vscode.InlayHint[] = [];
    const cache = new Map<string, ResolvedFunction | undefined>();
    for (const call of findCalls(parsed, document.offsetAt(range.start), document.offsetAt(range.end))) {
      if (call.args.length === 0) continue;
      if (!cache.has(call.name)) cache.set(call.name, lookupFunction(this.ctx, document, call.name));
      const fn = cache.get(call.name);
      if (!fn) continue;
      const params: ParamDef[] = signaturesOf(fn.def)[pickSignature(fn.def, call.args.length)];
      call.args.forEach(([a, b], i) => {
        const p = params[i];
        if (!p || isVariadic(p) || !p.name) return;
        const argText = text.slice(a, b);
        if (mode === 'literals' && !isLiteral(argText)) return;
        if (argText === p.name || argText.endsWith('.' + p.name)) return;
        const hint = new vscode.InlayHint(document.positionAt(a), `${p.name}:`, vscode.InlayHintKind.Parameter);
        hint.paddingRight = true;
        if (p.type || p.description) hint.tooltip = [p.type, p.description].filter(Boolean).join(' — ');
        hints.push(hint);
      });
    }
    return hints;
  }
}

const DOC_SYMBOL_KIND: Record<string, vscode.SymbolKind> = {
  variables: vscode.SymbolKind.Namespace,
  includes: vscode.SymbolKind.Package,
  event: vscode.SymbolKind.Event,
  function: vscode.SymbolKind.Function,
  testcase: vscode.SymbolKind.Method,
  testfunction: vscode.SymbolKind.Method,
  struct: vscode.SymbolKind.Struct,
  enum: vscode.SymbolKind.Enum,
  variable: vscode.SymbolKind.Variable,
};

class DocumentSymbolProvider implements vscode.DocumentSymbolProvider {
  constructor(private readonly ctx: Context) {}

  provideDocumentSymbols(document: vscode.TextDocument): vscode.DocumentSymbol[] {
    const parsed = this.ctx.analysis.get(document);
    const convert = (s: CaplSymbol): vscode.DocumentSymbol => {
      const name = s.kind === 'event' ? `on ${s.name}${s.detail ? ' ' + s.detail : ''}` : s.name;
      const detail =
        s.kind === 'function' ? `(${(s.params ?? []).map(formatParam).join(', ')}) → ${s.returnType}` : s.kind === 'event' ? '' : s.detail;
      const range = toRange(document, s.start, s.end);
      let sel = toRange(document, s.nameStart, s.nameEnd);
      if (!range.contains(sel)) sel = range;
      const ds = new vscode.DocumentSymbol(name, detail, DOC_SYMBOL_KIND[s.kind], range, sel);
      if (s.kind === 'includes') {
        ds.children = parsed.includes
          .filter((i) => i.start >= s.start && i.end <= s.end)
          .map((i) => {
            const r = toRange(document, i.start, i.end);
            return new vscode.DocumentSymbol(i.path, '', vscode.SymbolKind.File, r, r);
          });
      } else ds.children = (s.children ?? []).map(convert);
      return ds;
    };
    return parsed.symbols.map(convert);
  }
}

class DefinitionProvider implements vscode.DefinitionProvider {
  constructor(private readonly ctx: Context) {}

  provideDefinition(document: vscode.TextDocument, position: vscode.Position): vscode.Location | undefined {
    const w = wordAt(document, position);
    if (!w) return undefined;
    const offset = document.offsetAt(w.range.start);
    const fn = lookupFunction(this.ctx, document, w.word);
    const target = fn?.user ?? visibleSymbols(this.ctx, document, offset).find((s) => s.symbol.name === w.word);
    if (!target) return undefined;
    const range = target.text
      ? rangeInText(target.text, target.symbol.nameStart, target.symbol.nameEnd)
      : toRange(document, target.symbol.nameStart, target.symbol.nameEnd);
    return new vscode.Location(target.uri, range);
  }
}

class DocumentLinkProvider implements vscode.DocumentLinkProvider {
  constructor(private readonly ctx: Context) {}

  provideDocumentLinks(document: vscode.TextDocument): vscode.DocumentLink[] {
    const parsed = this.ctx.analysis.get(document);
    const text = document.getText();
    const out: vscode.DocumentLink[] = [];
    for (const inc of parsed.includes) {
      const target = this.ctx.analysis.resolve(document.uri.fsPath, inc.path);
      if (!target) continue;
      const at = text.indexOf(inc.path, inc.start);
      const link = new vscode.DocumentLink(toRange(document, at, at + inc.path.length), vscode.Uri.file(target));
      link.tooltip = target;
      out.push(link);
    }
    return out;
  }
}

class FoldingProvider implements vscode.FoldingRangeProvider {
  constructor(private readonly ctx: Context) {}

  provideFoldingRanges(document: vscode.TextDocument): vscode.FoldingRange[] {
    const parsed = this.ctx.analysis.get(document);
    const out: vscode.FoldingRange[] = [];
    for (const b of parsed.blocks) {
      const start = document.positionAt(b.start).line;
      const endLine = document.positionAt(b.end).line;
      const end = b.comment ? endLine : endLine - 1;
      if (end > start) out.push(new vscode.FoldingRange(start, end, b.comment ? vscode.FoldingRangeKind.Comment : undefined));
    }
    return out;
  }
}

export const SEMANTIC_LEGEND = new vscode.SemanticTokensLegend(
  ['keyword', 'type', 'event', 'enumMember', 'function'],
  ['defaultLibrary', 'deprecated', 'declaration'],
);

class SemanticTokensProvider implements vscode.DocumentSemanticTokensProvider {
  private readonly emitter = new vscode.EventEmitter<void>();
  readonly onDidChangeSemanticTokens = this.emitter.event;
  constructor(private readonly ctx: Context) {}

  refresh(): void {
    this.emitter.fire();
  }

  provideDocumentSemanticTokens(document: vscode.TextDocument): vscode.SemanticTokens {
    const builder = new vscode.SemanticTokensBuilder(SEMANTIC_LEGEND);
    const parsed = this.ctx.analysis.get(document);
    const reg: Registry = this.ctx.definitions.current;
    const userFns = new Set<string>();
    for (const s of parsed.symbols) if (s.params) userFns.add(s.name);
    for (const inc of this.ctx.analysis.includes(document)) for (const s of inc.parsed.symbols) if (s.params) userFns.add(s.name);
    const defStarts = new Set(parsed.symbols.filter((s) => s.params).map((s) => s.nameStart));

    const masked = parsed.masked;
    const re = /\b[A-Za-z_]\w*\b/g;
    let prevWord = '';
    for (let m; (m = re.exec(masked)); ) {
      const name = m[0];
      const start = m.index;
      const before = masked[start - 1];
      const afterOn = prevWord === 'on';
      prevWord = name;
      if (before === '.' || before === '$' || before === '@' || before === ':') continue;
      const rest = masked.slice(start + name.length, start + name.length + 40);
      const isCall = /^\s*\(/.test(rest);
      let type = -1;
      let mods = 0;
      if (isCall && userFns.has(name)) {
        type = 4;
        if (defStarts.has(start)) mods |= 1 << 2;
      } else if (isCall && reg.functions.has(name)) {
        type = 4;
        mods |= 1 << 0;
        if (reg.functions.get(name)!.deprecated) mods |= 1 << 1;
      } else {
        const defs = reg.keywords.get(name)?.filter((k) => k.source !== 'built-in');
        if (defs && defs.length > 0) {
          const ev = defs.find((d) => d.kind === 'event');
          const k = afterOn && ev ? ev : defs.find((d) => d.kind !== 'event');
          if (k) type = { keyword: 0, type: 1, event: 2, constant: 3 }[k.kind ?? 'keyword'];
        }
      }
      if (type < 0) continue;
      const pos = document.positionAt(start);
      builder.push(pos.line, pos.character, name.length, type, mods);
    }
    return builder.build();
  }
}

class Diagnostics implements vscode.Disposable {
  private readonly collection = vscode.languages.createDiagnosticCollection(LANGUAGE_ID);
  private readonly timers = new Map<string, NodeJS.Timeout>();
  constructor(private readonly ctx: Context) {}

  schedule(document: vscode.TextDocument, delay = 300): void {
    if (document.languageId !== LANGUAGE_ID) return;
    const key = document.uri.toString();
    clearTimeout(this.timers.get(key));
    this.timers.set(key, setTimeout(() => {
      this.timers.delete(key);
      this.update(document);
    }, delay));
  }

  clear(document: vscode.TextDocument): void {
    this.collection.delete(document.uri);
  }

  refreshAll(): void {
    for (const d of vscode.workspace.textDocuments) if (d.languageId === LANGUAGE_ID) this.update(d);
  }

  update(document: vscode.TextDocument): void {
    if (document.isClosed) return;
    const c = cfg();
    if (!c.get<boolean>('diagnostics.enable', true)) {
      this.collection.delete(document.uri);
      return;
    }
    const parsed = this.ctx.analysis.get(document);
    const reg = this.ctx.definitions.current;
    const sev = { error: vscode.DiagnosticSeverity.Error, warning: vscode.DiagnosticSeverity.Warning, info: vscode.DiagnosticSeverity.Information };
    const diags: vscode.Diagnostic[] = parsed.problems.map((p) => {
      const d = new vscode.Diagnostic(toRange(document, p.start, p.end), p.message, sev[p.severity]);
      d.source = 'capl';
      d.code = p.code;
      return d;
    });

    // unknown event types
    for (const s of parsed.symbols) {
      if (s.kind === 'event' && !reg.keyword(s.name, 'event')) {
        const d = new vscode.Diagnostic(
          toRange(document, s.nameStart, s.nameEnd),
          `Unknown event type "${s.name}". If it is valid for your CANoe version, add it as a custom event keyword.`,
          vscode.DiagnosticSeverity.Warning,
        );
        d.source = 'capl';
        d.code = 'unknown-event';
        diags.push(d);
      }
    }

    // calls: deprecated / unknown
    const checkUnknown = c.get<boolean>('diagnostics.unknownFunctions', false);
    let includes: IncludedFile[] | undefined;
    const known = new Set<string>();
    if (checkUnknown) {
      includes = this.ctx.analysis.includes(document);
      for (const s of parsed.symbols) if (s.params) known.add(s.name);
      for (const inc of includes) for (const s of inc.parsed.symbols) if (s.params) known.add(s.name);
    }
    for (const call of findCalls(parsed)) {
      const fn = reg.functions.get(call.name);
      const range = toRange(document, call.nameStart, call.nameStart + call.name.length);
      if (fn?.deprecated && !parsed.symbols.some((s) => s.params && s.name === call.name)) {
        const d = new vscode.Diagnostic(range, `"${call.name}" is deprecated: ${fn.deprecated}`, vscode.DiagnosticSeverity.Hint);
        d.tags = [vscode.DiagnosticTag.Deprecated];
        d.source = 'capl';
        d.code = 'deprecated';
        diags.push(d);
      } else if (checkUnknown && !fn && !known.has(call.name) && !reg.keywords.has(call.name)) {
        const d = new vscode.Diagnostic(range, `Unknown function "${call.name}".`, vscode.DiagnosticSeverity.Warning);
        d.source = 'capl';
        d.code = 'unknown-function';
        diags.push(d);
      }
    }
    this.collection.set(document.uri, diags);
  }

  dispose(): void {
    for (const t of this.timers.values()) clearTimeout(t);
    this.collection.dispose();
  }
}

class QuickFixProvider implements vscode.CodeActionProvider {
  static readonly kinds = [vscode.CodeActionKind.QuickFix];

  provideCodeActions(document: vscode.TextDocument, _range: vscode.Range, context: vscode.CodeActionContext): vscode.CodeAction[] {
    const out: vscode.CodeAction[] = [];
    for (const d of context.diagnostics) {
      if (d.source !== 'capl') continue;
      const name = document.getText(d.range);
      if (d.code === 'unknown-event') {
        const a = new vscode.CodeAction(`Add "${name}" as a custom CAPL event keyword`, vscode.CodeActionKind.QuickFix);
        a.command = { command: 'capl.addCustomKeyword', title: a.title, arguments: [{ name, kind: 'event' }] };
        a.diagnostics = [d];
        out.push(a);
      } else if (d.code === 'unknown-function') {
        const a = new vscode.CodeAction(`Add "${name}" as a custom CAPL function`, vscode.CodeActionKind.QuickFix);
        a.command = { command: 'capl.addCustomKeyword', title: a.title, arguments: [{ name, kind: 'function' }] };
        a.diagnostics = [d];
        out.push(a);
      }
    }
    return out;
  }
}

export function registerProviders(context: vscode.ExtensionContext, ctx: Context): { refresh: () => void } {
  const inlay = new InlayHintsProvider(ctx);
  const semantic = new SemanticTokensProvider(ctx);
  const diagnostics = new Diagnostics(ctx);

  context.subscriptions.push(
    vscode.languages.registerCompletionItemProvider(SELECTOR, new CompletionProvider(ctx), '.', ' '),
    vscode.languages.registerHoverProvider(SELECTOR, new HoverProvider(ctx)),
    vscode.languages.registerSignatureHelpProvider(SELECTOR, new SignatureHelpProvider(ctx), {
      triggerCharacters: ['(', ','],
      retriggerCharacters: [','],
    }),
    vscode.languages.registerInlayHintsProvider(SELECTOR, inlay),
    vscode.languages.registerDocumentSymbolProvider(SELECTOR, new DocumentSymbolProvider(ctx)),
    vscode.languages.registerDefinitionProvider(SELECTOR, new DefinitionProvider(ctx)),
    vscode.languages.registerDocumentLinkProvider(SELECTOR, new DocumentLinkProvider(ctx)),
    vscode.languages.registerFoldingRangeProvider(SELECTOR, new FoldingProvider(ctx)),
    vscode.languages.registerDocumentSemanticTokensProvider(SELECTOR, semantic, SEMANTIC_LEGEND),
    vscode.languages.registerCodeActionsProvider(SELECTOR, new QuickFixProvider(), { providedCodeActionKinds: QuickFixProvider.kinds }),
    diagnostics,
    vscode.workspace.onDidOpenTextDocument((d) => diagnostics.schedule(d, 0)),
    vscode.workspace.onDidChangeTextDocument((e) => diagnostics.schedule(e.document)),
    vscode.workspace.onDidCloseTextDocument((d) => {
      diagnostics.clear(d);
      ctx.analysis.forget(d);
    }),
    vscode.workspace.onDidChangeConfiguration((e) => {
      if (e.affectsConfiguration('capl')) {
        inlay.refresh();
        diagnostics.refreshAll();
      }
    }),
  );
  for (const d of vscode.workspace.textDocuments) diagnostics.schedule(d, 0);

  return {
    refresh: () => {
      inlay.refresh();
      semantic.refresh();
      diagnostics.refreshAll();
    },
  };
}
