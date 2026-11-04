const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const r = require('../out/registry');

const builtins = JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'data', 'builtins.json'), 'utf8'));

test('builtins validate without errors and have no duplicates', () => {
  const { set, errors } = r.validateDefinitionSet(builtins, 'builtins');
  assert.deepEqual(errors, []);
  const fnNames = set.functions.map((f) => f.name);
  assert.equal(new Set(fnNames).size, fnNames.length, 'duplicate function names');
  const kw = set.keywords.map((k) => `${k.kind}:${k.name}`);
  assert.equal(new Set(kw).size, kw.length, 'duplicate keyword+kind');
  assert.ok(fnNames.length > 100);
});

test('builtin events are all known to the TextMate grammar', () => {
  const grammar = JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'syntaxes', 'capl.tmLanguage.json'), 'utf8'));
  const re = grammar.repository['event-handler'].begin;
  for (const k of builtins.keywords.filter((k) => k.kind === 'event')) {
    assert.ok(new RegExp(re).test(`on ${k.name}`), `grammar misses event ${k.name}`);
  }
});

test('validation reports bad entries', () => {
  const { set, errors } = r.validateDefinitionSet(
    { keywords: [{ name: '1bad' }, { name: 'ok', kind: 'weird' }, { name: 'good', kind: 'type' }], functions: [{ name: 'f', parameters: ['dword id', { name: 'x', type: 'int' }, 42] }] },
    'test',
  );
  assert.equal(errors.length, 3);
  assert.deepEqual(set.keywords.map((k) => k.name), ['good']);
  assert.deepEqual(set.functions[0].parameters, [{ name: 'id', type: 'dword' }, { name: 'x', type: 'int', description: undefined }]);
  assert.equal(r.validateDefinitionSet([], 'x').errors.length, 1);
});

test('layers override, disable and deprecate', () => {
  const reg = r.Registry.build(
    [
      { source: 'built-in', set: { keywords: [{ name: 'message', kind: 'type' }, { name: 'message', kind: 'event' }], functions: [{ name: 'write' }, { name: 'getValue' }, { name: 'old' }] } },
      { source: 'settings', set: { keywords: [{ name: 'message', kind: 'type', description: 'mine' }, { name: 'myEvent', kind: 'event' }], functions: [{ name: 'write', description: 'override' }] } },
    ],
    ['old'],
    { getValue: 'Use $sysvar' },
  );
  assert.equal(reg.keywords.get('message').length, 2);
  assert.equal(reg.keyword('message', 'type').description, 'mine');
  assert.equal(reg.keyword('message', 'type').source, 'settings');
  assert.equal(reg.functions.get('write').description, 'override');
  assert.equal(reg.functions.has('old'), false);
  assert.equal(reg.functions.get('getValue').deprecated, 'Use $sysvar');
  assert.deepEqual(reg.keywordsOfKind('event').map((k) => k.name), ['message', 'myEvent']);
});

test('parameter text parsing and signatures', () => {
  assert.deepEqual(r.parseParamText('char buf[]'), { name: 'buf', type: 'char[]' });
  assert.deepEqual(r.parseParamText('byte data[8][2]'), { name: 'data', type: 'byte[][]' });
  assert.deepEqual(r.parseParamText('diagRequest * req'), { name: 'req', type: 'diagRequest *' });
  assert.deepEqual(r.parseParamText('...'), { name: '...', type: '' });
  assert.deepEqual(r.parseParamList('dword id, char s[], ...').map((x) => x.name), ['id', 's', '...']);
  assert.deepEqual(r.parseParamList('void'), []);
  const fn = { name: 'f', returnType: 'long', parameters: [{ name: 'a', type: 'int' }, { name: '...' }], overloads: [[{ name: 'x' }, { name: 'y' }, { name: 'z' }]] };
  assert.equal(r.formatSignature(fn, fn.parameters), 'long f(int a, ...)');
  assert.equal(r.pickSignature(fn, 5), 0);
  const fn2 = { name: 'g', parameters: [{ name: 't' }, { name: 'd' }], overloads: [[{ name: 't' }, { name: 's' }, { name: 'ns' }]] };
  assert.equal(r.pickSignature(fn2, 2), 0);
  assert.equal(r.pickSignature(fn2, 3), 1);
});

test('snippets file is valid JSON with prefix and body', () => {
  const snippets = JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'snippets', 'capl.code-snippets'), 'utf8'));
  for (const [name, s] of Object.entries(snippets)) {
    assert.ok(s.prefix && Array.isArray(s.body), name);
  }
});
