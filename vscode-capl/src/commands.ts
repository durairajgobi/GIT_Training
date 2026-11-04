import * as fs from 'fs';
import * as path from 'path';
import * as vscode from 'vscode';
import { DefinitionManager } from './definitions';
import { LANGUAGE_ID } from './providers';
import { FunctionDef, KeywordDef, KeywordKind, formatSignature, parseParamList } from './registry';

type AddKind = KeywordKind | 'function';

const KEYWORD_FILE_TEMPLATE = {
  keywords: [
    { name: 'myEvent', kind: 'event', description: 'Example custom event: `on myEvent <selector>`.', snippet: 'on myEvent ${1:selector}\n{\n\t$0\n}' },
    { name: 'myType', kind: 'type', description: 'Example custom type.' },
  ],
  functions: [
    {
      name: 'MyLib_SendFrame',
      returnType: 'long',
      category: 'MyLib',
      description: 'Example custom function from a `.cin` library or CAPL DLL.',
      parameters: [
        { name: 'id', type: 'dword', description: 'CAN identifier' },
        { name: 'data', type: 'byte[]', description: 'payload' },
        { name: 'len', type: 'dword', description: 'payload length' },
      ],
    },
  ],
};

function target(): vscode.ConfigurationTarget {
  return (vscode.workspace.workspaceFolders?.length ?? 0) > 0
    ? vscode.ConfigurationTarget.Workspace
    : vscode.ConfigurationTarget.Global;
}

async function addCustom(defs: DefinitionManager, preset?: { name?: string; kind?: AddKind }): Promise<void> {
  const kind =
    preset?.kind ??
    ((
      await vscode.window.showQuickPick(
        [
          { label: 'function', description: 'Function with parameters (signature help + inlay hints)' },
          { label: 'keyword', description: 'Language keyword' },
          { label: 'type', description: 'Data / object type' },
          { label: 'event', description: 'Event selector used after `on`' },
          { label: 'constant', description: 'Named constant' },
        ],
        { title: 'CAPL: what do you want to add?' },
      )
    )?.label as AddKind | undefined);
  if (!kind) return;

  const name = await vscode.window.showInputBox({
    title: `CAPL: new ${kind}`,
    prompt: 'Name',
    value: preset?.name,
    validateInput: (v) => (/^[A-Za-z_]\w*$/.test(v) ? undefined : 'Must be a valid identifier'),
  });
  if (!name) return;

  const cfg = vscode.workspace.getConfiguration('capl');
  if (kind === 'function') {
    const params = await vscode.window.showInputBox({
      title: `CAPL: parameters of ${name}`,
      prompt: 'Comma-separated, e.g. "dword id, byte data[], ..." (leave empty for none)',
    });
    if (params === undefined) return;
    const returnType = await vscode.window.showInputBox({ title: `CAPL: return type of ${name}`, value: 'void' });
    if (returnType === undefined) return;
    const description = await vscode.window.showInputBox({ title: `CAPL: description / hint for ${name}`, prompt: 'Shown on hover (Markdown)' });
    if (description === undefined) return;
    const fn: FunctionDef = { name, returnType: returnType || 'void', parameters: parseParamList(params), description: description || undefined };
    const list = cfg.get<FunctionDef[]>('customFunctions', []).filter((f) => f.name !== name);
    await cfg.update('customFunctions', [...list, fn], target());
    vscode.window.showInformationMessage(`CAPL: added ${formatSignature(fn, fn.parameters ?? [])}`);
  } else {
    const description = await vscode.window.showInputBox({ title: `CAPL: description / hint for ${name}`, prompt: 'Shown on hover (Markdown)' });
    if (description === undefined) return;
    const kw: KeywordDef = { name, kind, description: description || undefined };
    const list = cfg.get<KeywordDef[]>('customKeywords', []).filter((k) => !(k.name === name && (k.kind ?? 'keyword') === kind));
    await cfg.update('customKeywords', [...list, kw], target());
    vscode.window.showInformationMessage(`CAPL: added ${kind} "${name}"`);
  }
  // configuration change triggers reload
  void defs;
}

