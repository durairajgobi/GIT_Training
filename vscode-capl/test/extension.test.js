// Activates the compiled extension against the vscode stub and exercises every provider.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const Module = require('node:module');
const { vscode, state, createDocument } = require('./support/vscode-stub');

const origResolve = Module._resolveFilename;
Module._resolveFilename = function (request, ...rest) {
  if (request === 'vscode') return 'vscode';
  return origResolve.call(this, request, ...rest);
};
require.cache.vscode = { id: 'vscode', filename: 'vscode', loaded: true, exports: vscode };

const root = path.join(__dirname, '..');
const samplePath = path.join(root, 'examples', 'sample.can');
const sampleText = fs.readFileSync(samplePath, 'utf8');

const ext = require('../out/extension');
const context = { subscriptions: [], asAbsolutePath: (p) => path.join(root, p) };
ext.activate(context);

const provider = (kind) => state.providers[kind][0].provider;
const posOf = (doc, needle, delta = 0) => doc.positionAt(sampleText.indexOf(needle) + delta);
const doc = createDocument(sampleText, samplePath);
vscode.workspace.textDocuments.push(doc);

test('activation loads builtins and registers all providers and commands', () => {
  for (const k of ['completion', 'hover', 'signature', 'inlay', 'symbols', 'definition', 'links', 'folding', 'semantic', 'codeActions']) {
    assert.ok(state.providers[k]?.length, `${k} provider registered`);
  }
  for (const c of ['capl.reloadDefinitions', 'capl.addCustomKeyword', 'capl.createKeywordFile', 'capl.showKeywordDatabase', 'capl.insertEventHandler']) {
    assert.equal(typeof state.commands[c], 'function', c);
  }
  assert.ok(state.messages.some((m) => /Loaded \d+ functions/.test(m)));
  assert.ok(!state.messages.some((m) => m.startsWith('WARN')), state.messages.join('\n'));
});

test('completion offers builtins, user functions, globals, locals and include symbols', () => {
  const items = provider('completion').provideCompletionItems(doc, posOf(doc, 'rpm = this'), {}, {});
  const labels = new Set(items.map((i) => i.label));
  for (const l of ['setTimer', 'testStepPass', 'ResetCounters', 'gCounter', 'msgEngine', 'rpm', 'Util_SendAndWait', 'kMaxRetries', 'msTimer', 'variables']) {
    assert.ok(labels.has(l), `missing ${l}`);
  }
  const st = items.find((i) => i.label === 'setTimer');
  assert.equal(st.insertText.value, 'setTimer(${1:t}, ${2:duration})');
  assert.match(st.detail, /\+1 overload/);
});

test('completion after `on ` lists events; after `.` lists selectors', () => {
  const d = createDocument('on sys', path.join(root, 'x.can'));
  const items = provider('completion').provideCompletionItems(d, new vscode.Position(0, 6), {}, {});
  assert.ok(items.some((i) => i.label === 'sysvar_update'));
  assert.ok(items.every((i) => i.kind === 'Event'));
  const m = createDocument('on start { this.', path.join(root, 'y.can'));
  const sel = provider('completion').provideCompletionItems(m, new vscode.Position(0, 16), {}, { triggerCharacter: '.' });
  assert.ok(sel.some((i) => i.label === 'dlc'));
  const sp = provider('completion').provideCompletionItems(m, new vscode.Position(0, 10), {}, { triggerCharacter: ' ' });
  assert.equal(sp, undefined, 'space only triggers after `on`');
});

test('no completion inside comments and strings', () => {
  const d = createDocument('// writ\nwrite("wri', path.join(root, 'z.can'));
  assert.equal(provider('completion').provideCompletionItems(d, new vscode.Position(0, 7), {}, {}), undefined);
  assert.equal(provider('completion').provideCompletionItems(d, new vscode.Position(1, 10), {}, {}), undefined);
});

test('hover for builtin function, event, user function with doc comment, variable', () => {
  const h = provider('hover');
  assert.match(h.provideHover(doc, posOf(doc, 'setTimerCyclic', 2)).contents.value, /void setTimerCyclic\(msTimer t, long period\)/);
  assert.match(h.provideHover(doc, posOf(doc, 'sysvar_update', 3)).contents.value, /system variable was written/);
  assert.match(h.provideHover(doc, posOf(doc, 'ResetCounters(0', 2)).contents.value, /Resets the counters/);
  assert.match(h.provideHover(doc, posOf(doc, 'gBuffer, elcount', 2)).contents.value, /char\[256\] gBuffer/);
  assert.match(h.provideHover(doc, posOf(doc, 'msTimer', 2)).contents.value, /millisecond resolution/);
});

test('signature help tracks active parameter and overloads', () => {
  const d = createDocument('on start { setTimer(t1, 1, ', path.join(root, 's.can'));
  const help = provider('signature').provideSignatureHelp(d, new vscode.Position(0, 27));
  assert.equal(help.signatures.length, 2);
  assert.equal(help.activeSignature, 1, 'three-argument overload');
  assert.equal(help.activeParameter, 2);
  const [a, b] = help.signatures[1].parameters[2].label;
  assert.equal(help.signatures[1].label.slice(a, b), 'long nanoseconds');

  const v = createDocument('on start { write("%d %d", 1, 2, ', path.join(root, 'v.can'));
  const vh = provider('signature').provideSignatureHelp(v, new vscode.Position(0, 32));
  assert.equal(vh.activeParameter, 1, 'variadic parameter stays active');
});

