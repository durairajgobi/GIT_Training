const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const p = require('../out/parser');

const sample = fs.readFileSync(path.join(__dirname, '..', 'examples', 'sample.can'), 'utf8');

test('maskText blanks comments, strings and preprocessor lines but keeps offsets', () => {
  const src = '#include "a.cin"\nint a; // c\nwrite("x;{");/* { */\n';
  const m = p.maskText(src);
  assert.equal(m.length, src.length);
  assert.equal(m.split('\n').length, src.split('\n').length);
  assert.ok(!m.includes('include'));
  assert.ok(!m.includes('{'));
  assert.ok(m.includes('write("   ")'));
});

test('parses sections, events, functions, testcases', () => {
  const doc = p.parseDocument(sample);
  const kinds = doc.symbols.map((s) => `${s.kind}:${s.name}${s.detail && s.kind === 'event' ? ' ' + s.detail : ''}`);
  assert.deepEqual(kinds, [
    'includes:includes',
    'variables:variables',
    'event:preStart',
    'event:start',
    'event:timer tCycle',
    'event:message EngineData',
    'event:sysvar_update Engine::Enabled',
    "event:key 'r'",
    'event:diagResponse Engine.SerialNumber_Read',
    'function:ResetCounters',
    'testcase:TC_EngineSpeed',
  ]);
  assert.deepEqual(doc.includes.map((i) => i.path), ['utils.cin']);
  assert.deepEqual(doc.problems, []);
});

test('parses variable declarations including messages, arrays, struct and enum', () => {
  const doc = p.parseDocument(sample);
  const vars = doc.symbols.find((s) => s.kind === 'variables').children;
  const summary = vars.map((v) => `${v.kind}:${v.name}:${v.detail}`);
  assert.deepEqual(summary, [
    'variable:msgEngine:message 0x100',
    'variable:msgData:message CAN1.EngineData',
    'variable:tCycle:msTimer',
    'variable:gCounter:int',
    'variable:gErrors:int',
    'variable:gBuffer:char[256]',
    'struct:Point:struct',
    'enum:State:enum',
    'variable:gLookup:int[char[]]',
  ]);
  const gCounter = vars.find((v) => v.name === 'gCounter');
  assert.equal(sample.slice(gCounter.nameStart, gCounter.nameEnd), 'gCounter');
  const point = vars.find((v) => v.name === 'Point');
  assert.deepEqual(point.children.map((c) => c.name), ['x', 'y']);
});

test('function parameters, return type and doc comment', () => {
  const doc = p.parseDocument(sample);
  const fn = doc.symbols.find((s) => s.name === 'ResetCounters');
  assert.equal(fn.returnType, 'void');
  assert.deepEqual(fn.params, [{ name: 'counter', type: 'int' }, { name: 'errors', type: 'int' }]);
  assert.equal(fn.doc, 'Resets the counters to the given values.');
  assert.equal(sample.slice(fn.nameStart, fn.nameEnd), 'ResetCounters');

  const cin = p.parseDocument(fs.readFileSync(path.join(__dirname, '..', 'examples', 'utils.cin'), 'utf8'));
  const util = cin.symbols.find((s) => s.name === 'Util_SendAndWait');
  assert.equal(util.returnType, 'long');
  assert.deepEqual(util.params.map((x) => x.name), ['req', 'timeoutMs']);
  assert.match(util.doc, /^Sends a diagnostic request/);
  assert.match(util.doc, /@param timeoutMs/);
});

test('local symbols include params and local declarations', () => {
  const doc = p.parseDocument(sample);
  const types = new Set(['int', 'float', 'char', 'message']);
  const at = sample.indexOf('rpm = this');
  assert.deepEqual(p.localSymbols(doc, at, types).map((s) => s.name), ['rpm']);
  const inFn = sample.indexOf('gCounter = counter');
  assert.deepEqual(p.localSymbols(doc, inFn, types).map((s) => s.name), ['counter', 'errors']);
});

test('callContextAt finds function and active parameter', () => {
  const src = 'on start { snprintf(buf, elcount(buf), "%d", ';
  const m = p.maskText(src);
  const c = p.callContextAt(m, src.length);
  assert.equal(c.name, 'snprintf');
  assert.equal(c.activeParam, 3);
  const inner = 'x = f(a, g(1, ';
  assert.deepEqual([p.callContextAt(inner, inner.length).name, p.callContextAt(inner, inner.length).activeParam], ['g', 1]);
  assert.equal(p.callContextAt('if (', 4), undefined);
});

test('findCalls returns argument ranges and skips definitions/selectors', () => {
  const doc = p.parseDocument(sample);
  const calls = p.findCalls(doc);
  const names = calls.map((c) => c.name);
  assert.ok(names.includes('setTimerCyclic'));
  assert.ok(!names.includes('byte'), 'member selector .byte() is not a call');
  assert.equal(names.filter((n) => n === 'ResetCounters').length, 1, 'definition not counted');
  const st = calls.find((c) => c.name === 'setTimerCyclic');
  assert.deepEqual(st.args.map(([a, b]) => sample.slice(a, b)), ['tCycle', '100']);
  const sn = calls.find((c) => c.name === 'snprintf');
  assert.deepEqual(sn.args.map(([a, b]) => sample.slice(a, b)), ['gBuffer', 'elcount(gBuffer)', '"reset at %d"', 'timeNow()']);
});

test('structural diagnostics', () => {
  const doc = p.parseDocument('variables { int a; }\nvariables { }\non start { if (a) { }\non start { }\n');
  const msgs = doc.problems.map((x) => x.message);
  assert.ok(msgs.some((m) => m.includes('Duplicate "variables"')));
  assert.ok(msgs.some((m) => m.startsWith("Unclosed '{'")));
  const doc2 = p.parseDocument('on start { }\non start { }\nvoid f() { ) }');
  const m2 = doc2.problems.map((x) => x.message);
  assert.ok(m2.some((m) => m.includes('Duplicate event procedure "on start"')));
  assert.ok(m2.some((m) => m === "Unexpected ')'"));
});

test('completion context', () => {
  assert.equal(p.completionContext('on sys', 6).kind, 'event');
  assert.equal(p.completionContext('  this.dl', 9).kind, 'member');
  assert.equal(p.completionContext('x = @sysvar::Eng', 16).kind, 'sysvar');
  assert.equal(p.completionContext('x = $Engine::Sp', 15).kind, 'signal');
  assert.equal(p.completionContext('  wri', 5).kind, 'general');
});

test('isLiteral', () => {
  for (const t of ['1', '-2', '0x1F', '1.5', '"s"', "'a'", 'true', '100ms'.slice(0, 3)]) assert.ok(p.isLiteral(t), t);
  for (const t of ['a', 'f(1)', 'a + 1', 'this.dlc']) assert.ok(!p.isLiteral(t), t);
});
