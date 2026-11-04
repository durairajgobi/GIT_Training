/**
 * Keyword / function database. Pure TypeScript (no `vscode` import) so it can be unit-tested.
 *
 * Definitions come from several layers, applied in order (later layers win):
 *   1. data/builtins.json shipped with the extension
 *   2. JSON files listed in `capl.keywordFiles`
 *   3. `capl.customKeywords` / `capl.customFunctions` settings
 * `capl.disabledBuiltins` hides entries, `capl.deprecatedFunctions` marks functions deprecated.
 */

export type KeywordKind = 'keyword' | 'type' | 'event' | 'constant';
export const KEYWORD_KINDS: readonly KeywordKind[] = ['keyword', 'type', 'event', 'constant'];

export interface KeywordDef {
  name: string;
  kind?: KeywordKind;
  description?: string;
  snippet?: string;
  /** Where the definition came from (filled in by the registry). */
  source?: string;
}

export interface ParamDef {
  name: string;
  type?: string;
  description?: string;
}

export interface FunctionDef {
  name: string;
  returnType?: string;
  parameters?: ParamDef[];
  overloads?: ParamDef[][];
  description?: string;
  category?: string;
  deprecated?: string;
  source?: string;
}

export interface DefinitionSet {
  keywords?: KeywordDef[];
  functions?: FunctionDef[];
}

export interface DefinitionLayer {
  source: string;
  set: DefinitionSet;
}

const IDENT = /^[A-Za-z_][A-Za-z0-9_]*$/;

/** Validates a definition set loaded from JSON/settings. Returns human-readable problems. */
export function validateDefinitionSet(raw: unknown, source: string): { set: DefinitionSet; errors: string[] } {
  const errors: string[] = [];
  const set: DefinitionSet = { keywords: [], functions: [] };
  if (typeof raw !== 'object' || raw === null || Array.isArray(raw)) {
    return { set, errors: [`${source}: expected an object with "keywords" and/or "functions"`] };
  }
  const obj = raw as Record<string, unknown>;

  const kws = obj.keywords;
  if (kws !== undefined && !Array.isArray(kws)) errors.push(`${source}: "keywords" must be an array`);
  if (Array.isArray(kws)) {
    kws.forEach((k, i) => {
      const where = `${source}: keywords[${i}]`;
      if (typeof k !== 'object' || k === null || typeof (k as KeywordDef).name !== 'string') {
        errors.push(`${where}: missing "name"`);
        return;
      }
      const kw = k as KeywordDef;
      if (!IDENT.test(kw.name)) {
        errors.push(`${where}: "${kw.name}" is not a valid identifier`);
        return;
      }
      if (kw.kind !== undefined && !KEYWORD_KINDS.includes(kw.kind)) {
        errors.push(`${where}: unknown kind "${kw.kind}" (use ${KEYWORD_KINDS.join(', ')})`);
        return;
      }
      set.keywords!.push({ name: kw.name, kind: kw.kind ?? 'keyword', description: str(kw.description), snippet: str(kw.snippet) });
    });
  }

  const fns = obj.functions;
  if (fns !== undefined && !Array.isArray(fns)) errors.push(`${source}: "functions" must be an array`);
  if (Array.isArray(fns)) {
    fns.forEach((f, i) => {
      const where = `${source}: functions[${i}]`;
      if (typeof f !== 'object' || f === null || typeof (f as FunctionDef).name !== 'string') {
        errors.push(`${where}: missing "name"`);
        return;
      }
      const fn = f as FunctionDef;
      if (!IDENT.test(fn.name)) {
        errors.push(`${where}: "${fn.name}" is not a valid identifier`);
        return;
      }
      const params = normalizeParams(fn.parameters, `${where}.parameters`, errors);
      const overloads = Array.isArray(fn.overloads)
        ? fn.overloads.map((o, j) => normalizeParams(o, `${where}.overloads[${j}]`, errors))
        : undefined;
      set.functions!.push({
        name: fn.name,
        returnType: str(fn.returnType),
        parameters: params,
        overloads,
        description: str(fn.description),
        category: str(fn.category),
        deprecated: str(fn.deprecated),
      });
    });
  }
  return { set, errors };
}

function normalizeParams(raw: unknown, where: string, errors: string[]): ParamDef[] {
  if (raw === undefined) return [];
  if (!Array.isArray(raw)) {
    errors.push(`${where}: must be an array`);
    return [];
  }
  const out: ParamDef[] = [];
  raw.forEach((p, i) => {
    if (typeof p === 'string') {
      out.push(parseParamText(p));
    } else if (typeof p === 'object' && p !== null && typeof (p as ParamDef).name === 'string') {
      const pd = p as ParamDef;
      out.push({ name: pd.name, type: str(pd.type), description: str(pd.description) });
    } else {
      errors.push(`${where}[${i}]: expected {"name": ...} or "type name"`);
    }
  });
  return out;
}

