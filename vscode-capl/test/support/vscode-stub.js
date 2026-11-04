// Minimal stand-in for the `vscode` module, enough to activate the extension and call its
// providers from plain Node (the real VS Code test host cannot be downloaded in every CI).
const path = require('node:path');

const enumProxy = () => new Proxy({}, { get: (_t, k) => k });

class Position {
  constructor(line, character) { this.line = line; this.character = character; }
  isBefore(o) { return this.line < o.line || (this.line === o.line && this.character < o.character); }
  isBeforeOrEqual(o) { return this.isBefore(o) || (this.line === o.line && this.character === o.character); }
}
class Range {
  constructor(a, b, c, d) {
    if (typeof a === 'number') { this.start = new Position(a, b); this.end = new Position(c, d); }
    else { this.start = a; this.end = b; }
  }
  contains(r) {
    const s = r.start ?? r; const e = r.end ?? r;
    return this.start.isBeforeOrEqual(s) && e.isBeforeOrEqual(this.end);
  }
}
class Disposable { constructor(fn) { this.fn = fn; } dispose() { this.fn && this.fn(); } static from() { return new Disposable(); } }
const disposable = () => new Disposable();
class EventEmitter {
  constructor() { this.listeners = []; this.event = (l) => { this.listeners.push(l); return disposable(); }; }
  fire(v) { for (const l of this.listeners) l(v); }
  dispose() {}
}
class MarkdownString {
  constructor(v = '') { this.value = v; }
  appendMarkdown(s) { this.value += s; return this; }
  appendText(s) { this.value += s; return this; }
  appendCodeblock(s, lang) { this.value += '```' + (lang || '') + '\n' + s + '\n```\n'; return this; }
}
class SnippetString {
  constructor(v = '') { this.value = v; this._n = 1; }
  appendText(s) { this.value += s.replace(/\$|}|\\/g, '\\$&'); return this; }
  appendPlaceholder(s) { this.value += '${' + this._n++ + ':' + s + '}'; return this; }
}
class Uri {
  constructor(fsPath) { this.fsPath = fsPath; this.scheme = 'file'; this.path = fsPath; }
  toString() { return 'file://' + this.fsPath; }
  static file(p) { return new Uri(p); }
  static joinPath(u, ...p) { return new Uri(path.join(u.fsPath, ...p)); }
}
class CompletionItem { constructor(label, kind) { this.label = label; this.kind = kind; } }
class SignatureHelp { constructor() { this.signatures = []; } }
class SignatureInformation { constructor(label, documentation) { this.label = label; this.documentation = documentation; this.parameters = []; } }
class ParameterInformation { constructor(label, documentation) { this.label = label; this.documentation = documentation; } }
class Hover { constructor(contents, range) { this.contents = contents; this.range = range; } }
class InlayHint { constructor(position, label, kind) { this.position = position; this.label = label; this.kind = kind; } }
class DocumentSymbol { constructor(name, detail, kind, range, selectionRange) { Object.assign(this, { name, detail, kind, range, selectionRange, children: [] }); } }
class Location { constructor(uri, range) { this.uri = uri; this.range = range; } }
class DocumentLink { constructor(range, target) { this.range = range; this.target = target; } }
class FoldingRange { constructor(start, end, kind) { Object.assign(this, { start, end, kind }); } }
class Diagnostic { constructor(range, message, severity) { Object.assign(this, { range, message, severity }); } }
class CodeAction { constructor(title, kind) { this.title = title; this.kind = kind; } }
class SemanticTokensLegend { constructor(t, m) { this.tokenTypes = t; this.tokenModifiers = m; } }
class SemanticTokensBuilder {
  constructor() { this.tokens = []; }
  push(line, char, len, type, mods) { this.tokens.push({ line, char, len, type, mods }); }
  build() { return { data: this.tokens }; }
}
class RelativePattern { constructor(base, pattern) { this.base = base; this.pattern = pattern; } }