test('inlay hints label literal arguments', () => {
  const inlay = provider('inlay');
  const hints = inlay.provideInlayHints(doc, new vscode.Range(doc.positionAt(0), doc.positionAt(sampleText.length)));
  const labels = hints.map((h) => h.label);
  assert.ok(labels.includes('period:'), 'setTimerCyclic(tCycle, 100)');
  assert.ok(labels.includes('counter:'), 'user function ResetCounters(0, 0)');
  assert.ok(labels.includes('timeout:'));
  assert.ok(!labels.includes('t:'), 'non-literal tCycle not hinted in literals mode');
  state.config['capl.inlayHints.parameterNames'] = 'all';
  assert.ok(inlay.provideInlayHints(doc, new vscode.Range(doc.positionAt(0), doc.positionAt(sampleText.length))).some((h) => h.label === 't:'));
  state.config['capl.inlayHints.parameterNames'] = 'none';
  assert.equal(inlay.provideInlayHints(doc, new vscode.Range(doc.positionAt(0), doc.positionAt(sampleText.length))).length, 0);
  delete state.config['capl.inlayHints.parameterNames'];
});

test('outline, definition, include links and folding', () => {
  const syms = provider('symbols').provideDocumentSymbols(doc);
  assert.deepEqual(syms.map((s) => s.name).slice(0, 5), ['includes', 'variables', 'on preStart', 'on start', 'on timer tCycle']);
  assert.ok(syms[1].children.some((c) => c.name === 'gLookup'));
  assert.equal(syms[0].children[0].name, 'utils.cin');

  const def = provider('definition').provideDefinition(doc, posOf(doc, 'ResetCounters(0', 1));
  assert.equal(def.range.start.line, doc.positionAt(sampleText.indexOf('void ResetCounters') + 5).line);

  const links = provider('links').provideDocumentLinks(doc);
  assert.equal(path.basename(links[0].target.fsPath), 'utils.cin');
  assert.ok(provider('folding').provideFoldingRanges(doc).length > 5);
});

test('diagnostics: clean sample; unknown event, deprecated and unknown function with quick fixes', async () => {
  const diags = () => state.diagnostics.get(bad.uri.toString()) ?? [];
  const bad = createDocument('on myBusEvent X\n{\n  getValue(ev);\n  MyLib_Send(1);\n}\n', path.join(root, 'bad.can'));
  state.config['capl.deprecatedFunctions'] = { getValue: 'Use system variables' };
  state.config['capl.diagnostics.unknownFunctions'] = true;
  vscode.workspace.textDocuments.push(bad);
  // reloading the registry refreshes diagnostics of all open CAPL documents
  await state.commands['capl.reloadDefinitions']();
  const msgs = diags().map((d) => d.message);
  assert.ok(msgs.some((m) => m.startsWith('Unknown event type "myBusEvent"')), msgs.join('\n'));
  assert.ok(msgs.some((m) => m.includes('"getValue" is deprecated: Use system variables')));
  assert.ok(msgs.some((m) => m === 'Unknown function "MyLib_Send".'));
  assert.deepEqual(state.diagnostics.get(doc.uri.toString()), [], 'sample.can has no diagnostics');

  const actions = new (state.providers.codeActions[0].provider.constructor)().provideCodeActions(bad, null, { diagnostics: diags() });
  assert.deepEqual(actions.map((a) => a.title).sort(), ['Add "MyLib_Send" as a custom CAPL function', 'Add "myBusEvent" as a custom CAPL event keyword']);
});

test('custom keywords and functions from settings and keyword files become known', async () => {
  const bad = vscode.workspace.textDocuments.find((d) => d.fileName.endsWith('bad.can'));
  const tmp = fs.mkdtempSync(path.join(require('node:os').tmpdir(), 'capl-'));
  const kwFile = path.join(tmp, 'capl-keywords.json');
  fs.writeFileSync(kwFile, JSON.stringify({ keywords: [{ name: 'myBusEvent', kind: 'event', description: 'My bus' }] }));
  state.config['capl.keywordFiles'] = [kwFile];
  state.config['capl.customFunctions'] = [{ name: 'MyLib_Send', returnType: 'long', parameters: [{ name: 'id', type: 'dword' }], description: 'Sends.' }];
  await state.commands['capl.reloadDefinitions']();
  const msgs = (state.diagnostics.get(bad.uri.toString()) ?? []).map((d) => d.message);
  assert.ok(!msgs.some((m) => m.includes('myBusEvent') || m.includes('MyLib_Send')), msgs.join('\n'));

  const hover = provider('hover').provideHover(bad, new vscode.Position(3, 4));
  assert.match(hover.contents.value, /long MyLib_Send\(dword id\)/);
  const inlay = provider('inlay').provideInlayHints(bad, new vscode.Range(0, 0, 5, 0));
  assert.ok(inlay.some((h) => h.label === 'id:'));
  const tokens = provider('semantic').provideDocumentSemanticTokens(bad).data;
  assert.ok(tokens.some((t) => t.line === 0 && t.type === 2), 'custom event highlighted as event');

  // add via command (quick fix path) writes into settings
  vscode.window.showInputBox = async (o) => (o.value ?? 'A custom hint');
  await state.commands['capl.addCustomKeyword']({ name: 'kMyConst', kind: 'constant' });
  assert.deepEqual(state.config['capl.customKeywords'], [{ name: 'kMyConst', kind: 'constant', description: 'A custom hint' }]);

  // disabled builtins disappear
  state.config['capl.disabledBuiltins'] = ['beep'];
  await state.commands['capl.reloadDefinitions']();
  const items = provider('completion').provideCompletionItems(doc, posOf(doc, 'rpm = this'), {}, {});
  assert.ok(!items.some((i) => i.label === 'beep'));
  fs.rmSync(tmp, { recursive: true });
});