function str(v: unknown): string | undefined {
  return typeof v === 'string' ? v : undefined;
}

/** Parses "char buf[]" / "long" / "..." into a ParamDef. */
export function parseParamText(text: string): ParamDef {
  const t = text.trim();
  if (t === '...' || t === '') return { name: t || '...' , type: '' };
  const m = /^(.*?)\s*\b([A-Za-z_]\w*)\s*((?:\[\s*\w*\s*\])*)$/.exec(t);
  if (m && m[1].trim() !== '') {
    return { name: m[2], type: (m[1].trim() + (m[3] ? '[]'.repeat((m[3].match(/\[/g) || []).length) : '')).replace(/\s+/g, ' ') };
  }
  return { name: t, type: '' };
}

/** Splits "char s[], long len" into params (used by the "Add custom function" command). */
export function parseParamList(text: string): ParamDef[] {
  const t = text.trim();
  if (t === '' || t === 'void') return [];
  return t.split(',').map(parseParamText);
}

export function isVariadic(p: ParamDef): boolean {
  return p.name === '...';
}

export function formatParam(p: ParamDef): string {
  if (isVariadic(p)) return '...';
  return p.type ? `${p.type} ${p.name}` : p.name;
}

export function formatSignature(fn: { name: string; returnType?: string }, params: ParamDef[]): string {
  return `${fn.returnType ?? 'void'} ${fn.name}(${params.map(formatParam).join(', ')})`;
}

/** All parameter lists of a function: main signature first, then overloads. */
export function signaturesOf(fn: FunctionDef): ParamDef[][] {
  return [fn.parameters ?? [], ...(fn.overloads ?? [])];
}

/** Picks the overload best matching `argCount` arguments. */
export function pickSignature(fn: FunctionDef, argCount: number): number {
  const sigs = signaturesOf(fn);
  let best = 0;
  for (let i = 0; i < sigs.length; i++) {
    const s = sigs[i];
    const variadic = s.some(isVariadic);
    const fixed = s.filter((p) => !isVariadic(p)).length;
    if (s.length === argCount || (variadic && argCount >= fixed)) return i;
    if (s.length >= argCount && (sigs[best].length < argCount || s.length < sigs[best].length)) best = i;
  }
  return best;
}

export class Registry {
  /** name → definitions (a name may exist with several kinds, e.g. `message` is type and event). */
  readonly keywords = new Map<string, KeywordDef[]>();
  readonly functions = new Map<string, FunctionDef>();

  static build(layers: DefinitionLayer[], disabled: string[] = [], deprecated: Record<string, string> = {}): Registry {
    const r = new Registry();
    const off = new Set(disabled);
    for (const layer of layers) {
      for (const kw of layer.set.keywords ?? []) {
        const def: KeywordDef = { ...kw, kind: kw.kind ?? 'keyword', source: layer.source };
        const list = (r.keywords.get(def.name) ?? []).filter((k) => k.kind !== def.kind);
        list.push(def);
        r.keywords.set(def.name, list);
      }
      for (const fn of layer.set.functions ?? []) {
        r.functions.set(fn.name, { ...fn, source: layer.source });
      }
    }
    for (const name of off) {
      r.keywords.delete(name);
      r.functions.delete(name);
    }
    for (const [name, msg] of Object.entries(deprecated)) {
      const fn = r.functions.get(name);
      if (fn) r.functions.set(name, { ...fn, deprecated: msg || 'deprecated' });
      else r.functions.set(name, { name, deprecated: msg || 'deprecated', source: 'capl.deprecatedFunctions' });
    }
    return r;
  }

  keywordsOfKind(kind: KeywordKind): KeywordDef[] {
    const out: KeywordDef[] = [];
    for (const list of this.keywords.values()) for (const k of list) if (k.kind === kind) out.push(k);
    return out;
  }

  keyword(name: string, kind?: KeywordKind): KeywordDef | undefined {
    const list = this.keywords.get(name);
    if (!list) return undefined;
    return kind ? list.find((k) => k.kind === kind) : list[0];
  }

  typeNames(): Set<string> {
    return new Set(this.keywordsOfKind('type').map((k) => k.name));
  }

  get size(): number {
    let n = this.functions.size;
    for (const l of this.keywords.values()) n += l.length;
    return n;
  }
}
