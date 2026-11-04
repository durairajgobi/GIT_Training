import * as vscode from 'vscode';
import { Analysis } from './analysis';
import { registerCommands } from './commands';
import { DefinitionManager } from './definitions';
import { registerProviders } from './providers';

export function activate(context: vscode.ExtensionContext): void {
  const output = vscode.window.createOutputChannel('CAPL');
  const definitions = new DefinitionManager(context, output);
  const analysis = new Analysis();
  context.subscriptions.push(output, definitions);

  definitions.reload();
  const providers = registerProviders(context, { analysis, definitions });
  context.subscriptions.push(definitions.onDidChange(() => providers.refresh()));
  registerCommands(context, definitions);
}

export function deactivate(): void {
  /* disposables are cleaned up via context.subscriptions */
}
