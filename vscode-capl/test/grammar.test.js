const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const tm = require('vscode-textmate');
const onig = require('vscode-oniguruma');

const root = path.join(__dirname, '..');

async function loadGrammar() {
  const wasm = fs.readFileSync(require.resolve('vscode-oniguruma/release/onig.wasm')).buffer;
  await onig.loadWASM(wasm);
  const registry = new tm.Registry({
    onigLib: Promise.resolve({ createOnigScanner: (p) => new onig.OnigScanner(p), createOnigString: (s) => new onig.OnigString(s) }),
    loadGrammar: async () => tm.parseRawGrammar(fs.readFileSync(path.join(root, 'syntaxes', 'capl.tmLanguage.json'), 'utf8'), 'capl.tmLanguage.json'),
  });
  return registry.loadGrammar('source.capl');
}

function tokenize(grammar, src) {
  const out = [];
  let rule = tm.INITIAL;
  for (const line of src.split('\n')) {
    const r = grammar.tokenizeLine(line, rule);
    for (const t of r.tokens) out.push({ text: line.slice(t.startIndex, t.endIndex), scopes: t.scopes });
    rule = r.ruleStack;
  }
  return out;
}

function scopeOf(tokens, text) {
  const t = tokens.find((x) => x.text === text);
  assert.ok(t, `token "${text}" not found`);
  return t.scopes[t.scopes.length - 1];
}

test('grammar tokenizes the sample', async () => {
  const g = await loadGrammar();
  const toks = tokenize(g, fs.readFileSync(path.join(root, 'examples', 'sample.can'), 'utf8'));
  assert.equal(scopeOf(toks, 'variables'), 'keyword.other.section.capl');
  assert.equal(scopeOf(toks, 'sysvar_update'), 'keyword.other.event.capl');
  assert.equal(scopeOf(toks, 'msTimer'), 'storage.type.capl');
  assert.equal(scopeOf(toks, '0x100'), 'constant.numeric.hex.capl');
  assert.equal(scopeOf(toks, '%f'), 'constant.other.placeholder.capl');
  assert.equal(scopeOf(toks, 'EngineData::EngineSpeed'), 'variable.other.signal.capl');
  assert.equal(scopeOf(toks, 'sysvar::Engine::Enabled'), 'variable.other.sysvar.capl');
  assert.equal(scopeOf(toks, 'ResetCounters'), 'entity.name.function.call.capl'); // first use is a call
  assert.equal(scopeOf(tokenize(g, 'void ResetCounters(int c)\n{\n}'), 'ResetCounters'), 'entity.name.function.capl');
  assert.equal(scopeOf(toks, 'TC_EngineSpeed'), 'entity.name.function.test.capl');
  assert.equal(scopeOf(toks, 'dlc'), 'variable.other.member.selector.capl');
  assert.equal(scopeOf(toks, 'output'), 'entity.name.function.call.capl');
  assert.equal(scopeOf(toks, '#include'), 'keyword.control.directive.include.capl');
  assert.equal(scopeOf(toks, 'struct'), 'storage.modifier.capl');
});

test('grammar handles key events, comments and doc tags', async () => {
  const g = await loadGrammar();
  const ev = tokenize(g, 'on timer tCycle\n{\n}\non message CAN1.EngineData { }\n');
  assert.equal(scopeOf(ev, 'tCycle'), 'entity.name.function.event.capl');
  assert.equal(scopeOf(ev, 'message'), 'keyword.other.event.capl');
  const toks = tokenize(g, "on key ctrlF5 { }\n/** @param x the x */\nint x; // done\n");
  assert.equal(scopeOf(toks, 'ctrlF5'), 'constant.language.key.capl');
  const body = tokenize(g, 'void f()\n{\n  return g(1);\n  else if (x) h(2);\n}');
  assert.equal(scopeOf(body, 'return'), 'keyword.control.capl');
  assert.equal(scopeOf(body, 'g'), 'entity.name.function.call.capl');
  assert.equal(scopeOf(body, 'if'), 'keyword.control.capl');
  assert.equal(scopeOf(toks, '@param'), 'storage.type.class.doc.capl');
  assert.equal(scopeOf(toks, '// done'), 'comment.line.double-slash.capl');
});
