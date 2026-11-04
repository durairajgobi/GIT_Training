import * as fs from 'fs';
import * as path from 'path';
import * as vscode from 'vscode';
import { DefinitionLayer, Registry, validateDefinitionSet } from './registry';

/**
 * Builds the keyword registry from builtins, keyword files and settings, and rebuilds it
 * when settings or watched files change.
 */
export class DefinitionManager implements vscode.Disposable {
  private registry: Registry = new Registry();
  private watchers: vscode.FileSystemWatcher[] = [];
  private readonly emitter = new vscode.EventEmitter<Registry>();
  private readonly disposables: vscode.Disposable[] = [];
  readonly onDidChange = this.emitter.event;

  constructor(private readonly context: vscode.ExtensionContext, private readonly output: vscode.OutputChannel) {
    this.disposables.push(
      vscode.workspace.onDidChangeConfiguration((e) => {
        if (
          e.affectsConfiguration('capl.customKeywords') ||
          e.affectsConfiguration('capl.customFunctions') ||
          e.affectsConfiguration('capl.keywordFiles') ||
          e.affectsConfiguration('capl.disabledBuiltins') ||
          e.affectsConfiguration('capl.deprecatedFunctions')
        ) {
          this.reload();
        }
      }),
      vscode.workspace.onDidChangeWorkspaceFolders(() => this.reload()),
    );
  }

  get current(): Registry {
    return this.registry;
  }

  /** Absolute paths of configured keyword files (relative paths resolved per workspace folder). */
  keywordFilePaths(): string[] {
    const cfg = vscode.workspace.getConfiguration('capl');
    const files = cfg.get<string[]>('keywordFiles', []);
    const folders = vscode.workspace.workspaceFolders ?? [];
    const out = new Set<string>();
    for (const f of files) {
      const expanded = f.replace(/^~(?=$|[\\/])/, process.env.HOME ?? process.env.USERPROFILE ?? '~');
      if (path.isAbsolute(expanded)) out.add(path.normalize(expanded));
      else if (folders.length === 0) continue;
      else for (const folder of folders) out.add(path.join(folder.uri.fsPath, expanded));
    }
    return [...out];
  }

  reload(): Registry {
    const cfg = vscode.workspace.getConfiguration('capl');
    const layers: DefinitionLayer[] = [];
    const problems: string[] = [];

    // 1. builtins
    const builtinPath = this.context.asAbsolutePath(path.join('data', 'builtins.json'));
    try {
      const { set, errors } = validateDefinitionSet(JSON.parse(fs.readFileSync(builtinPath, 'utf8')), 'builtins');
      problems.push(...errors);
      layers.push({ source: 'built-in', set });
    } catch (e) {
      problems.push(`builtins: ${(e as Error).message}`);
    }

    // 2. keyword files
    for (const file of this.keywordFilePaths()) {
      if (!fs.existsSync(file)) {
        // relative paths are tried in every workspace folder; only complain if none exist
        continue;
      }
      try {
        const { set, errors } = validateDefinitionSet(JSON.parse(fs.readFileSync(file, 'utf8')), path.basename(file));
        problems.push(...errors);
        layers.push({ source: path.basename(file), set });
      } catch (e) {
        problems.push(`${file}: ${(e as Error).message}`);
      }
    }
    const configured = cfg.get<string[]>('keywordFiles', []);
    const found = this.keywordFilePaths().filter((f) => fs.existsSync(f));
    if (configured.length > 0 && found.length === 0 && (vscode.workspace.workspaceFolders?.length ?? 0) > 0) {
      problems.push(`None of the files in capl.keywordFiles were found: ${configured.join(', ')}`);
    }

    // 3. settings
    const { set, errors } = validateDefinitionSet(
      { keywords: cfg.get('customKeywords', []), functions: cfg.get('customFunctions', []) },
      'settings',
    );
    problems.push(...errors);
    layers.push({ source: 'settings', set });

    this.registry = Registry.build(
      layers,
      cfg.get<string[]>('disabledBuiltins', []),
      cfg.get<Record<string, string>>('deprecatedFunctions', {}),
    );

    this.output.appendLine(
      `[${new Date().toLocaleTimeString()}] Loaded ${this.registry.functions.size} functions and ` +
        `${this.registry.size - this.registry.functions.size} keywords from ${layers.map((l) => l.source).join(', ')}`,
    );
    for (const p of problems) this.output.appendLine(`  ! ${p}`);
    if (problems.length > 0) {
      void vscode.window
        .showWarningMessage(`CAPL: ${problems.length} problem(s) in keyword definitions.`, 'Show Details')
        .then((choice) => choice && this.output.show(true));
    }

    this.rewatch();
    this.emitter.fire(this.registry);
    return this.registry;
  }

  private rewatch(): void {
    for (const w of this.watchers) w.dispose();
    this.watchers = [];
    for (const file of this.keywordFilePaths()) {
      const w = vscode.workspace.createFileSystemWatcher(
        new vscode.RelativePattern(vscode.Uri.file(path.dirname(file)), path.basename(file)),
      );
      const reload = () => this.reload();
      w.onDidChange(reload);
      w.onDidCreate(reload);
      w.onDidDelete(reload);
      this.watchers.push(w);
    }
  }

  dispose(): void {
    for (const w of this.watchers) w.dispose();
    for (const d of this.disposables) d.dispose();
    this.emitter.dispose();
  }
}