/** TextDocument built from a string. */
function createDocument(text, fsPath, languageId = 'capl') {
  const lineStarts = [0];
  for (let i = 0; i < text.length; i++) if (text[i] === '\n') lineStarts.push(i + 1);
  const doc = {
    uri: Uri.file(fsPath), languageId, version: 1, isClosed: false, fileName: fsPath,
    getText(range) { return range ? text.slice(doc.offsetAt(range.start), doc.offsetAt(range.end)) : text; },
    offsetAt(p) { return Math.min(text.length, (lineStarts[p.line] ?? text.length) + p.character); },
    positionAt(o) {
      let line = 0;
      while (line + 1 < lineStarts.length && lineStarts[line + 1] <= o) line++;
      return new Position(line, o - lineStarts[line]);
    },
    lineAt(line) {
      const s = lineStarts[line]; const e = line + 1 < lineStarts.length ? lineStarts[line + 1] - 1 : text.length;
      return { text: text.slice(s, e) };
    },
    getWordRangeAtPosition(pos, re) {
      const lt = doc.lineAt(pos.line).text;
      const g = new RegExp(re.source, 'g');
      for (let m; (m = g.exec(lt)); ) {
        if (m.index <= pos.character && pos.character <= m.index + m[0].length) return new Range(pos.line, m.index, pos.line, m.index + m[0].length);
      }
      return undefined;
    },
  };
  return doc;
}

const state = { config: {}, providers: {}, diagnostics: new Map(), commands: {}, messages: [], workspaceFolders: undefined };

const register = (kind) => (_sel, provider, ...rest) => {
  (state.providers[kind] ??= []).push({ provider, rest });
  return disposable();
};

const vscode = {
  Position, Range, Disposable, EventEmitter, MarkdownString, SnippetString, Uri, CompletionItem, SignatureHelp,
  SignatureInformation, ParameterInformation, Hover, InlayHint, DocumentSymbol, Location, DocumentLink, FoldingRange,
  Diagnostic, CodeAction, SemanticTokensLegend, SemanticTokensBuilder, RelativePattern,
  CompletionItemKind: enumProxy(), CompletionItemTag: { Deprecated: 'Deprecated' }, SymbolKind: enumProxy(),
  InlayHintKind: enumProxy(), FoldingRangeKind: enumProxy(), DiagnosticSeverity: { Error: 0, Warning: 1, Information: 2, Hint: 3 },
  DiagnosticTag: enumProxy(), CodeActionKind: { QuickFix: 'quickfix' }, ConfigurationTarget: { Global: 1, Workspace: 2 },
  QuickPickItemKind: { Separator: -1 },
  languages: {
    registerCompletionItemProvider: register('completion'),
    registerHoverProvider: register('hover'),
    registerSignatureHelpProvider: register('signature'),
    registerInlayHintsProvider: register('inlay'),
    registerDocumentSymbolProvider: register('symbols'),
    registerDefinitionProvider: register('definition'),
    registerDocumentLinkProvider: register('links'),
    registerFoldingRangeProvider: register('folding'),
    registerDocumentSemanticTokensProvider: register('semantic'),
    registerCodeActionsProvider: register('codeActions'),
    createDiagnosticCollection: () => ({
      set: (uri, d) => state.diagnostics.set(uri.toString(), d),
      delete: (uri) => state.diagnostics.delete(uri.toString()),
      dispose() {},
    }),
  },
  workspace: {
    textDocuments: [],
    get workspaceFolders() { return state.workspaceFolders; },
    getConfiguration: (section) => ({
      get: (key, def) => {
        const v = state.config[`${section}.${key}`];
        return v === undefined ? def : v;
      },
      update: async (key, value) => { state.config[`${section}.${key}`] = value; },
    }),
    onDidChangeConfiguration: () => disposable(),
    onDidChangeWorkspaceFolders: () => disposable(),
    onDidOpenTextDocument: () => disposable(),
    onDidChangeTextDocument: () => disposable(),
    onDidCloseTextDocument: () => disposable(),
    createFileSystemWatcher: () => ({ onDidChange: disposable, onDidCreate: disposable, onDidDelete: disposable, dispose() {} }),
    asRelativePath: (u) => path.basename(u.fsPath ?? u),
    getWorkspaceFolder: () => undefined,
  },
  window: {
    createOutputChannel: () => ({ appendLine: (l) => state.messages.push(l), show() {}, dispose() {} }),
    showWarningMessage: async (m) => { state.messages.push('WARN ' + m); return undefined; },
    showInformationMessage: async (m) => { state.messages.push('INFO ' + m); return undefined; },
    activeTextEditor: undefined,
  },
  commands: {
    registerCommand: (id, fn) => { state.commands[id] = fn; return disposable(); },
  },
  env: { clipboard: { writeText: async () => {} } },
};

module.exports = { vscode, state, createDocument };