async function createKeywordFile(): Promise<void> {
  const folder = vscode.workspace.workspaceFolders?.[0];
  const uri = await vscode.window.showSaveDialog({
    title: 'CAPL: create keyword definition file',
    defaultUri: folder ? vscode.Uri.joinPath(folder.uri, 'capl-keywords.json') : undefined,
    filters: { 'CAPL keyword file': ['json'] },
  });
  if (!uri) return;
  if (!fs.existsSync(uri.fsPath)) {
    fs.writeFileSync(uri.fsPath, JSON.stringify(KEYWORD_FILE_TEMPLATE, null, 2) + '\n', 'utf8');
  }
  const cfg = vscode.workspace.getConfiguration('capl');
  const owner = vscode.workspace.getWorkspaceFolder(uri);
  const entry = owner ? path.relative(owner.uri.fsPath, uri.fsPath).replace(/\\/g, '/') : uri.fsPath;
  const files = cfg.get<string[]>('keywordFiles', []);
  if (!files.includes(entry)) await cfg.update('keywordFiles', [...files, entry], target());
  await vscode.window.showTextDocument(uri);
}

interface DbItem extends vscode.QuickPickItem {
  insert: string | vscode.SnippetString;
}

async function browse(defs: DefinitionManager, onlyEvents: boolean): Promise<void> {
  const reg = defs.current;
  const items: DbItem[] = [];
  if (!onlyEvents) {
    const byCat = new Map<string, FunctionDef[]>();
    for (const fn of reg.functions.values()) {
      const c = fn.category ?? 'Other';
      byCat.set(c, [...(byCat.get(c) ?? []), fn]);
    }
    for (const [cat, fns] of [...byCat].sort(([a], [b]) => a.localeCompare(b))) {
      items.push({ label: cat, kind: vscode.QuickPickItemKind.Separator, insert: '' });
      for (const fn of fns.sort((a, b) => a.name.localeCompare(b.name))) {
        items.push({
          label: `$(symbol-function) ${fn.name}`,
          description: formatSignature(fn, fn.parameters ?? []),
          detail: [fn.deprecated ? `Deprecated: ${fn.deprecated}` : '', fn.description ?? '', fn.source && fn.source !== 'built-in' ? `(${fn.source})` : '']
            .filter(Boolean)
            .join(' '),
          insert: fn.name,
        });
      }
    }
  }
  const icons: Record<string, string> = { keyword: 'symbol-keyword', type: 'symbol-class', event: 'symbol-event', constant: 'symbol-constant' };
  for (const kind of (onlyEvents ? ['event'] : ['event', 'keyword', 'type', 'constant']) as KeywordKind[]) {
    items.push({ label: kind, kind: vscode.QuickPickItemKind.Separator, insert: '' });
    for (const k of reg.keywordsOfKind(kind).sort((a, b) => a.name.localeCompare(b.name))) {
      let insert: string | vscode.SnippetString = k.name;
      if (k.snippet) insert = new vscode.SnippetString(k.snippet);
      else if (kind === 'event') insert = new vscode.SnippetString(`on ${k.name} \${1}\n{\n\t$0\n}`);
      items.push({
        label: `$(${icons[kind]}) ${kind === 'event' ? 'on ' : ''}${k.name}`,
        detail: [k.description ?? '', k.source && k.source !== 'built-in' ? `(${k.source})` : ''].filter(Boolean).join(' '),
        insert,
      });
    }
  }
  const picked = await vscode.window.showQuickPick(items, {
    title: onlyEvents ? 'CAPL: insert event handler' : `CAPL keyword database (${reg.size} entries)`,
    matchOnDescription: true,
    matchOnDetail: true,
  });
  if (!picked) return;
  const editor = vscode.window.activeTextEditor;
  if (!editor || editor.document.languageId !== LANGUAGE_ID) {
    await vscode.env.clipboard.writeText(typeof picked.insert === 'string' ? picked.insert : picked.insert.value);
    vscode.window.showInformationMessage('CAPL: copied to clipboard (no CAPL editor active).');
    return;
  }
  if (typeof picked.insert === 'string') await editor.insertSnippet(new vscode.SnippetString(picked.insert.replace(/\$/g, '\\$')));
  else await editor.insertSnippet(picked.insert);
}

export function registerCommands(context: vscode.ExtensionContext, defs: DefinitionManager): void {
  context.subscriptions.push(
    vscode.commands.registerCommand('capl.reloadDefinitions', () => {
      const reg = defs.reload();
      vscode.window.showInformationMessage(`CAPL: loaded ${reg.size} keyword/function definitions.`);
    }),
    vscode.commands.registerCommand('capl.addCustomKeyword', (preset?: { name?: string; kind?: AddKind }) => addCustom(defs, preset)),
    vscode.commands.registerCommand('capl.createKeywordFile', createKeywordFile),
    vscode.commands.registerCommand('capl.showKeywordDatabase', () => browse(defs, false)),
    vscode.commands.registerCommand('capl.insertEventHandler', () => browse(defs, true)),
  );
}
