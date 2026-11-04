import * as fs from 'fs';
import * as path from 'path';
import * as vscode from 'vscode';
import { CaplSymbol, ParsedDocument, parseDocument } from './parser';

export interface IncludedFile {
  uri: vscode.Uri;
  text: string;
  parsed: ParsedDocument;
}

/** Parses documents on demand and caches results per version / mtime. */
export class Analysis {
  private readonly docs = new Map<string, { version: number; parsed: ParsedDocument }>();
  private readonly files = new Map<string, { mtime: number; file: IncludedFile }>();

  get(document: vscode.TextDocument): ParsedDocument {
    const key = document.uri.toString();
    const hit = this.docs.get(key);
    if (hit && hit.version === document.version) return hit.parsed;
    const parsed = parseDocument(document.getText());
    this.docs.set(key, { version: document.version, parsed });
    return parsed;
  }

  forget(document: vscode.TextDocument): void {
    this.docs.delete(document.uri.toString());
  }

  /** Recursively resolves `#include` files (relative to the including file). */
  includes(document: vscode.TextDocument, maxDepth = 8): IncludedFile[] {
    const out: IncludedFile[] = [];
    const seen = new Set<string>([path.normalize(document.uri.fsPath)]);
    const visit = (fromFile: string, parsed: ParsedDocument, depth: number) => {
      if (depth > maxDepth) return;
      for (const inc of parsed.includes) {
        const target = this.resolve(fromFile, inc.path);
        if (!target || seen.has(target)) continue;
        seen.add(target);
        const file = this.load(target);
        if (!file) continue;
        out.push(file);
        visit(target, file.parsed, depth + 1);
      }
    };
    if (document.uri.scheme === 'file' || document.uri.scheme === 'untitled') {
      visit(document.uri.fsPath, this.get(document), 0);
    }
    return out;
  }

  /** Path of an include relative to the including file; tries workspace folders as fallback. */
  resolve(fromFile: string, includePath: string): string | undefined {
    const normalized = includePath.replace(/\\/g, '/');
    const candidates = [
      path.isAbsolute(normalized) ? normalized : path.resolve(path.dirname(fromFile), normalized),
      ...(vscode.workspace.workspaceFolders ?? []).map((f) => path.resolve(f.uri.fsPath, normalized)),
    ];
    for (const c of candidates) {
      const p = path.normalize(c);
      if (fs.existsSync(p)) return p;
      // case-insensitive fallback (CANoe projects often come from Windows)
      const dir = path.dirname(p);
      try {
        const match = fs.readdirSync(dir).find((n) => n.toLowerCase() === path.basename(p).toLowerCase());
        if (match) return path.join(dir, match);
      } catch {
        /* directory does not exist */
      }
    }
    return undefined;
  }

  private load(file: string): IncludedFile | undefined {
    const open = vscode.workspace.textDocuments.find((d) => path.normalize(d.uri.fsPath) === file);
    if (open) {
      return { uri: open.uri, text: open.getText(), parsed: this.get(open) };
    }
    try {
      const mtime = fs.statSync(file).mtimeMs;
      const hit = this.files.get(file);
      if (hit && hit.mtime === mtime) return hit.file;
      const text = fs.readFileSync(file, 'utf8');
      const result = { uri: vscode.Uri.file(file), text, parsed: parseDocument(text) };
      this.files.set(file, { mtime, file: result });
      return result;
    } catch {
      return undefined;
    }
  }
}

/** Global (top-level and `variables`) symbols, flattened. */
export function globalSymbols(parsed: ParsedDocument): CaplSymbol[] {
  const out: CaplSymbol[] = [];
  for (const s of parsed.symbols) {
    if (s.kind === 'variables') out.push(...(s.children ?? []));
    else if (s.kind !== 'includes' && s.kind !== 'event') out.push(s);
  }
  return out;
}

export function toRange(document: vscode.TextDocument, start: number, end: number): vscode.Range {
  return new vscode.Range(document.positionAt(start), document.positionAt(end));
}

/** Range in a file that is not necessarily open, from text offsets. */
export function rangeInText(text: string, start: number, end: number): vscode.Range {
  const pos = (o: number) => {
    const before = text.slice(0, o);
    const line = (before.match(/\n/g) || []).length;
    return new vscode.Position(line, o - (before.lastIndexOf('\n') + 1));
  };
  return new vscode.Range(pos(start), pos(end));
}
